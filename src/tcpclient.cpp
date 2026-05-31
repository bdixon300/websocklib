#include "tcpclient.h"

#include <fcntl.h>

namespace websocklib {

TCPClient::TCPClient(const std::string &host,
                     const std::function<void(std::vector<uint8_t> &)> &cb,
                     unsigned int port, bool useTls,
                     std::chrono::milliseconds readIdleTimeout,
                     std::function<void()> disconnectCallback)
    : m_host(host), m_port(port), m_useTls(useTls),
      m_readIdleTimeout(readIdleTimeout), m_packetProcessorCallback(cb),
      m_disconnectCallback(std::move(disconnectCallback)) {
  if (m_useTls)
    m_sslCtx = createSslCtx();
}

SSL_CTX *TCPClient::createSslCtx() {
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
  if (!ctx)
    throw std::runtime_error("SSL_CTX_new failed: " + sslErrors());
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
  if (!SSL_CTX_set_default_verify_paths(ctx)) {
    SSL_CTX_free(ctx);
    throw std::runtime_error("SSL_CTX_set_default_verify_paths failed: " + sslErrors());
  }
  return ctx;
}

std::string TCPClient::sslErrors() {
  std::string out;
  unsigned long e;
  char buf[256];
  while ((e = ERR_get_error()) != 0) {
    ERR_error_string_n(e, buf, sizeof(buf));
    if (!out.empty()) out += "; ";
    out += buf;
  }
  return out;
}

int TCPClient::connectWithTimeout(int fd, const sockaddr *addr, socklen_t addrLen,
                                  std::chrono::milliseconds timeout) {
  if (timeout.count() == 0)
    return ::connect(fd, addr, addrLen);

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return -1;

  int r = ::connect(fd, addr, addrLen);
  if (r == 0) {
    fcntl(fd, F_SETFL, flags);
    return 0;
  }
  if (errno != EINPROGRESS) {
    fcntl(fd, F_SETFL, flags);
    return -1;
  }

  pollfd pfd{fd, POLLOUT, 0};
  int n = poll(&pfd, 1, static_cast<int>(timeout.count()));
  fcntl(fd, F_SETFL, flags);

  if (n <= 0) {
    if (n == 0) errno = ETIMEDOUT;
    return -1;
  }
  int err = 0;
  socklen_t len = sizeof(err);
  getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
  if (err) { errno = err; return -1; }
  return 0;
}

void TCPClient::tcpConnect(std::chrono::milliseconds connectTimeout) {
  if (m_connected)
    throw std::runtime_error("tcpConnect() called on an already-connected client");

  // Recreate cancel pipe — supports clean reconnect after tcpDisconnect().
  if (m_cancelPipe[0] >= 0) { close(m_cancelPipe[0]); m_cancelPipe[0] = -1; }
  if (m_cancelPipe[1] >= 0) { close(m_cancelPipe[1]); m_cancelPipe[1] = -1; }
  if (pipe(m_cancelPipe) < 0)
    throw std::runtime_error("pipe() failed: " + std::string(strerror(errno)));

  // Resolve host and attempt all returned addresses (IPv4 and IPv6).
  addrinfo hints{};
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *results = nullptr;
  int gaiErr = getaddrinfo(m_host.c_str(), std::to_string(m_port).c_str(),
                           &hints, &results);
  if (gaiErr != 0) {
    close(m_cancelPipe[0]); m_cancelPipe[0] = -1;
    close(m_cancelPipe[1]); m_cancelPipe[1] = -1;
    throw std::runtime_error("getaddrinfo(\"" + m_host + "\"): " +
                             std::string(gai_strerror(gaiErr)));
  }

  int fd = -1;
  int lastErrno = 0;
  for (addrinfo *p = results; p != nullptr; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) { lastErrno = errno; continue; }

    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    if (connectWithTimeout(fd, p->ai_addr, p->ai_addrlen, connectTimeout) == 0)
      break;

    lastErrno = errno;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(results);

  if (fd < 0) {
    close(m_cancelPipe[0]); m_cancelPipe[0] = -1;
    close(m_cancelPipe[1]); m_cancelPipe[1] = -1;
    throw std::runtime_error("connect() to \"" + m_host + "\" failed: " +
                             std::string(strerror(lastErrno)));
  }
  m_socketFd = fd;

  if (m_useTls) {
    SSL *ssl = SSL_new(m_sslCtx);
    if (!ssl) {
      close(fd); m_socketFd = -1;
      throw std::runtime_error("SSL_new failed: " + sslErrors());
    }
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, m_host.c_str());  // SNI
    SSL_set1_host(ssl, m_host.c_str());              // hostname verification

    if (SSL_connect(ssl) != 1) {
      std::string e = sslErrors();
      SSL_free(ssl);
      close(fd); m_socketFd = -1;
      throw std::runtime_error("TLS handshake failed: " + e);
    }
    m_ssl = ssl;
  }

  m_connected = true;
  m_packetBuffer.reserve(MAX_TCP_PACKET_PAYLOAD_SIZE);
  m_receivingThread = std::thread([this] { receiveMessages(); });
}

void TCPClient::tcpDisconnect() {
  if (m_connected.exchange(false)) {
    uint8_t b = 1;
    write(m_cancelPipe[1], &b, 1);
    int fd = m_socketFd.exchange(-1);
    if (fd >= 0) close(fd);  // interrupts any blocking SSL_read / read
  }
  if (m_receivingThread.joinable())
    m_receivingThread.join();  // m_ssl is exclusively ours past this point

  if (m_ssl) { SSL_free(m_ssl); m_ssl = nullptr; }

  if (m_cancelPipe[0] >= 0) { close(m_cancelPipe[0]); m_cancelPipe[0] = -1; }
  if (m_cancelPipe[1] >= 0) { close(m_cancelPipe[1]); m_cancelPipe[1] = -1; }
}

TCPClient::~TCPClient() {
  tcpDisconnect();
  if (m_sslCtx) { SSL_CTX_free(m_sslCtx); m_sslCtx = nullptr; }
}

ssize_t TCPClient::sslRead(uint8_t *buf, size_t len) {
  int n = SSL_read(m_ssl, buf, static_cast<int>(len));
  if (n > 0) return n;
  int sslErr = SSL_get_error(m_ssl, n);
  if (sslErr == SSL_ERROR_WANT_READ || sslErr == SSL_ERROR_WANT_WRITE)
    return 0;  // transient; caller retries via poll
  return -1;
}

void TCPClient::sendMessage(std::span<const uint8_t> msgPayload) {
  std::lock_guard<std::mutex> lk(m_sendMtx);
  const uint8_t *ptr = msgPayload.data();
  size_t remaining   = msgPayload.size();

  while (remaining > 0) {
    ssize_t sent;
    if (m_useTls && m_ssl) {
      int w = SSL_write(m_ssl, ptr, static_cast<int>(remaining));
      if (w <= 0) {
        int sslErr = SSL_get_error(m_ssl, w);
        if (sslErr == SSL_ERROR_WANT_WRITE) { sent = 0; continue; }
        throw std::runtime_error("SSL_write failed: " + sslErrors());
      }
      sent = w;
    } else {
      sent = write(m_socketFd, ptr, remaining);
      if (sent < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("write() failed: " + std::string(strerror(errno)));
      }
    }
    ptr += sent;
    remaining -= static_cast<size_t>(sent);
  }
}

void TCPClient::receiveMessages() {
  uint8_t buf[65536];
  const int timeoutMs = (m_readIdleTimeout.count() > 0)
                            ? static_cast<int>(m_readIdleTimeout.count())
                            : -1;

  while (m_connected) {
    // Skip poll when OpenSSL already has decrypted bytes buffered internally
    // (possible when a single TCP segment carried multiple TLS records).
    const bool hasPending = m_useTls && m_ssl && SSL_pending(m_ssl) > 0;
    if (!hasPending) {
      pollfd fds[2];
      fds[0] = {m_socketFd.load(), POLLIN, 0};
      fds[1] = {m_cancelPipe[0],   POLLIN, 0};

      int r = poll(fds, 2, timeoutMs);
      if (r < 0) {
        if (errno == EINTR) continue;
        break;
      }
      if (r == 0) break;  // idle timeout — stale connection

      if (fds[1].revents & POLLIN) break;
      if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
      if (!(fds[0].revents & POLLIN)) continue;
    }

    ssize_t n;
    if (m_useTls && m_ssl) {
      n = sslRead(buf, sizeof(buf));
      if (n == 0) continue;  // SSL_ERROR_WANT_READ — retry via poll
    } else {
      n = read(m_socketFd, buf, sizeof(buf));
    }
    if (n <= 0) break;

    m_packetBuffer.insert(m_packetBuffer.end(), buf, buf + n);
    m_packetProcessorCallback(m_packetBuffer);
  }
  m_connected = false;
  if (m_disconnectCallback) m_disconnectCallback();
}

} // namespace websocklib
