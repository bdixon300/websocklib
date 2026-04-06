#pragma once

#include <string>

namespace websocklib {

/**
 * 
 * The websocket will have a connect/disconnect etc
 * 
 * It will also hold a 
 * 1. tcp client instance
 * 2. webframe serializer (to serialize / deserialize data from/to websocket frames into raw bytes to send over wire)
 * 
 * Once we start the TCP connection/TCP client
 * we need to send a HTTP GET request to upgrade our tcp connection to websocket
 * we get a response back that we need to handle -> https://datatracker.ietf.org/doc/html/rfc6455#section-4.1
 * 
 * eg:
 * 
 *   * GET /chat HTTP/1.1\r\n
        Host: example.com\r\n
        Upgrade: websocket\r\n
        Connection: Upgrade\r\n
        Sec-WebSocket-Key: <base64 encoded 16 random bytes>\r\n
        Sec-WebSocket-Version: 13\r\n
        \r\n
    
        Once done, We should receive a response as follows ->

        HTTP/1.1 101 Switching Protocols\r\n
        Upgrade: websocket\r\n
        Connection: Upgrade\r\n
        Sec-WebSocket-Accept: <base64 encoded SHA-1 hash>\r\n
        \r\n
 * 
 * 
 * 
 * After that all packets from tcp client are websocket frames and can be parsed as such
 * 
 * 
 */

class WebSocketClient {
public:
    WebSocketClient();


    ~WebSocketClient();

private:

    
    // Method will generate the upgrade GET request to leverage our underlying
    // TCP connection as a web socket connection
    std::string generateHandShakePayload();

    // sends handshake to TCP (HTTP payload required)
    void sendHandshake();


    // TODO - something to process the webframes and do something with them
};

} // namespace websocklib
