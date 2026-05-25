#include <gtest/gtest.h>
#include "websocketclient.hpp"
#include "tcpserver.h"

#include <openssl/bio.h>
#include <openssl/evp.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace websocklib;
using namespace std::chrono_literals;

static constexpr uint16_t WS_TEST_PORT = 19879;

// ---- Local helpers ---------------------------------------------------------

template <typename Pred>
static bool waitFor(Pred pred, std::chrono::milliseconds timeout = 1000ms) {
  auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred()) {
    if (std::chrono::steady_clock::now() >= deadline)
      return false;
    std::this_thread::sleep_for(5ms);
  }
  return true;
}

// Extract value of a named HTTP header from a request/response string.
static std::string extractHeader(const std::string &msg,
                                 const std::string &name) {
  std::string search = name + ": ";
  auto pos = msg.find(search);
  if (pos == std::string::npos)
    return "";
  pos += search.size();
  auto end = msg.find("\r\n", pos);
  return msg.substr(pos, end - pos);
}

// Independent implementation of the WebSocket accept-key derivation so tests
// don't share code with the production path.
static std::string computeAcceptKey(const std::string &key) {
  static constexpr char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string input = key + WS_GUID;

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digestLen = 0;
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);
  EVP_DigestUpdate(ctx, input.data(), input.size());
  EVP_DigestFinal_ex(ctx, digest, &digestLen);
  EVP_MD_CTX_free(ctx);

  BIO *b64 = BIO_new(BIO_f_base64());
  BIO *mem = BIO_new(BIO_s_mem());
  BIO_push(b64, mem);
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(b64, digest, static_cast<int>(digestLen));
  BIO_flush(b64);
  char *data;
  long len = BIO_get_mem_data(mem, &data);
  std::string result(data, len);
  BIO_free_all(b64);
  return result;
}

static void sendString(TCPServer &server, const std::string &s) {
  std::vector<uint8_t> v(s.begin(), s.end());
  server.sendMessage(std::span<uint8_t>(v));
}

static std::string validUpgradeResponse(const std::string &key) {
  return "HTTP/1.1 101 Switching Protocols\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Accept: " +
         computeAcceptKey(key) + "\r\n\r\n";
}

// ---- Fixture ---------------------------------------------------------------

class WebSocketClientTest : public ::testing::Test {
protected:
  // Set this in each test to control how the mock server responds.
  std::function<void(std::vector<uint8_t> &)> dispatchableServerHandler;

  std::unique_ptr<TCPServer> server;
  std::unique_ptr<WebSocketClient> wsClient;

  void SetUp() override {
    server = std::make_unique<TCPServer>(
        "127.0.0.1",
        [this](std::vector<uint8_t> &buf) {
          if (dispatchableServerHandler)
            dispatchableServerHandler(buf);
        },
        WS_TEST_PORT);
    server->start();
    wsClient =
        std::make_unique<WebSocketClient>("127.0.0.1", WS_TEST_PORT);
  }

  void TearDown() override {
    wsClient.reset();
    if (server) {
      server->stop();
      server.reset();
    }
  }
};

// ---- Tests -----------------------------------------------------------------

// Verify the server receives a properly formatted HTTP/1.1 upgrade request.
TEST_F(WebSocketClientTest, HandshakeIsValidHTTP) {
  std::string captured;
  std::mutex m;
  std::condition_variable cv;
  bool received = false;

  dispatchableServerHandler = [&](std::vector<uint8_t> &buf) {
    {
      std::lock_guard<std::mutex> lk(m);
      captured.insert(captured.end(), buf.begin(), buf.end());
      buf.clear();
      received = true;
    }
    cv.notify_one();
  };

  // connect() blocks — run it on a background thread so we can assert first.
  std::exception_ptr connectEx;
  std::thread t([&] {
    try {
      wsClient->connect();
    } catch (...) {
      connectEx = std::current_exception();
    }
  });

  // Wait until the server receives the handshake request.
  {
    std::unique_lock<std::mutex> lk(m);
    ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return received; }))
        << "Server did not receive handshake within timeout";
  }

  EXPECT_NE(captured.find("GET / HTTP/1.1\r\n"), std::string::npos)
      << "Missing or malformed request line";
  EXPECT_NE(captured.find("Upgrade: websocket\r\n"), std::string::npos)
      << "Missing Upgrade header";
  EXPECT_NE(captured.find("Connection: Upgrade\r\n"), std::string::npos)
      << "Missing Connection header";
  EXPECT_NE(captured.find("Sec-WebSocket-Key: "), std::string::npos)
      << "Missing Sec-WebSocket-Key header";
  EXPECT_NE(captured.find("Sec-WebSocket-Version: 13\r\n"), std::string::npos)
      << "Missing Sec-WebSocket-Version header";
  EXPECT_NE(captured.find("\r\n\r\n"), std::string::npos)
      << "Missing blank line terminating headers";

  // Send a valid 101 to unblock connect().
  std::string key = extractHeader(captured, "Sec-WebSocket-Key");
  sendString(*server, validUpgradeResponse(key));

  t.join();
  EXPECT_EQ(connectEx, nullptr) << "connect() threw unexpectedly";
}

// Verify the Sec-WebSocket-Key is a 24-character base64 string.
TEST_F(WebSocketClientTest, HandshakeSendsCorrectKey) {
  std::string captured;
  std::mutex m;
  std::condition_variable cv;
  bool received = false;

  dispatchableServerHandler = [&](std::vector<uint8_t> &buf) {
    {
      std::lock_guard<std::mutex> lk(m);
      captured.insert(captured.end(), buf.begin(), buf.end());
      buf.clear();
      received = true;
    }
    cv.notify_one();
  };

  std::thread t([&] {
    try { wsClient->connect(); } catch (...) {}
  });

  {
    std::unique_lock<std::mutex> lk(m);
    ASSERT_TRUE(cv.wait_for(lk, 2s, [&] { return received; }));
  }

  std::string key = extractHeader(captured, "Sec-WebSocket-Key");
  // 16 random bytes base64-encoded without padding issues = 24 chars.
  EXPECT_EQ(key.size(), 24u) << "Sec-WebSocket-Key should be 24 characters";
  EXPECT_EQ(key.find_first_not_of(
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                "0123456789+/="),
            std::string::npos)
      << "Sec-WebSocket-Key contains invalid base64 characters";

  sendString(*server, validUpgradeResponse(key));
  t.join();
}

// Server sends a valid 101 — connect() should complete and report connected.
TEST_F(WebSocketClientTest, ConnectsSuccessfullyWithValidResponse) {
  dispatchableServerHandler = [&](std::vector<uint8_t> &buf) {
    std::string req(buf.begin(), buf.end());
    buf.clear();
    std::string key = extractHeader(req, "Sec-WebSocket-Key");
    sendString(*server, validUpgradeResponse(key));
  };

  EXPECT_NO_THROW(wsClient->connect());
  EXPECT_TRUE(wsClient->isWebSocketConnected());
}

// Server sends a non-101 status — connect() must throw.
TEST_F(WebSocketClientTest, HandshakeThrowsOnBadStatusCode) {
  dispatchableServerHandler = [&](std::vector<uint8_t> &buf) {
    buf.clear();
    sendString(*server, "HTTP/1.1 400 Bad Request\r\n\r\n");
  };

  EXPECT_THROW(wsClient->connect(), std::runtime_error);
}

// Server sends 101 but with a wrong Accept key — connect() must throw.
TEST_F(WebSocketClientTest, HandshakeThrowsOnBadAcceptKey) {
  dispatchableServerHandler = [&](std::vector<uint8_t> &buf) {
    buf.clear();
    sendString(*server,
               "HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "Sec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAAA==\r\n\r\n");
  };

  EXPECT_THROW(wsClient->connect(), std::runtime_error);
}

// Server sends 101 with no Sec-WebSocket-Accept header — connect() must throw.
TEST_F(WebSocketClientTest, HandshakeThrowsOnMissingAcceptKey) {
  dispatchableServerHandler = [&](std::vector<uint8_t> &buf) {
    buf.clear();
    sendString(*server,
               "HTTP/1.1 101 Switching Protocols\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n\r\n");
  };

  EXPECT_THROW(wsClient->connect(), std::runtime_error);
}
