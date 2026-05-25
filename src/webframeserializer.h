#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace websocklib {

/**
 *
 * Will process/parse TCP packets into appropriate payload format per RFC :
 *
 *
 *       0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-------+-+-------------+-------------------------------+
     |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
     |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
     |N|V|V|V|       |S|             |   (if payload len==126/127)   |
     | |1|2|3|       |K|             |                               |
     +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
     |     Extended payload length continued, if payload len == 127  |
     + - - - - - - - - - - - - - - - +-------------------------------+
     |                               |Masking-key, if MASK set to 1  |
     +-------------------------------+-------------------------------+
     | Masking-key (continued)       |          Payload Data         |
     +-------------------------------- - - - - - - - - - - - - - - - +
     :                     Payload Data continued ...                :
     + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
     |                     Payload Data continued ...                |
     +---------------------------------------------------------------+
 *
 *
 *
 *
 */
class WebFrameSerializer {
public:
  explicit WebFrameSerializer(
      std::function<void(const std::string &)> onTextMessage = nullptr);
  ~WebFrameSerializer();

  // Parse as many complete frames as possible from packets, consuming
  // processed bytes. Calls onTextMessage for each delivered message.
  void convertRawPacketsToWebframes(std::vector<uint8_t> &packets);

  // Serialize a single WebSocket frame. Client→server frames must be masked
  // (RFC 6455 §5.3). opcode: 0x01 = text, 0x02 = binary.
  static std::vector<uint8_t> buildFrame(uint8_t opcode, const uint8_t *payload,
                                       size_t len);

private:
  std::function<void(const std::string &)> m_onTextMessage;
  // Accumulates payload across continuation frames for fragmented messages
  std::vector<uint8_t> m_fragmentBuffer;
  uint8_t m_fragmentedOpcode{0};
};

} // namespace websocklib
