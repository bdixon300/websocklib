#include "websocketclient.hpp"
#include <arpa/inet.h> // inet_ntop
#include <cstdint>
#include <netdb.h> // getaddrinfo, freeaddrinfo, gai_strerror
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>

namespace websocklib {

constexpr char PLACEHOLDER_ORIGIN[] = "https://placeholderorigin.com";

// Freefunction to resolve ip addresses from host url
std::string resolveHost(const std::string &host,
                        uint16_t port = DEFAULT_WS_PORT) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *results = nullptr;
  int err =
      getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
  if (err != 0)
    throw std::runtime_error(gai_strerror(err));

  // Pull the IP string out of the first result
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

WebSocketClient::WebSocketClient(const std::string &hostUrl)
    : m_hostUrl(hostUrl),
      m_tcpClient(resolveHost(hostUrl), [&](std::vector<uint8_t> &packets) {
        readDataHandle(packets);
      }) {}

WebSocketClient::~WebSocketClient() {}

void WebSocketClient::connect() { sendHandshake(); }

void WebSocketClient::sendHandshake() {
  // // Establish TCP Connection
  m_tcpClient.tcpConnect();

  // Send HTTP websocket upgrade handshake
  m_tcpClient.sendMessage(generateHandShakePayload());

  // validate response is ok
  // TODO - once connection is established client can send anything and receive
  // anything
}

std::string WebSocketClient::generateHandShakePayload() {
  std::ostringstream ss;

  ss << "GET /chat HTTP/1.1 " << "Host: " << m_hostUrl << " "
     << "Upgrade: websocket " << "Connection: Upgrade"
     << "Sec-WebSocket-Key: " << generateWebSocketKey() << " "
     << "Origin: " << PLACEHOLDER_ORIGIN
     << "Sec-WebSocket-Protocol: chat, superchat "
     << "Sec-WebSocket-Version: 13";

  return ss.str();
}

void WebSocketClient::readDataHandle(std::vector<uint8_t> &packets) {
  // Converts it to a webframe structure - TODO - still need to implement this
  m_webFrameSerializer.convertRawPacketsToWebframes(packets);

  // We can then have something passed by websocket client caller to get the
  // packets back
}

std::string WebSocketClient::generateWebSocketKey() {
  unsigned char random_bytes[16];
  RAND_bytes(random_bytes, sizeof(random_bytes));

  // Base64 encode
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

} // namespace websocklib
