#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tcpclient.h"
#include "webframeserializer.h"

namespace websocklib {

class WebSocketClient {
public:
  // host:    hostname or IP address.
  // port:    defaults to 80 (use 443 for wss://).
  // path:    HTTP request path (e.g. "/ws/v2/stream").
  // useTls:  set true for wss:// connections.
  WebSocketClient(const std::string &host,
                  unsigned int port = DEFAULT_WS_PORT,
                  std::string path  = "/",
                  bool useTls       = false);

  ~WebSocketClient();

  // Establish the WebSocket connection: TCP connect, HTTP Upgrade handshake,
  // blocks until the server confirms with 101 or throws on failure.
  // timeout=0 means no application-level deadline (OS / TCP defaults apply).
  void connect(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

  // Send a text (opcode 0x01) or binary (opcode 0x02) frame.
  // Throws if not connected.  Both overloads are zero-copy for the caller:
  // they accept views/spans and avoid forcing a heap allocation at the call site.
  void send(std::string_view message);
  void send(std::span<const uint8_t> message);

  // Initiate a clean close: sends a WebSocket Close frame then tears down TCP.
  void disconnect();

  // Register callbacks — must be called before connect().
  void onMessage(std::function<void(const std::string &)> cb);
  void onBinaryMessage(std::function<void(const std::vector<uint8_t> &)> cb);

  // Fired when the connection is closed by the remote end.
  // Not fired for a user-initiated disconnect().
  void onClose(std::function<void()> cb);

  bool isWebSocketConnected() const;

private:
  enum class ConnectionState { Disconnected, AwaitingHandshakeResponse, Connected };

  std::string generateHandShakePayload();
  std::string generateWebSocketKey();
  void        sendHandshake(std::chrono::milliseconds connectTimeout);
  void        readDataHandle(std::vector<uint8_t> &packets);
  std::string computeExpectedAcceptKey(const std::string &key);
  void        parseHandshakeResponse(const std::string &response);

  std::string  m_hostUrl;
  unsigned int m_port;
  std::string  m_path;
  std::string  m_webSocketKey;
  std::string  m_httpBuffer;

  // Atomic so the receive thread and the main thread can both read it without
  // acquiring m_handshakeMtx on every message after the handshake completes.
  std::atomic<ConnectionState> m_connectionState{ConnectionState::Disconnected};

  std::function<void(const std::string &)>          m_messageCallback;
  std::function<void(const std::vector<uint8_t> &)> m_binaryCallback;
  std::mutex              m_handshakeMtx;
  std::condition_variable m_handshakeCv;
  std::exception_ptr      m_handshakeException;

  // Declared before m_tcpClient so all captured members outlive the receive
  // thread (~TCPClient joins the thread before anything else is destroyed).
  WebFrameSerializer      m_webFrameSerializer;
  std::atomic<bool>       m_wsConnected{false};
  std::function<void()>   m_closeCallback;
  TCPClient               m_tcpClient;
};

} // namespace websocklib
