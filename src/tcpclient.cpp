#include "tcpclient.h"

namespace websocklib {

TCPClient::TCPClient(
    const std::string& ipAddr,
    const std::function<void(std::vector<uint8_t>&)>& cb,
    unsigned int portNumber)
    : m_ipAddr(ipAddr), m_portNumber(portNumber), m_packetProcessorCallback(cb) {}

void TCPClient::tcpConnect()
{
    if (pipe(m_cancelPipe) < 0)
        throw std::runtime_error("pipe() failed");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        throw std::runtime_error("socket() failed");
    m_socketFd = fd;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(m_portNumber);
    if (inet_pton(AF_INET, m_ipAddr.c_str(), &addr.sin_addr) <= 0)
        throw std::runtime_error("Invalid IP address");

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        throw std::runtime_error("connect() failed");

    m_connected = true;
    m_packetBuffer.reserve(MAX_TCP_PACKET_PAYLOAD_SIZE);
    m_receivingThread = std::thread([this]{ receiveMessages(); });
}

void TCPClient::tcpDisconnect()
{
    if (m_connected.exchange(false)) {
        // Wake the poll() in receiveMessages
        uint8_t b = 1;
        write(m_cancelPipe[1], &b, 1);
        int fd = m_socketFd.exchange(-1);
        if (fd >= 0) close(fd);
    }
    if (m_receivingThread.joinable()) m_receivingThread.join();

    if (m_cancelPipe[0] >= 0) { close(m_cancelPipe[0]); m_cancelPipe[0] = -1; }
    if (m_cancelPipe[1] >= 0) { close(m_cancelPipe[1]); m_cancelPipe[1] = -1; }
}

TCPClient::~TCPClient()
{
    tcpDisconnect();
}

void TCPClient::sendMessage(const std::span<uint8_t>& msgPayload)
{
    const uint8_t* ptr = msgPayload.data();
    size_t remaining   = msgPayload.size_bytes();
    while (remaining > 0) {
        ssize_t sent = write(m_socketFd, ptr, remaining);
        if (sent < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("write() failed: " + std::string(strerror(errno)));
        }
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
}

void TCPClient::receiveMessages()
{
    uint8_t buf[4096];
    while (m_connected) {
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

        int n = read(m_socketFd, buf, sizeof(buf));
        if (n <= 0) break; // EOF or error

        m_packetBuffer.insert(m_packetBuffer.end(), buf, buf + n);
        m_packetProcessorCallback(m_packetBuffer);
    }
    m_connected = false;
}

} // namespace websocklib
