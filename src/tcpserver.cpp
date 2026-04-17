#include "tcpserver.h"

namespace websocklib {

TCPServer::TCPServer(
    const std::string& ipAddr,
    const std::function<void(std::vector<uint8_t>& packetBuffer)>& packetProcessorCallback,
    unsigned int portNumber): 
    m_ipAddr(ipAddr), m_packetProcessorCallback(packetProcessorCallback), m_portNumber(portNumber) {}

void TCPServer::start()
{
    // Create file descriptor/socket (SOCK_STREAM is TCP, AF_INET is IPV4)
    m_socketFd = socket(AF_INET, SOCK_STREAM, 0);

    if (m_socketFd < 0)
    {
        throw std::runtime_error("TCP Socket creation ERROR!");
    }

    // Initialise server side information
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(m_portNumber);

    if (bind(m_socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        throw std::runtime_error("Error binding socket to local address");
    }

    if (listen(m_socketFd, SOMAXCONN) < 0)
    {
        throw std::runtime_error("Error market socket as a passive listener for accept() calls");
    }

    // TCP Server now started
    m_started = true;

    // loop on blocking accept, starting new process for every client connection request
    waitForConnection();
}

void TCPServer::stop()
{
    if (m_connected)
    {
        // Disconnecting TCP socket
        m_connected = false;

        // Join on thread receiving packets (should break out of loop as m_connected is now false)
        m_processingThread.join();

        // Close TCP sockets
        close(m_clientSocketFd);

        close(m_socketFd); // TODO- what happens if m_socketFd is unassigned
    }
}


TCPServer::~TCPServer()
{
    if (m_connected)
    {
        // Disconnecting TCP socket
        m_connected = false;

        // Stopping TCP Server
        m_started = false;

        // Close TCP socket
        close(m_socketFd); // TODO- what happens if m_socketFd is unassigned
    }
}

void TCPServer::waitForConnection()
{
    while (m_started)
    {
        // Potential client socket to listen to
        struct sockaddr_in incomingClientAddr;
        socklen_t clientAddrSize = sizeof(incomingClientAddr);
        
        m_clientSocketFd = accept(m_socketFd, (struct sockaddr *)&incomingClientAddr, &clientAddrSize);
        if (m_clientSocketFd < 0)
        {
            throw std::runtime_error("Failed to accept request from client!");
        }

        // connection established with a client
        m_connected = true;

        // fork new process from here - TCP connection established, send/receives will occur
        // fork() clones the entire process: page tables, file descriptor table,
        pid_t pid = fork();
        if (pid < 0) {
            close(m_clientSocketFd);
            continue;
        }

        if (pid == 0)
        {
            // CHILD process: close the inherited server socket fd — child doesn't accept.
            // If we forget this, m_socketFd leaks and the socket won't close
            // when the parent closes it (ref count stays > 0).
            close(m_socketFd);
            handleClient();
            _exit(0);
        }

        // Close client socket when done in parent
        close(m_clientSocketFd);
    }
}

void TCPServer::handleClient()
{
    // Once we accept a new connection we fork the process and loop back to a waiting state
    // Preallocate packet buffer to max possible TCP payload size we could receive
    m_packetBuffer.reserve(MAX_TCP_PACKET_PAYLOAD_SIZE);

    // Launch Thread to asynchronously process packets as they are received
    m_processingThread = std::thread([&]() {
        processMessages();
    });

    receiveMessages();

    m_connected = false;
    m_processingThread.join();

    // Either an error has occurred or n == 0 which mean CLIENT sent a FIN (and closed connection) and we should close this
    close(m_clientSocketFd);
}


void TCPServer::sendMessage(const std::span<uint8_t>& msgPayload)
{
    if (m_connected)
    {

        const uint8_t* ptr       = msgPayload.data();
        size_t         remaining = msgPayload.size_bytes();

        while (remaining > 0)
        {
            ssize_t sent = write(m_clientSocketFd, ptr, remaining);
            if (sent < 0)
            {
                if (errno == EINTR) continue;
                throw std::runtime_error("write() failed: " + std::string(strerror(errno)));
            }
            ptr       += sent;
            remaining -= sent;
        }
    }
    else
    {
        throw std::runtime_error("No open TCP connection at this time with a client");
    }
}


void TCPServer::receiveMessages()
{
    // temporary buffer to copy packets to
    uint8_t tmpBuffer[4096];

    while (m_connected)
    {
        // read packets from tcp read syscall
        // and do something with them
        int numBytesRead = read(m_clientSocketFd, &tmpBuffer, sizeof(tmpBuffer));

        // Error from sys call
        if (numBytesRead < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Do nothing this just means there was no data in the socket to read
            }
            else
            {
                throw std::runtime_error("Unexpected Error when reading bytes from TCP socket/TCP server!");
            }
        }

        // Server side closed connection, ending stream
        if (numBytesRead == 0)
        {
            // TODO - handle this gracefully
            break;
        }

        // Extract information from data
        std::lock_guard<std::mutex> lock(m_packetMutex);
        m_packetBuffer.insert(m_packetBuffer.end(), tmpBuffer, tmpBuffer + numBytesRead);
    }
}

void TCPServer::processMessages()
{
    while (m_connected)
    {
        // TODO - high contention here on the buffer
        std::lock_guard<std::mutex> lock(m_packetMutex);
        m_packetProcessorCallback(m_packetBuffer);
    }
}

} // namespace websocklib