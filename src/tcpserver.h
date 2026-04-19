#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

#include <string>
#include <vector>
#include <stdexcept>
#include <atomic>
#include <thread>
#include <span>
#include <functional>
#include <cerrno>
#include <cstring>

#include "tcpcommon.h"

namespace websocklib {

// Minimal TCP Server implementation
// Not designed to scale, mainly for testing TCP client/websocket client implementation 
// (only handles one client connection at a time)
class TCPServer {
    public:
        TCPServer(const std::string& ipAddr,
                  const std::function<void(std::vector<uint8_t>&)>& packetProcessorCallback,
                  unsigned int portNumber = DEFAULT_WS_PORT);
        ~TCPServer();

        void start();
        void stop();
        void sendMessage(const std::span<uint8_t>& msgPayload);
        bool isClientConnected() const { return m_connected; }

    private:
        void waitForConnection();
        void handleClient();
        void receiveMessages();

        // Ip address of the server
        std::string m_ipAddr;

        // Port number to bind server socket to
        unsigned int m_portNumber;

        // callback to process packets received from client
        std::function<void(std::vector<uint8_t>&)> m_packetProcessorCallback;

        // Packet buffer
        std::vector<uint8_t> m_packetBuffer;

        // Thread to prevent blocking on server. When accept moves on a client has attempted to establish a connection
        std::thread m_acceptThread;

        // Server Socket FD
        std::atomic<int> m_socketFd{-1};

        // Client Side Socket FD (once a connection is established)
        std::atomic<int> m_clientSocketFd{-1};

        // Bool flag to indicate a TCP Server has started and is waiting on a connection via accept() in a separate thread
        std::atomic<bool> m_started{false};
        
        // Bool flag to indicate a TCP Server has connected to a Client
        std::atomic<bool> m_connected{false};

        // Pipe for terminating connection/TCP server (read/recv calls are blocking till client sends data, this allows for immediate termination)
        int m_cancelPipe[2] = {-1, -1};
};

} // namespace websocklib
