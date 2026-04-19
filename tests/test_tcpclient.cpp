#include <gtest/gtest.h>
#include "tcpclient.h"
#include "tcpserver.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

using namespace websocklib;
using namespace std::chrono_literals;

static constexpr uint16_t CLIENT_TEST_PORT = 19877;

struct DataCollector {
    std::mutex mtx;
    std::condition_variable cv;
    std::vector<uint8_t> data;

    void callback(std::vector<uint8_t>& buf) {
        std::lock_guard<std::mutex> lock(mtx);
        data.insert(data.end(), buf.begin(), buf.end());
        buf.clear();
        cv.notify_all();
    }

    bool waitForBytes(size_t n, std::chrono::milliseconds timeout = 2000ms) {
        std::unique_lock<std::mutex> lock(mtx);
        return cv.wait_for(lock, timeout, [&]{ return data.size() >= n; });
    }

    std::string asString() {
        std::lock_guard<std::mutex> lock(mtx);
        return {data.begin(), data.end()};
    }

    std::vector<uint8_t> asBytes() {
        std::lock_guard<std::mutex> lock(mtx);
        return data;
    }
};

template<typename Pred>
static bool waitFor(Pred pred, std::chrono::milliseconds timeout = 1000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

class TCPClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        serverData = std::make_shared<DataCollector>();
        clientData = std::make_shared<DataCollector>();

        server = std::make_unique<TCPServer>(
            "127.0.0.1",
            [this](std::vector<uint8_t>& buf) { serverData->callback(buf); },
            CLIENT_TEST_PORT
        );
        server->start();

        client = std::make_unique<TCPClient>(
            "127.0.0.1",
            [this](std::vector<uint8_t>& buf) { clientData->callback(buf); },
            CLIENT_TEST_PORT
        );
        client->tcpConnect();

        ASSERT_TRUE(waitFor([&]{ return server->isClientConnected(); }))
            << "Server did not accept connection within timeout";
    }

    void TearDown() override {
        if (client) { client->tcpDisconnect(); client.reset(); }
        if (server) { server->stop(); server.reset(); }
    }

    std::unique_ptr<TCPServer> server;
    std::unique_ptr<TCPClient> client;
    std::shared_ptr<DataCollector> serverData;
    std::shared_ptr<DataCollector> clientData;
};

TEST_F(TCPClientTest, ConnectsSuccessfully) {
    EXPECT_TRUE(client->isConnected());
}

TEST_F(TCPClientTest, SendsDataToServer) {
    std::string msg = "hello server";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    client->sendMessage(std::span<uint8_t>(payload));

    ASSERT_TRUE(serverData->waitForBytes(msg.size()))
        << "Server did not receive expected data within timeout";
    EXPECT_EQ(serverData->asString(), msg);
}

TEST_F(TCPClientTest, ReceivesDataFromServer) {
    std::string msg = "hello client";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    server->sendMessage(std::span<uint8_t>(payload));

    ASSERT_TRUE(clientData->waitForBytes(msg.size()))
        << "Client did not receive expected data within timeout";
    EXPECT_EQ(clientData->asString(), msg);
}

TEST_F(TCPClientTest, SendsLargePayload) {
    std::vector<uint8_t> payload(20000, 0xAB);
    client->sendMessage(std::span<uint8_t>(payload));

    ASSERT_TRUE(serverData->waitForBytes(payload.size(), 5000ms));
    EXPECT_EQ(serverData->asBytes(), payload);
}

TEST_F(TCPClientTest, SendsMultipleMessages) {
    std::vector<std::string> parts = {"foo", "bar", "baz"};
    std::string expected;
    for (const auto& part : parts) {
        expected += part;
        std::vector<uint8_t> payload(part.begin(), part.end());
        client->sendMessage(std::span<uint8_t>(payload));
    }

    ASSERT_TRUE(serverData->waitForBytes(expected.size()));
    EXPECT_EQ(serverData->asString(), expected);
}

TEST_F(TCPClientTest, DisconnectsCleanly) {
    EXPECT_TRUE(client->isConnected());
    client->tcpDisconnect();
    EXPECT_FALSE(client->isConnected());
    client.reset(); // prevent double-disconnect in TearDown
}
