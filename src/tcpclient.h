#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include "tcpcommon.h"

namespace websocklib {

class TCPClient {
public:
  // host: hostname or IPv4/IPv6 address literal. DNS resolution is deferred to
  // tcpConnect() so the constructor never blocks or throws on network errors.
  //
  // readIdleTimeout > 0 triggers disconnect when no bytes arrive within the
  // interval — use this for stall detection on financial data feeds.
  TCPClient(const std::string &host,
            const std::function<void(std::vector<uint8_t> &)> &packetProcessorCallback,
            unsigned int port = DEFAULT_WS_PORT,
            bool useTls = false,
            std::chrono::milliseconds readIdleTimeout = std::chrono::milliseconds(0),
            std::function<void()> disconnectCallback = nullptr);
  ~TCPClient();

  // Resolves DNS, iterates all returned addresses (IPv4 and IPv6), connects,
  // and if useTls=true completes the TLS handshake.  connectTimeout=0 means
  // the OS default; non-zero enforces an application-level connect deadline.
  void tcpConnect(std::chrono::milliseconds connectTimeout = std::chrono::milliseconds(0));
  void tcpDisconnect();

  // Thread-safe: acquires an internal mutex so concurrent callers cannot
  // interleave frame bytes.
  void sendMessage(std::span<const uint8_t> msgPayload);

  bool isConnected() const { return m_connected; }
  const std::string &host() const { return m_host; }

private:
  void    receiveMessages();
  int     connectWithTimeout(int fd, const sockaddr *addr, socklen_t addrLen,
                             std::chrono::milliseconds timeout);
  static SSL_CTX *createSslCtx();
  static std::string sslErrors();
  ssize_t sslRead(uint8_t *buf, size_t len);

  std::string m_host;
  unsigned int m_port;
  bool m_useTls;
  std::chrono::milliseconds m_readIdleTimeout;
  std::function<void(std::vector<uint8_t> &)> m_packetProcessorCallback;
  std::function<void()> m_disconnectCallback;

  std::vector<uint8_t> m_packetBuffer;
  std::thread m_receivingThread;

  std::atomic<int>  m_socketFd{-1};
  std::atomic<bool> m_connected{false};
  int m_cancelPipe[2] = {-1, -1};

  SSL_CTX *m_sslCtx{nullptr};
  SSL     *m_ssl{nullptr};
  mutable std::mutex m_sendMtx;
};

} // namespace websocklib
