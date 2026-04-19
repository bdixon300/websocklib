#include "tcpserver.h"

namespace websocklib {

TCPServer::TCPServer(
    const std::string& ipAddr,
    const std::function<void(std::vector<uint8_t>&)>& cb,
    unsigned int portNumber)
    : m_ipAddr(ipAddr), m_portNumber(portNumber), m_packetProcessorCallback(cb) {}

void TCPServer::start()
{
    if (pipe(m_cancelPipe) < 0)
        throw std::runtime_error("pipe() failed");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw std::runtime_error("socket() failed");
    m_socketFd = fd;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(m_portNumber);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed");
    if (listen(fd, SOMAXCONN) < 0)
        throw std::runtime_error("listen() failed");

    m_started = true;
    m_acceptThread = std::thread([this]{ waitForConnection(); });
}

void TCPServer::stop()
{
    m_started   = false;
    m_connected = false;

    // Wake any poll() call in the accept/receive loop
    if (m_cancelPipe[1] >= 0) {
        uint8_t b = 1;
        write(m_cancelPipe[1], &b, 1);
    }

    int clientFd = m_clientSocketFd.exchange(-1);
    if (clientFd >= 0) close(clientFd);

    int serverFd = m_socketFd.exchange(-1);
    if (serverFd >= 0) close(serverFd);

    if (m_acceptThread.joinable()) m_acceptThread.join();

    if (m_cancelPipe[0] >= 0) { close(m_cancelPipe[0]); m_cancelPipe[0] = -1; }
    if (m_cancelPipe[1] >= 0) { close(m_cancelPipe[1]); m_cancelPipe[1] = -1; }
}

TCPServer::~TCPServer()
{
    stop();
}

void TCPServer::waitForConnection()
{
    while (m_started) {
        struct pollfd fds[2];
        fds[0] = {m_socketFd.load(), POLLIN, 0};
        fds[1] = {m_cancelPipe[0],   POLLIN, 0};

        int r = poll(fds, 2, -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[1].revents & POLLIN) break; // cancel pipe written
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (!(fds[0].revents & POLLIN)) continue;

        struct sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int fd = accept(m_socketFd, (struct sockaddr*)&clientAddr, &len);
        if (fd < 0) continue;

        m_clientSocketFd = fd;
        m_connected = true;
        handleClient();
    }
}

void TCPServer::handleClient()
{
    m_packetBuffer.clear();
    m_packetBuffer.reserve(MAX_TCP_PACKET_PAYLOAD_SIZE);

    receiveMessages();

    m_connected = false;
    int fd = m_clientSocketFd.exchange(-1);
    if (fd >= 0) close(fd);
}

void TCPServer::sendMessage(const std::span<uint8_t>& msgPayload)
{
    if (!m_connected)
        throw std::runtime_error("No client connected");

    const uint8_t* ptr = msgPayload.data();
    size_t remaining = msgPayload.size_bytes();
    while (remaining > 0) {
        ssize_t sent = write(m_clientSocketFd, ptr, remaining);
        if (sent < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("write() failed: " + std::string(strerror(errno)));
        }
        ptr       += sent;
        remaining -= static_cast<size_t>(sent);
    }
}

void TCPServer::receiveMessages()
{
    uint8_t buf[4096];
    while (m_connected) {
        struct pollfd fds[2];
        fds[0] = {m_clientSocketFd.load(), POLLIN, 0};
        fds[1] = {m_cancelPipe[0],         POLLIN, 0};

        int r = poll(fds, 2, -1);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[1].revents & POLLIN) break; // cancel pipe written
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (!(fds[0].revents & POLLIN)) continue;

        int n = read(m_clientSocketFd, buf, sizeof(buf));
        if (n <= 0) break; // EOF or error

        m_packetBuffer.insert(m_packetBuffer.end(), buf, buf + n);
        m_packetProcessorCallback(m_packetBuffer);
    }
}

} // namespace websocklib
