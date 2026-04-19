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

static constexpr uint16_t SERVER_TEST_PORT = 19878;

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

class TCPServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        serverData = std::make_shared<DataCollector>();
        clientData = std::make_shared<DataCollector>();

        server = std::make_unique<TCPServer>(
            "127.0.0.1",
            [this](std::vector<uint8_t>& buf) { serverData->callback(buf); },
            SERVER_TEST_PORT
        );
        server->start();

        client = std::make_unique<TCPClient>(
            "127.0.0.1",
            [this](std::vector<uint8_t>& buf) { clientData->callback(buf); },
            SERVER_TEST_PORT
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

TEST_F(TCPServerTest, AcceptsConnection) {
    EXPECT_TRUE(server->isClientConnected());
}

TEST_F(TCPServerTest, ReceivesDataFromClient) {
    std::string msg = "hello server";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    client->sendMessage(std::span<uint8_t>(payload));

    ASSERT_TRUE(serverData->waitForBytes(msg.size()))
        << "Server did not receive expected data within timeout";
    EXPECT_EQ(serverData->asString(), msg);
}

TEST_F(TCPServerTest, SendsDataToClient) {
    std::string msg = "hello client";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    server->sendMessage(std::span<uint8_t>(payload));

    ASSERT_TRUE(clientData->waitForBytes(msg.size()))
        << "Client did not receive expected data within timeout";
    EXPECT_EQ(clientData->asString(), msg);
}

TEST_F(TCPServerTest, DetectsClientDisconnect) {
    client->tcpDisconnect();
    client.reset();

    ASSERT_TRUE(waitFor([&]{ return !server->isClientConnected(); }))
        << "Server did not detect client disconnect within timeout";
}

TEST_F(TCPServerTest, AcceptsNewClientAfterReconnect) {
    // Disconnect first client, stop and restart the server, connect a new client
    client->tcpDisconnect();
    client.reset();
    server->stop();
    server.reset();

    auto serverData2 = std::make_shared<DataCollector>();
    auto server2 = std::make_unique<TCPServer>(
        "127.0.0.1",
        [&](std::vector<uint8_t>& buf) { serverData2->callback(buf); },
        SERVER_TEST_PORT
    );
    server2->start();

    auto clientData2 = std::make_shared<DataCollector>();
    auto client2 = std::make_unique<TCPClient>(
        "127.0.0.1",
        [&](std::vector<uint8_t>& buf) { clientData2->callback(buf); },
        SERVER_TEST_PORT
    );
    client2->tcpConnect();
    ASSERT_TRUE(waitFor([&]{ return server2->isClientConnected(); }));

    std::string msg = "reconnected";
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    client2->sendMessage(std::span<uint8_t>(payload));

    ASSERT_TRUE(serverData2->waitForBytes(msg.size()));
    EXPECT_EQ(serverData2->asString(), msg);

    client2->tcpDisconnect();
    server2->stop();
}

TEST_F(TCPServerTest, BidirectionalCommunication) {
    std::string toServer = "ping";
    std::string toClient = "pong";

    std::vector<uint8_t> clientPayload(toServer.begin(), toServer.end());
    client->sendMessage(std::span<uint8_t>(clientPayload));
    ASSERT_TRUE(serverData->waitForBytes(toServer.size()));
    EXPECT_EQ(serverData->asString(), toServer);

    std::vector<uint8_t> serverPayload(toClient.begin(), toClient.end());
    server->sendMessage(std::span<uint8_t>(serverPayload));
    ASSERT_TRUE(clientData->waitForBytes(toClient.size()));
    EXPECT_EQ(clientData->asString(), toClient);
}
