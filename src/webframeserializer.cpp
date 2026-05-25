#include "webframeserializer.h"

#include <openssl/rand.h>

namespace websocklib {

WebFrameSerializer::WebFrameSerializer(
    std::function<void(const std::string &)> onTextMessage)
    : m_onTextMessage(std::move(onTextMessage)) {}

WebFrameSerializer::~WebFrameSerializer() {}

void WebFrameSerializer::convertRawPacketsToWebframes(
    std::vector<uint8_t> &packets) {
    
  // TODO - implement this
}

// Serialize a single WebSocket frame. Client→server frames must be masked
// (RFC 6455 §5.3). opcode: 0x01 = text, 0x02 = binary.
std::vector<uint8_t> WebFrameSerializer::buildFrame(uint8_t opcode, const uint8_t *payload,
                                       size_t len) {
  std::vector<uint8_t> frame;

  frame.push_back(0x80 | opcode); // FIN=1, RSV=0, opcode

  // Generate 4-byte masking key
  uint8_t mask[4];
  RAND_bytes(mask, sizeof(mask));

  // Payload length + MASK bit
  if (len <= 125) {
    frame.push_back(0x80 | static_cast<uint8_t>(len));
  } else if (len <= 65535) {
    frame.push_back(0x80 | 126);
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  } else {
    frame.push_back(0x80 | 127);
    for (int shift = 56; shift >= 0; shift -= 8)
      frame.push_back(static_cast<uint8_t>((len >> shift) & 0xFF));
  }

  frame.insert(frame.end(), mask, mask + 4);

  for (size_t i = 0; i < len; ++i)
    frame.push_back(payload[i] ^ mask[i % 4]);

  return frame;
}

} // namespace websocklib
