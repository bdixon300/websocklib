#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include "tcpclient.h"
#include "webframeserializer.h"

namespace websocklib {

class WebSocketClient {
public:
  WebSocketClient(const std::string &hostUrl,
                  unsigned int port = DEFAULT_WS_PORT);

  ~WebSocketClient();

  // Establish websocket connection: starts TCP connection, sends HTTP upgrade
  // handshake, blocks until server confirms with 101 or throws on failure.
  void connect();

  // Send a text or binary WebSocket frame. Throws if not connected.
  void send(const std::string &message);
  void send(const std::vector<uint8_t> &message);

  // Initiate a clean close: sends a close frame then tears down the TCP connection.
  void disconnect();

  // Register callback invoked when a text message is received.
  void onMessage(std::function<void(const std::string &)> cb);

  // Register callback invoked when the connection is closed by the remote end.
  // Not fired for user-initiated disconnect().
  void onClose(std::function<void()> cb);

  bool isWebSocketConnected() const;

private:
  enum class ConnectionState {
    Disconnected,
    AwaitingHandshakeResponse,
    Connected
  };

  std::string generateHandShakePayload();
  std::string generateWebSocketKey();
  void sendHandshake();
  void readDataHandle(std::vector<uint8_t> &packets);
  std::string computeExpectedAcceptKey(const std::string &key);
  void parseHandshakeResponse(const std::string &response);

  std::string m_hostUrl;
  unsigned int m_port;
  std::string m_webSocketKey;
  std::string m_httpBuffer;
  ConnectionState m_connectionState{ConnectionState::Disconnected};
  std::function<void(const std::string &)> m_messageCallback;
  std::mutex m_handshakeMtx;
  std::condition_variable m_handshakeCv;
  std::exception_ptr m_handshakeException;
  WebFrameSerializer m_webFrameSerializer;
  // Declared before m_tcpClient so they outlive the receive thread.
  std::atomic<bool> m_wsConnected{false};
  std::function<void()> m_closeCallback;
  TCPClient m_tcpClient;
};

} // namespace websocklib
