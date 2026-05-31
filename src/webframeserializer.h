#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace websocklib {

/**
 * Parses and serialises RFC 6455 WebSocket frames.
 *
 *   0                   1                   2                   3
 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *  +-+-+-+-+-------+-+-------------+-------------------------------+
 *  |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 *  |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
 *  |N|V|V|V|       |S|             |   (if payload len==126/127)   |
 *  | |1|2|3|       |K|             |                               |
 *  +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
 *  |     Extended payload length continued, if payload len == 127  |
 *  + - - - - - - - - - - - - - - - +-------------------------------+
 *  |                               |Masking-key, if MASK set to 1  |
 *  +-------------------------------+-------------------------------+
 *  | Masking-key (continued)       |          Payload Data         |
 *  +-------------------------------- - - - - - - - - - - - - - - - +
 *  :                     Payload Data continued ...                :
 *  +---------------------------------------------------------------+
 */
class WebFrameSerializer {
public:
  // onTextMessage   — called for each complete UTF-8 text message.
  // onBinaryMessage — called for each complete binary message.
  // sendCallback    — write raw bytes back to the peer (used internally to
  //                   respond to Ping frames with Pong per RFC 6455 §5.5.2).
  explicit WebFrameSerializer(
      std::function<void(const std::string &)>          onTextMessage   = nullptr,
      std::function<void(const std::vector<uint8_t> &)> onBinaryMessage = nullptr,
      std::function<void(std::vector<uint8_t>)>         sendCallback    = nullptr);

  // Parse as many complete frames as possible from incoming bytes.
  // 'packets' is consumed (cleared) on entry; partial frames are held
  // internally and completed on the next call.
  void convertRawPacketsToWebframes(std::vector<uint8_t> &packets);

  // Build a single masked WebSocket frame ready for sending.
  // Client→server frames must always be masked (RFC 6455 §5.3).
  // opcode: 0x01=text, 0x02=binary, 0x08=close, 0x09=ping, 0x0A=pong.
  static std::vector<uint8_t> buildFrame(uint8_t opcode, const uint8_t *payload,
                                         size_t len);

private:
  std::function<void(const std::string &)>          m_onTextMessage;
  std::function<void(const std::vector<uint8_t> &)> m_onBinaryMessage;
  std::function<void(std::vector<uint8_t>)>         m_sendCallback;

  // Raw byte accumulator.  m_parseOffset is the logical start of unprocessed
  // data; the dead prefix is compacted periodically to avoid unbounded growth.
  std::vector<uint8_t> m_parseBuffer;
  size_t               m_parseOffset{0};

  // Pre-allocated scratch for unmasking — avoids a heap allocation per frame.
  std::vector<uint8_t> m_payloadScratch;

  // Payload assembled across continuation frames for a fragmented message.
  std::vector<uint8_t> m_fragmentBuffer;
  uint8_t m_fragmentedOpcode{0};
};

} // namespace websocklib
