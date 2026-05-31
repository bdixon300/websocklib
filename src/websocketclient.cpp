#include "websocketclient.hpp"

#include <cstdint>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <stdexcept>

namespace websocklib {

// Member destruction order is reverse of declaration.  m_tcpClient is
// declared last so it is destroyed first: ~TCPClient joins the receive thread,
// ensuring m_wsConnected, m_closeCallback, m_webFrameSerializer, and all
// captured [this] references are still live for the thread's full lifetime.
WebSocketClient::WebSocketClient(const std::string &host, unsigned int port,
                                 std::string path, bool useTls)
    : m_hostUrl(host), m_port(port), m_path(std::move(path)),
      m_webFrameSerializer(
          // text callback
          [this](const std::string &msg) {
            if (m_messageCallback) m_messageCallback(msg);
          },
          // binary callback
          [this](const std::vector<uint8_t> &msg) {
            if (m_binaryCallback) m_binaryCallback(msg);
          },
          // pong-send callback (responds to server Ping frames)
          [this](std::vector<uint8_t> frame) {
            try {
              m_tcpClient.sendMessage(std::span<const uint8_t>(frame));
            } catch (...) {}
          }),
      m_tcpClient(
          host,
          [this](std::vector<uint8_t> &packets) { readDataHandle(packets); },
          port, useTls, std::chrono::milliseconds(0),
          [this] {
            // Fires when the receive loop exits due to server close or error.
            // Not fired for user-initiated disconnect() (m_wsConnected is
            // already false before tcpDisconnect() is called).
            if (m_wsConnected.exchange(false) && m_closeCallback)
              m_closeCallback();
          }) {}

WebSocketClient::~WebSocketClient() {}

void WebSocketClient::connect(std::chrono::milliseconds timeout) {
  // Reset state to support reconnect after a previous disconnect.
  m_httpBuffer.clear();
  m_connectionState.store(ConnectionState::Disconnected);
  m_handshakeException = nullptr;

  sendHandshake(timeout);

  std::unique_lock<std::mutex> lock(m_handshakeMtx);
  auto pred = [this] {
    return m_connectionState.load() == ConnectionState::Connected ||
           m_handshakeException != nullptr;
  };

  if (timeout.count() > 0) {
    if (!m_handshakeCv.wait_for(lock, timeout, pred))
      throw std::runtime_error("WebSocket connection timed out");
  } else {
    m_handshakeCv.wait(lock, pred);
  }

  if (m_handshakeException)
    std::rethrow_exception(m_handshakeException);
}

void WebSocketClient::sendHandshake(std::chrono::milliseconds connectTimeout) {
  m_webSocketKey = generateWebSocketKey();
  m_connectionState.store(ConnectionState::AwaitingHandshakeResponse);
  m_tcpClient.tcpConnect(connectTimeout);
  auto payload = generateHandShakePayload();
  m_tcpClient.sendMessage(std::span<const uint8_t>(
      reinterpret_cast<const uint8_t *>(payload.data()), payload.size()));
}

std::string WebSocketClient::generateHandShakePayload() {
  std::ostringstream ss;
  ss << "GET " << m_path << " HTTP/1.1\r\n"
     << "Host: " << m_hostUrl << "\r\n"
     << "Upgrade: websocket\r\n"
     << "Connection: Upgrade\r\n"
     << "Sec-WebSocket-Key: " << m_webSocketKey << "\r\n"
     << "Sec-WebSocket-Version: 13\r\n"
     << "\r\n";
  return ss.str();
}

void WebSocketClient::readDataHandle(std::vector<uint8_t> &packets) {
  if (m_connectionState.load() == ConnectionState::AwaitingHandshakeResponse) {
    m_httpBuffer.insert(m_httpBuffer.end(), packets.begin(), packets.end());
    packets.clear();

    auto pos = m_httpBuffer.find("\r\n\r\n");
    if (pos == std::string::npos)
      return;  // incomplete headers — wait for more data

    std::string httpResponse = m_httpBuffer.substr(0, pos + 4);
    std::string trailing     = m_httpBuffer.substr(pos + 4);
    m_httpBuffer.clear();

    try {
      parseHandshakeResponse(httpResponse);
    } catch (...) {
      std::lock_guard<std::mutex> lk(m_handshakeMtx);
      m_handshakeException = std::current_exception();
      m_handshakeCv.notify_one();
      return;
    }

    m_connectionState.store(ConnectionState::Connected);
    m_wsConnected = true;
    m_handshakeCv.notify_one();

    // Any bytes that arrived after the HTTP headers belong to the first
    // WebSocket frame(s).
    if (!trailing.empty()) {
      std::vector<uint8_t> trailingBytes(trailing.begin(), trailing.end());
      m_webFrameSerializer.convertRawPacketsToWebframes(trailingBytes);
    }
    return;
  }

  if (m_connectionState.load() == ConnectionState::Connected)
    m_webFrameSerializer.convertRawPacketsToWebframes(packets);
}

void WebSocketClient::parseHandshakeResponse(const std::string &response) {
  if (response.find("HTTP/1.1 101") == std::string::npos)
    throw std::runtime_error(
        "WebSocket upgrade failed: unexpected HTTP status");

  const std::string acceptHeader = "Sec-WebSocket-Accept: ";
  auto pos = response.find(acceptHeader);
  if (pos == std::string::npos)
    throw std::runtime_error(
        "WebSocket upgrade failed: missing Sec-WebSocket-Accept header");

  pos += acceptHeader.size();
  auto end     = response.find("\r\n", pos);
  std::string received = response.substr(pos, end - pos);

  if (received != computeExpectedAcceptKey(m_webSocketKey))
    throw std::runtime_error(
        "WebSocket upgrade failed: invalid Sec-WebSocket-Accept value");
}

std::string WebSocketClient::computeExpectedAcceptKey(const std::string &key) {
  static constexpr char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string input = key + WS_GUID;

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int  digestLen = 0;
  EVP_MD_CTX   *ctx       = EVP_MD_CTX_new();
  if (!ctx)
    throw std::runtime_error("EVP_MD_CTX_new failed");
  if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, input.data(), input.size()) != 1 ||
      EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
    EVP_MD_CTX_free(ctx);
    throw std::runtime_error("EVP SHA-1 digest failed");
  }
  EVP_MD_CTX_free(ctx);

  BIO *b64 = BIO_new(BIO_f_base64());
  BIO *mem = BIO_new(BIO_s_mem());
  BIO_push(b64, mem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, digest, static_cast<int>(digestLen));
  BIO_flush(b64);
  char *data;
  long  len = BIO_get_mem_data(mem, &data);
  std::string result(data, len);
  BIO_free_all(b64);
  return result;
}

std::string WebSocketClient::generateWebSocketKey() {
  unsigned char randomBytes[16];
  if (RAND_bytes(randomBytes, sizeof(randomBytes)) != 1)
    throw std::runtime_error("RAND_bytes failed: insufficient entropy");

  BIO *b64 = BIO_new(BIO_f_base64());
  BIO *mem = BIO_new(BIO_s_mem());
  BIO_push(b64, mem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, randomBytes, sizeof(randomBytes));
  BIO_flush(b64);
  char *data;
  long  len = BIO_get_mem_data(mem, &data);
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

void WebSocketClient::onBinaryMessage(
    std::function<void(const std::vector<uint8_t> &)> cb) {
  m_binaryCallback = std::move(cb);
}

void WebSocketClient::onClose(std::function<void()> cb) {
  m_closeCallback = std::move(cb);
}

void WebSocketClient::disconnect() {
  if (!m_wsConnected.exchange(false)) return;
  m_connectionState.store(ConnectionState::Disconnected);
  auto frame = m_webFrameSerializer.buildFrame(0x08, nullptr, 0);
  try {
    m_tcpClient.sendMessage(std::span<const uint8_t>(frame));
  } catch (...) {}
  m_tcpClient.tcpDisconnect();
}

void WebSocketClient::send(std::string_view message) {
  if (!m_wsConnected.load())
    throw std::runtime_error("send() called on a disconnected WebSocket");
  auto frame = m_webFrameSerializer.buildFrame(
      0x01, reinterpret_cast<const uint8_t *>(message.data()), message.size());
  m_tcpClient.sendMessage(std::span<const uint8_t>(frame));
}

void WebSocketClient::send(std::span<const uint8_t> message) {
  if (!m_wsConnected.load())
    throw std::runtime_error("send() called on a disconnected WebSocket");
  auto frame =
      m_webFrameSerializer.buildFrame(0x02, message.data(), message.size());
  m_tcpClient.sendMessage(std::span<const uint8_t>(frame));
}

} // namespace websocklib
