#pragma once

#include <string>
#include "tcpclient.h"
#include "webframeserializer.h"

namespace websocklib {

class WebSocketClient {
public:
    WebSocketClient(const std::string& hostUrl);

    ~WebSocketClient();

    // establish websocket connection, 
    // (starts tcp connection, generates and sends handshake payload, 
    // receives/processes websocket frames after that)
    void connect();

private:
    // Method will generate the upgrade GET request to leverage our underlying
    // TCP connection as a web socket connection
    std::string generateHandShakePayload();

    // Helper method to generate a unique web socket key when sending a HTTP websocket handshake
    std::string generateWebSocketKey();

    // sends handshake to TCP (HTTP payload required)
    void sendHandshake();

    // Handle to retrieve TCP packets and convert them to websocket frames
    void readDataHandle(std::vector<uint8_t>& packets);

    // URL to connect to using the websocket protocol
    std::string m_hostUrl;

    // use webframe serializer to decode the bytes into readable format
    WebFrameSerializer m_webFrameSerializer;

    // TCP Client for reading / writing payloads using web socket protocol
    TCPClient m_tcpClient;
};

} // namespace websocklib
