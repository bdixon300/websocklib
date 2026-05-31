#include "webframeserializer.h"

#include <cstring>
#include <stdexcept>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace websocklib {

// XOR a buffer with a 4-byte WebSocket mask.  Processes 8 bytes at a time
// via a repeated 64-bit key to allow the compiler (or runtime) to vectorise.
static void applyMask(uint8_t *data, size_t len, const uint8_t mask[4]) {
  const uint32_t key32 =
      static_cast<uint32_t>(mask[0])        |
      (static_cast<uint32_t>(mask[1]) <<  8) |
      (static_cast<uint32_t>(mask[2]) << 16) |
      (static_cast<uint32_t>(mask[3]) << 24);
  const uint64_t key64 = (static_cast<uint64_t>(key32) << 32) | key32;

  size_t i = 0;
  for (; i + 8 <= len; i += 8) {
    uint64_t word;
    std::memcpy(&word, data + i, 8);
    word ^= key64;
    std::memcpy(data + i, &word, 8);
  }
  for (; i < len; ++i)
    data[i] ^= mask[i & 3u];
}

WebFrameSerializer::WebFrameSerializer(
    std::function<void(const std::string &)>          onText,
    std::function<void(const std::vector<uint8_t> &)> onBinary,
    std::function<void(std::vector<uint8_t>)>         sendCb)
    : m_onTextMessage(std::move(onText)),
      m_onBinaryMessage(std::move(onBinary)),
      m_sendCallback(std::move(sendCb)) {}

void WebFrameSerializer::convertRawPacketsToWebframes(
    std::vector<uint8_t> &packets) {
  m_parseBuffer.insert(m_parseBuffer.end(), packets.begin(), packets.end());
  packets.clear();

  // Compact the dead prefix when it has grown large enough to matter.
  // Done before the loop so raw pointers computed inside it are never
  // invalidated by a mid-loop erase.
  if (m_parseOffset > 65536) {
    m_parseBuffer.erase(m_parseBuffer.begin(),
                        m_parseBuffer.begin() +
                            static_cast<ptrdiff_t>(m_parseOffset));
    m_parseOffset = 0;
  }

  while (true) {
    const uint8_t *data     = m_parseBuffer.data() + m_parseOffset;
    const size_t   available = m_parseBuffer.size() - m_parseOffset;

    if (available < 2) break;

    const uint8_t byte0   = data[0];
    const uint8_t byte1   = data[1];
    const bool    fin     = (byte0 & 0x80) != 0;
    const uint8_t opcode  = byte0 & 0x0F;
    const bool    masked  = (byte1 & 0x80) != 0;
    uint64_t      payloadLen = byte1 & 0x7F;

    size_t headerLen = 2;
    if (payloadLen == 126) headerLen += 2;
    else if (payloadLen == 127) headerLen += 8;
    if (masked) headerLen += 4;

    if (available < headerLen) break;

    if (payloadLen == 126) {
      payloadLen = (static_cast<uint64_t>(data[2]) << 8) | data[3];
    } else if (payloadLen == 127) {
      payloadLen = 0;
      for (int i = 0; i < 8; ++i)
        payloadLen = (payloadLen << 8) | data[2 + i];
    }

    const size_t totalLen = headerLen + static_cast<size_t>(payloadLen);
    if (available < totalLen) break;

    // Unmask into a pre-allocated scratch buffer to avoid per-frame
    // heap allocation on the hot path.
    const size_t maskOffset = headerLen - (masked ? 4 : 0);
    m_payloadScratch.resize(payloadLen);
    if (payloadLen > 0)
      std::memcpy(m_payloadScratch.data(), data + headerLen, payloadLen);
    if (masked)
      applyMask(m_payloadScratch.data(), payloadLen, data + maskOffset);

    // Advance the logical start pointer (O(1) — no copy).
    m_parseOffset += totalLen;

    // --- Dispatch by opcode ---

    if (opcode == 0x08) {  // Close — stop processing
      break;
    }
    if (opcode == 0x09) {  // Ping — must reply with Pong (RFC 6455 §5.5.2)
      if (m_sendCallback) {
        auto pong = buildFrame(0x0A, m_payloadScratch.data(), payloadLen);
        m_sendCallback(std::move(pong));
      }
      continue;
    }
    if (opcode == 0x0A) {  // Pong — ignore
      continue;
    }

    // Data frames: text=0x01, binary=0x02, continuation=0x00
    if (opcode == 0x01 || opcode == 0x02) {
      m_fragmentBuffer.clear();
      m_fragmentedOpcode = opcode;
    }

    m_fragmentBuffer.insert(m_fragmentBuffer.end(),
                            m_payloadScratch.begin(),
                            m_payloadScratch.begin() +
                                static_cast<ptrdiff_t>(payloadLen));

    if (fin) {
      if (m_fragmentedOpcode == 0x01 && m_onTextMessage)
        m_onTextMessage(std::string(m_fragmentBuffer.begin(),
                                    m_fragmentBuffer.end()));
      else if (m_fragmentedOpcode == 0x02 && m_onBinaryMessage)
        m_onBinaryMessage(m_fragmentBuffer);
      m_fragmentBuffer.clear();
      m_fragmentedOpcode = 0;
    }
  }
}

std::vector<uint8_t> WebFrameSerializer::buildFrame(uint8_t opcode,
                                                    const uint8_t *payload,
                                                    size_t len) {
  uint8_t mask[4];
  if (RAND_bytes(mask, sizeof(mask)) != 1)
    throw std::runtime_error("RAND_bytes failed: insufficient entropy");

  // Pre-allocate the exact frame size to avoid reallocations.
  size_t headerLen = 2 + 4;  // base + mask key
  if (len > 65535)      headerLen += 8;
  else if (len > 125)   headerLen += 2;

  std::vector<uint8_t> frame;
  frame.reserve(headerLen + len);

  frame.push_back(0x80 | opcode);  // FIN=1, RSV=0, opcode

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

  // Copy payload then mask in-place (single pass over the data).
  const size_t payloadStart = frame.size();
  frame.resize(payloadStart + len);
  if (len > 0 && payload)
    std::memcpy(frame.data() + payloadStart, payload, len);
  applyMask(frame.data() + payloadStart, len, mask);

  return frame;
}

} // namespace websocklib
