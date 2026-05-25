#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "tcpcommon.h"

namespace websocklib {

class TCPClient {
public:
  TCPClient(const std::string &ipAddr,
            const std::function<void(std::vector<uint8_t> &)>
                &packetProcessorCallback,
            unsigned int portNumber = DEFAULT_WS_PORT,
            std::function<void()> disconnectCallback = nullptr);
  ~TCPClient();

  void tcpConnect();
  void tcpDisconnect();
  void sendMessage(const std::span<uint8_t> &msgPayload);
  bool isConnected() const { return m_connected; }

private:
  void receiveMessages();

  // IP Address to TCP server
  std::string m_ipAddr;

  // Port number to TCP Server
  unsigned int m_portNumber;

  // Callback when handling data received from TCP server
  std::function<void(std::vector<uint8_t> &)> m_packetProcessorCallback;

  // Callback invoked once when the receive loop exits (server close or error)
  std::function<void()> m_disconnectCallback;

  // Intermediate storage of packets received from TCP Server
  std::vector<uint8_t> m_packetBuffer;

  // Thread to handle receiving packets asynchronously (so read/recv calls do
  // not block)
  std::thread m_receivingThread;

  // Socket FD for client
  std::atomic<int> m_socketFd{-1};

  // Bool flag to indicate a TCP connection has been established to a TCP Server
  std::atomic<bool> m_connected{false};

  // Pipe for terminating connection/TCP server (read/recv calls are blocking
  // till client sends data, this allows for immediate termination)
  int m_cancelPipe[2] = {-1, -1};
};

} // namespace websocklib
