#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>


#include <string>
#include <vector>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <span>
#include <functional>

#include <cstring>

namespace websocklib {

// Default Port used in the Web socket protocol
constexpr unsigned int DEFAULT_WS_PORT = 80;

// Theoretical limit is 64KB, but we will assume we follow MTU limit (1500 bytes)
constexpr unsigned int MAX_TCP_PACKET_PAYLOAD_SIZE = 65535;

/**
 * 
 * TCP Client class. This class leverages the BSD socket library / linux sys calls
 * to connect to a TCP server, allowing bidrectional communication (sending messages and receiving messages)
 * Class takes callbacks that can be used to asynchronously send/receive network packets.
 * 
 */
class TCPClient {
    public:
        TCPClient(const std::string& ipAddr,
                  const std::function<void(std::vector<uint8_t>& packetBuffer)>& packetProcessorCallback,
                  unsigned int portNumber = DEFAULT_WS_PORT);
        ~TCPClient();

        // Method to initiate TCP connection
        void tcpConnect();

        // Method to terminate TCP connection
        void tcpDisconnect();

        // Method to send data to TCP server
        void sendMessage(const std::span<uint8_t>& msgPayload);

    private:
        /* 
            Helper function that runs in a dedicated thread to receive packets 
            asynchronously.
        */
        void receiveMessages();

        /*
            Helper function to process packets that have been collected in the packet
            buffer
        */
        void processMessages();

        // IP Address the TCP Socket will connect with (in string format)
        std::string m_ipAddr;
        
        // Port number the TCP Socket will connect with
        unsigned int m_portNumber;

        // Private member containing file descripter to TCP socket once opened.
        int m_socketFd;

        // Thread safe boolean flag indicating a connection is established
        std::atomic<bool> m_connected{false};

        // Thread that will receive messages before being handed over for 
        // processing once TCP connection is established
        std::thread m_receivingThread;

        // Thread that will process messages once TCP connection is established
        std::thread m_processingThread;

        // Packet buffer to hold packets as they are collected from server
        std::vector<uint8_t> m_packetBuffer;

        // Function callback to handle packets stored in packet buffer
        std::function<void(std::vector<uint8_t>& packetBuffer)> m_packetProcessorCallback;
};


} // namespace websocklib