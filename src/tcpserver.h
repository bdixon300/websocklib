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

// Minimal single-client TCP server for integration tests only.
// Not compiled into libwebsocklib; lives in the test build target.
class TCPServer {
public:
  TCPServer(const std::string &ipAddr,
            const std::function<void(std::vector<uint8_t> &)>
                &packetProcessorCallback,
            unsigned int portNumber = DEFAULT_WS_PORT);
  ~TCPServer();

  void start();
  void stop();
  void sendMessage(std::span<const uint8_t> msgPayload);
  bool isClientConnected() const { return m_connected; }

private:
  void waitForConnection();
  void handleClient();
  void receiveMessages();

  std::string m_ipAddr;
  unsigned int m_portNumber;
  std::function<void(std::vector<uint8_t> &)> m_packetProcessorCallback;

  std::vector<uint8_t> m_packetBuffer;
  std::thread m_acceptThread;

  std::atomic<int>  m_socketFd{-1};
  std::atomic<int>  m_clientSocketFd{-1};
  std::atomic<bool> m_started{false};
  std::atomic<bool> m_connected{false};

  int m_cancelPipe[2] = {-1, -1};
};

} // namespace websocklib
