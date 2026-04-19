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

class TCPClient {
    public:
        TCPClient(const std::string& ipAddr,
                  const std::function<void(std::vector<uint8_t>&)>& packetProcessorCallback,
                  unsigned int portNumber = DEFAULT_WS_PORT);
        ~TCPClient();

        void tcpConnect();
        void tcpDisconnect();
        void sendMessage(const std::span<uint8_t>& msgPayload);
        bool isConnected() const { return m_connected; }

    private:
        void receiveMessages();

        // IP Address to TCP server
        std::string m_ipAddr;

        // Port number to TCP Server
        unsigned int m_portNumber;

        // Callback when handling data received from TCP server
        std::function<void(std::vector<uint8_t>&)> m_packetProcessorCallback;

        // Intermediate storage of packets received from TCP Server
        std::vector<uint8_t> m_packetBuffer;

        // Thread to handle receiving packets asynchronously (so read/recv calls do not block)
        std::thread m_receivingThread;

        // Socket FD for client
        std::atomic<int> m_socketFd{-1};

        // Bool flag to indicate a TCP connection has been established to a TCP Server
        std::atomic<bool> m_connected{false};

        // Pipe for terminating connection/TCP server (read/recv calls are blocking till client sends data, this allows for immediate termination)
        int m_cancelPipe[2] = {-1, -1};
};

} // namespace websocklib
