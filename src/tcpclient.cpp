#include "tcpclient.h"

namespace websocklib {

TCPClient::TCPClient(const std::string& ipAddr, unsigned int portNumber): 
    m_ipAddr(ipAddr), m_portNumber(portNumber) {}

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
    // TODO - Not yet implemented
    // use the write() sys call
    /**
     * You need to send this in raw text:
     * 
     * GET /chat HTTP/1.1\r\n
        Host: example.com\r\n
        Upgrade: websocket\r\n
        Connection: Upgrade\r\n
        Sec-WebSocket-Key: <base64 encoded 16 random bytes>\r\n
        Sec-WebSocket-Version: 13\r\n
        \r\n
    
        Once done, you should receive a response as follows ->

        HTTP/1.1 101 Switching Protocols\r\n
        Upgrade: websocket\r\n
        Connection: Upgrade\r\n
        Sec-WebSocket-Accept: <base64 encoded SHA-1 hash>\r\n
        \r\n
     * 
     */
    (void)msgPayload;
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
                throw std::runtime_error("Unexpected Error when reading bytes from TCP socket!");
            }
        }

        // Server side closed connection, ending stream
        if (numBytesRead == 0)
        {
            // TODO - handle this gracefully, send back some ACK etc
            break;
        }

        // Extract information from data
        m_packetBuffer.insert(m_packetBuffer.begin(), tmpBuffer, tmpBuffer + numBytesRead);
    }
}

void TCPClient::processMessages()
{
    while (m_connected)
    {
        // TODO busy polls the packet buffer data till we have something to decode??
        // read from m_packetBuffer and parse the data

        // Only once packet buffer is collected everything 
        m_packetProcessorCallback(m_packetBuffer);
    }
}

} // namespace websocklib