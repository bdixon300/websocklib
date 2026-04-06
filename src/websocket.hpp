#pragma once

namespace websocklib {

/**
 * 
 * The websocket will have a connect/disconnect etc
 * 
 * It will also hold a 
 * 1. tcp client instance
 * 2. frame serializer (to serialize / deserialize data from/to websocket frames into raw bytes to send over wire)
 * 3. 
 * 
 * Once we start the TCP connection/TCP client
 * we neeed to send a HTTP GET request to upgrade our tcp connection to websocket
 * we get a response back that we need to handle -> https://datatracker.ietf.org/doc/html/rfc6455#section-4.1
 * 
 * 
 * 
 * After that we also need to handle the frames we get
 * 
 * 
 */

class WebSocket {
public:
    WebSocket();
    ~WebSocket();
};

} // namespace websocklib
