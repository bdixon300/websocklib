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
#include <mutex>

#include <cstring>

namespace websocklib {

// Default Port used in the Web socket protocol
constexpr unsigned int DEFAULT_WS_PORT = 80;

// Theoretical limit is 64KB, but we will assume we follow MTU limit (1500 bytes)
constexpr unsigned int MAX_TCP_PACKET_PAYLOAD_SIZE = 65535;

// Max number of connection requests to listen to at a time
constexpr unsigned int MAX_BACKLOG = 5;

/**
 * 
 * TCP Server class. This class leverages the BSD socket library / linux sys calls
 * to establish a TCP server, allowing bidrectional communication (sending messages and receiving messages)
 * Class takes callbacks that can be used to asynchronously send/receive network packets.
 * 
 */
class TCPServer {
    public:
        TCPServer(const std::string& ipAddr,
                  const std::function<void(std::vector<uint8_t>& packetBuffer)>& packetProcessorCallback,
                  unsigned int portNumber = DEFAULT_WS_PORT);
        ~TCPServer();

        // Method to initiate TCP start
        void start();

        // Method to end TCP server
        void stop();

        // Method to send data to TCP server
        void sendMessage(const std::span<uint8_t>& msgPayload);

    private:
        /*
            Helper function that continuously loops on accept calls from the BSD socket library
            when an accept is received (ie a TCP client has sent a connection request)
            we fork a new process for the given connection and continue to loop until the server is stopped.
        */
       void waitForConnection();

       /*
            Helper function that will be called once a TCP connection is established,
            it will continously call receiveMessages and create a new thread to run 
            processMessages asynchronously
       */
       void handleClient();
    
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

        // Thread that will receive messages before being handed over for 
        // processing once TCP connection is established
        std::thread m_receivingThread;

        // Thread that will process messages once TCP connection is established
        std::thread m_processingThread;

        // Mutex to prevent race conditions on the packet buffer
        std::mutex m_packetMutex;

        // Packet buffer to hold packets as they are collected from server
        std::vector<uint8_t> m_packetBuffer;

        // Function callback to handle packets stored in packet buffer
        std::function<void(std::vector<uint8_t>& packetBuffer)> m_packetProcessorCallback;

        // Port number the TCP Socket will connect with
        unsigned int m_portNumber;

        // Private member containing file descripter to TCP socket once opened.
        int m_socketFd;

        // client socket fd - file descriptor for TCP socket of receiving client
        int m_clientSocketFd;

        // Thread safe boolean flag indicating a TCP server has started
        std::atomic<bool> m_started{false};

        // Thread safe boolean flag indicating a TCP connection has been established between a client and a server
        std::atomic<bool> m_connected{false};
};


} // namespace websocklib