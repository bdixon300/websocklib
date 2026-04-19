#include "websocketclient.hpp"
#include <sstream>    
#include <openssl/rand.h>
#include <openssl/bio.h>
#include <openssl/evp.h> 

namespace websocklib {

constexpr char PLACEHOLDER_ORIGIN[] = "https://placeholderorigin.com";

WebSocketClient::WebSocketClient(const std::string& hostUrl): m_hostUrl(hostUrl) {}

WebSocketClient::~WebSocketClient() {}

void WebSocketClient::connect()
{
    sendHandshake();
}

void WebSocketClient::sendHandshake()
{
    // // Establish TCP Connection
    // m_tcpClient.tcpConnect();

    // // Send HTTP websocket upgrade handshake
    // m_tcpClient.sendMessage(generateHandShakePayload());
}

std::string WebSocketClient::generateHandShakePayload()
{
    std::ostringstream ss;

    ss << "GET /chat HTTP/1.1 "
       << "Host: " << m_hostUrl << " "
       << "Upgrade: websocket "
       << "Connection: Upgrade"
       << "Sec-WebSocket-Key: " << generateWebSocketKey() << " "
       << "Origin: " << PLACEHOLDER_ORIGIN
       << "Sec-WebSocket-Protocol: chat, superchat "
       << "Sec-WebSocket-Version: 13";

    return ss.str();
}

std::string WebSocketClient::generateWebSocketKey() {
    unsigned char random_bytes[16];
    RAND_bytes(random_bytes, sizeof(random_bytes));

    // Base64 encode
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    BIO_push(b64, mem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, random_bytes, sizeof(random_bytes));
    BIO_flush(b64);

    char* data;
    long len = BIO_get_mem_data(mem, &data);
    std::string key(data, len);
    BIO_free_all(b64);
    return key;
}

} // namespace websocklib
