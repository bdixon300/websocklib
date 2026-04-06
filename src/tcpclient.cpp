#include "tcpclient.h"

namespace websocklib {

TCPClient::TCPClient(
    const std::string& ipAddr,
    const std::function<void(std::vector<uint8_t>& packetBuffer)>& packetProcessorCallback,
    unsigned int portNumber): 
    m_ipAddr(ipAddr), m_packetProcessorCallback(packetProcessorCallback), m_portNumber(portNumber) {}

void TCPClient::tcpConnect()
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
    serverAddr.sin_port = htons(m_portNumber);

    // Convert IP address from text to binary form, binding it to sin_addr property
    if (inet_pton(AF_INET, m_ipAddr.c_str(), &serverAddr.sin_addr) <= 0)
    {
        throw std::runtime_error("Invalid IP Address ERROR!");
    }

    // Establish Connection to the given server
    if (connect(m_socketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
    {
        throw std::runtime_error("Failed to establish connection ERROR!");
    }

    // TCP Client now connected
    m_connected = true;
    
    // Preallocate packet buffer to max possible TCP payload size we could receive
    m_packetBuffer.reserve(MAX_TCP_PACKET_PAYLOAD_SIZE);

    // Launch Thread to asynchronously process packets as they are received
    m_processingThread = std::thread([&]() {
        processMessages();
    });

    // Launch Thread to asynchronously receive packets
    m_receivingThread = std::thread([&]() {
        receiveMessages();
    });
}

void TCPClient::tcpDisconnect()
{
    if (m_connected)
    {
        // Disconnecting TCP socket
        m_connected = false;

        // Join on thread receiving packets (should break out of loop as m_connected is now false)
        m_processingThread.join();

        // Join on thread processing packets
        m_receivingThread.join();

        // Close TCP socket
        close(m_socketFd); // TODO- what happens if m_socketFd is unassigned
    }
}


TCPClient::~TCPClient()
{
    if (m_connected)
    {
        // Disconnecting TCP socket
        m_connected = false;

        // Join on thread receiving packets (should break out of loop as m_connected is now false)
        m_processingThread.join();

        // Close TCP socket
        close(m_socketFd); // TODO- what happens if m_socketFd is unassigned
    }
}


void TCPClient::sendMessage(const std::span<uint8_t>& msgPayload)
{
    int numBytesSent = write(m_socketFd, msgPayload.data(), msgPayload.size_bytes());
    if (numBytesSent < 0)
    {
        throw std::runtime_error("Unexpected Error when Sending bytes to TCP socket/TCP server!");
    }
}


void TCPClient::receiveMessages()
{
    // temporary buffer to copy packets to
    uint8_t tmpBuffer[4096];

    while (m_connected)
    {
        // read packets from tcp read syscall
        // and do something with them
        int numBytesRead = read(m_socketFd, &tmpBuffer, sizeof(tmpBuffer));

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
            // TODO - handle this gracefully, send back some ACK etc
            break;
        }

        // Extract information from data
        std::lock_guard<std::mutex> lock(m_packetMutex);
        m_packetBuffer.insert(m_packetBuffer.begin(), tmpBuffer, tmpBuffer + numBytesRead);
    }
}

void TCPClient::processMessages()
{
    while (m_connected)
    {
        std::lock_guard<std::mutex> lock(m_packetMutex);
        m_packetProcessorCallback(m_packetBuffer);
    }
}

} // namespace websocklib