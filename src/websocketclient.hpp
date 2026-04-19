#pragma once

#include <string>

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

    // URL to connect to using the websocket protocol
    std::string m_hostUrl;

    // TCP Client for reading / writing payloads using web socket protocol
    // TODO - Implement/integrate the TCP client here
    //TCPClient m_tcpClient;

    // TODO - use webframe serializer to decode the bytes into readable format
    // WebFrameSerializer m_webFrameSerializer
};

} // namespace websocklib
