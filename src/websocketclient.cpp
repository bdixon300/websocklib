#include "websocketclient.hpp"
#include <arpa/inet.h>
#include <cstdint>
#include <netdb.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>

namespace websocklib {

constexpr char PLACEHOLDER_ORIGIN[] = "https://placeholderorigin.com";

static std::string resolveHost(const std::string &host, uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *results = nullptr;
  int err =
      getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
  if (err != 0)
    throw std::runtime_error(gai_strerror(err));

  char ipStr[INET6_ADDRSTRLEN];
  if (results->ai_family == AF_INET) {
    auto *addr = reinterpret_cast<sockaddr_in *>(results->ai_addr);
    inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
  } else {
    auto *addr = reinterpret_cast<sockaddr_in6 *>(results->ai_addr);
    inet_ntop(AF_INET6, &addr->sin6_addr, ipStr, sizeof(ipStr));
  }

  freeaddrinfo(results);
  return std::string(ipStr);
}

// Member destruction order (reverse of declaration): m_tcpClient first, then
// m_closeCallback/m_wsConnected, then the rest. ~TCPClient joins the receive
// thread, so by the time m_wsConnected and m_closeCallback are destroyed no
// more callbacks can fire — all [this] captures are safe.
WebSocketClient::WebSocketClient(const std::string &hostUrl, unsigned int port)
    : m_hostUrl(hostUrl), m_port(port),
      m_connectionState(ConnectionState::Disconnected),
      m_webFrameSerializer([this](const std::string &msg) {
        if (m_messageCallback)
          m_messageCallback(msg);
      }),
      m_tcpClient(resolveHost(hostUrl, static_cast<uint16_t>(port)),
                  [this](std::vector<uint8_t> &packets) {
                    readDataHandle(packets);
                  },
                  port,
                  [this] {
                    // Fires when the receive loop exits due to server close or error,
                    // not for user-initiated disconnect() (m_wsConnected is already false).
                    if (m_wsConnected.exchange(false) && m_closeCallback)
                      m_closeCallback();
                  }) {}

WebSocketClient::~WebSocketClient() {}

void WebSocketClient::connect() {
  m_handshakeException = nullptr;
  sendHandshake();

  std::unique_lock<std::mutex> lock(m_handshakeMtx);
  m_handshakeCv.wait(lock, [this] {
    return m_connectionState == ConnectionState::Connected ||
           m_handshakeException != nullptr;
  });

  if (m_handshakeException)
    std::rethrow_exception(m_handshakeException);
}

void WebSocketClient::sendHandshake() {
  m_webSocketKey = generateWebSocketKey();
  m_connectionState = ConnectionState::AwaitingHandshakeResponse;
  m_tcpClient.tcpConnect();
  auto payload = generateHandShakePayload();
  std::vector<uint8_t> bytes(payload.begin(), payload.end());
  m_tcpClient.sendMessage(std::span<uint8_t>(bytes));
}

std::string WebSocketClient::generateHandShakePayload() {
  std::ostringstream ss;
  ss << "GET / HTTP/1.1\r\n"
     << "Host: " << m_hostUrl << "\r\n"
     << "Upgrade: websocket\r\n"
     << "Connection: Upgrade\r\n"
     << "Sec-WebSocket-Key: " << m_webSocketKey << "\r\n"
     << "Origin: " << PLACEHOLDER_ORIGIN << "\r\n"
     << "Sec-WebSocket-Protocol: chat, superchat\r\n"
     << "Sec-WebSocket-Version: 13\r\n"
     << "\r\n";
  return ss.str();
}

void WebSocketClient::readDataHandle(std::vector<uint8_t> &packets) {
  if (m_connectionState == ConnectionState::AwaitingHandshakeResponse) {
    m_httpBuffer.insert(m_httpBuffer.end(), packets.begin(), packets.end());
    packets.clear();

    auto pos = m_httpBuffer.find("\r\n\r\n");
    if (pos == std::string::npos)
      return;

    std::string httpResponse = m_httpBuffer.substr(0, pos + 4);
    std::string trailing = m_httpBuffer.substr(pos + 4);
    m_httpBuffer.clear();

    try {
      parseHandshakeResponse(httpResponse);
    } catch (...) {
      std::lock_guard<std::mutex> lk(m_handshakeMtx);
      m_handshakeException = std::current_exception();
      m_handshakeCv.notify_one();
      return;
    }

    {
      std::lock_guard<std::mutex> lk(m_handshakeMtx);
      m_connectionState = ConnectionState::Connected;
    }
    m_wsConnected = true;
    m_handshakeCv.notify_one();

    if (!trailing.empty()) {
      std::vector<uint8_t> trailingBytes(trailing.begin(), trailing.end());
      m_webFrameSerializer.convertRawPacketsToWebframes(trailingBytes);
    }
    return;
  }

  if (m_connectionState == ConnectionState::Connected)
    m_webFrameSerializer.convertRawPacketsToWebframes(packets);
}

void WebSocketClient::parseHandshakeResponse(const std::string &response) {
  if (response.find("HTTP/1.1 101") == std::string::npos)
    throw std::runtime_error("WebSocket upgrade failed: unexpected HTTP status");

  const std::string acceptHeader = "Sec-WebSocket-Accept: ";
  auto pos = response.find(acceptHeader);
  if (pos == std::string::npos)
    throw std::runtime_error(
        "WebSocket upgrade failed: missing Sec-WebSocket-Accept header");

  pos += acceptHeader.size();
  auto end = response.find("\r\n", pos);
  std::string received = response.substr(pos, end - pos);

  if (received != computeExpectedAcceptKey(m_webSocketKey))
    throw std::runtime_error(
        "WebSocket upgrade failed: invalid Sec-WebSocket-Accept value");
}

std::string WebSocketClient::computeExpectedAcceptKey(const std::string &key) {
  static constexpr char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string input = key + WS_GUID;

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digestLen = 0;
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
  EVP_DigestUpdate(ctx, input.data(), input.size());
  EVP_DigestFinal_ex(ctx, digest, &digestLen);
  EVP_MD_CTX_free(ctx);

  BIO *b64 = BIO_new(BIO_f_base64());
  BIO *mem = BIO_new(BIO_s_mem());
  BIO_push(b64, mem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, digest, static_cast<int>(digestLen));
  BIO_flush(b64);
  char *data;
  long len = BIO_get_mem_data(mem, &data);
  std::string result(data, len);
  BIO_free_all(b64);
  return result;
}

std::string WebSocketClient::generateWebSocketKey() {
  unsigned char random_bytes[16];
  RAND_bytes(random_bytes, sizeof(random_bytes));

  BIO *b64 = BIO_new(BIO_f_base64());
  BIO *mem = BIO_new(BIO_s_mem());
  BIO_push(b64, mem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, random_bytes, sizeof(random_bytes));
  BIO_flush(b64);

  char *data;
  long len = BIO_get_mem_data(mem, &data);
  std::string key(data, len);
  BIO_free_all(b64);
  return key;
}

bool WebSocketClient::isWebSocketConnected() const {
  return m_wsConnected.load();
}

void WebSocketClient::onMessage(std::function<void(const std::string &)> cb) {
  m_messageCallback = std::move(cb);
}

void WebSocketClient::onClose(std::function<void()> cb) {
  m_closeCallback = std::move(cb);
}

void WebSocketClient::disconnect() {
  if (!m_wsConnected.exchange(false))
    return; // already disconnected or never connected
  auto frame = m_webFrameSerializer.buildFrame(0x08, nullptr, 0); // opcode 0x08 = close
  try {
    m_tcpClient.sendMessage(std::span<uint8_t>(frame));
  } catch (...) {}
  m_tcpClient.tcpDisconnect();
}

void WebSocketClient::send(const std::string &message) {
  if (!m_wsConnected.load())
    throw std::runtime_error("send() called on a disconnected WebSocket");
  auto frame = m_webFrameSerializer.buildFrame(0x01,
                          reinterpret_cast<const uint8_t *>(message.data()),
                          message.size());
  m_tcpClient.sendMessage(std::span<uint8_t>(frame));
}

void WebSocketClient::send(const std::vector<uint8_t> &message) {
  if (!m_wsConnected.load())
    throw std::runtime_error("send() called on a disconnected WebSocket");
  auto frame = m_webFrameSerializer.buildFrame(0x02, message.data(), message.size());
  m_tcpClient.sendMessage(std::span<uint8_t>(frame));
}

} // namespace websocklib
