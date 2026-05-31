#include <gtest/gtest.h>
#include "webframeserializer.h"

#include <string>
#include <vector>

using namespace websocklib;

// Build a server→client frame (unmasked, as the server sends).
static std::vector<uint8_t> makeServerFrame(uint8_t opcode,
                                            const std::string &payload,
                                            bool fin = true,
                                            bool masked = false) {
  std::vector<uint8_t> frame;
  frame.push_back((fin ? 0x80 : 0x00) | opcode);

  size_t len = payload.size();
  uint8_t maskBit = masked ? 0x80 : 0x00;

  if (len <= 125) {
    frame.push_back(maskBit | static_cast<uint8_t>(len));
  } else if (len <= 65535) {
    frame.push_back(maskBit | 126);
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  } else {
    frame.push_back(maskBit | 127);
    for (int shift = 56; shift >= 0; shift -= 8)
      frame.push_back(static_cast<uint8_t>((len >> shift) & 0xFF));
  }

  if (masked) {
    uint8_t key[4] = {0x12, 0x34, 0x56, 0x78};
    frame.insert(frame.end(), key, key + 4);
    for (size_t i = 0; i < len; ++i)
      frame.push_back(static_cast<uint8_t>(payload[i]) ^ key[i % 4]);
  } else {
    frame.insert(frame.end(), payload.begin(), payload.end());
  }

  return frame;
}

// ---- buildFrame (serializer) -----------------------------------------------

TEST(WebFrameSerializerBuildFrame, TextFrameHasCorrectOpcode) {
  const std::string msg = "hello";
  auto frame = WebFrameSerializer::buildFrame(
      0x01, reinterpret_cast<const uint8_t *>(msg.data()), msg.size());

  EXPECT_EQ(frame[0], 0x81); // FIN=1, opcode=1
  EXPECT_TRUE((frame[1] & 0x80) != 0) << "MASK bit must be set for client→server";
}

TEST(WebFrameSerializerBuildFrame, BinaryFrameHasCorrectOpcode) {
  std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
  auto frame = WebFrameSerializer::buildFrame(0x02, data.data(), data.size());

  EXPECT_EQ(frame[0], 0x82);
  EXPECT_TRUE((frame[1] & 0x80) != 0);
}

TEST(WebFrameSerializerBuildFrame, ShortPayloadLength) {
  const std::string msg = "hi";
  auto frame = WebFrameSerializer::buildFrame(
      0x01, reinterpret_cast<const uint8_t *>(msg.data()), msg.size());

  EXPECT_EQ(frame[1] & 0x7F, 2u);
}

TEST(WebFrameSerializerBuildFrame, ExtendedPayload16Bit) {
  std::vector<uint8_t> payload(200, 'x');
  auto frame = WebFrameSerializer::buildFrame(0x02, payload.data(), payload.size());

  EXPECT_EQ(frame[1] & 0x7F, 126u);
  uint16_t len = (static_cast<uint16_t>(frame[2]) << 8) | frame[3];
  EXPECT_EQ(len, 200u);
}

TEST(WebFrameSerializerBuildFrame, ExtendedPayload64Bit) {
  std::vector<uint8_t> payload(70000, 'z');
  auto frame = WebFrameSerializer::buildFrame(0x02, payload.data(), payload.size());

  EXPECT_EQ(frame[1] & 0x7F, 127u);
  uint64_t len = 0;
  for (int i = 0; i < 8; ++i)
    len = (len << 8) | frame[2 + i];
  EXPECT_EQ(len, 70000u);
}

TEST(WebFrameSerializerBuildFrame, MaskKeyApplied) {
  const std::string msg = "test";
  auto frame = WebFrameSerializer::buildFrame(
      0x01, reinterpret_cast<const uint8_t *>(msg.data()), msg.size());

  // Header: byte0, byte1, 4-byte mask, then masked payload.
  const uint8_t *mask = frame.data() + 2;
  for (size_t i = 0; i < msg.size(); ++i) {
    uint8_t unmasked = frame[6 + i] ^ mask[i % 4];
    EXPECT_EQ(unmasked, static_cast<uint8_t>(msg[i]));
  }
}

// ---- convertRawPacketsToWebframes ------------------------------------------

TEST(WebFrameSerializerParse, SingleTextFrame) {
  std::string received;
  WebFrameSerializer s([&](const std::string &msg) { received = msg; });

  auto frame = makeServerFrame(0x01, "hello");
  s.convertRawPacketsToWebframes(frame);

  EXPECT_EQ(received, "hello");
}

TEST(WebFrameSerializerParse, InputBufferConsumed) {
  WebFrameSerializer s(nullptr);
  auto frame = makeServerFrame(0x01, "data");
  s.convertRawPacketsToWebframes(frame);

  EXPECT_TRUE(frame.empty());
}

TEST(WebFrameSerializerParse, TwoFramesInOneChunk) {
  std::vector<std::string> msgs;
  WebFrameSerializer s([&](const std::string &m) { msgs.push_back(m); });

  auto f1 = makeServerFrame(0x01, "foo");
  auto f2 = makeServerFrame(0x01, "bar");
  std::vector<uint8_t> combined(f1);
  combined.insert(combined.end(), f2.begin(), f2.end());

  s.convertRawPacketsToWebframes(combined);

  ASSERT_EQ(msgs.size(), 2u);
  EXPECT_EQ(msgs[0], "foo");
  EXPECT_EQ(msgs[1], "bar");
}

TEST(WebFrameSerializerParse, SplitAcrossMultipleCalls) {
  std::string received;
  WebFrameSerializer s([&](const std::string &m) { received = m; });

  auto frame = makeServerFrame(0x01, "split");
  // Feed first 3 bytes, then the rest.
  std::vector<uint8_t> part1(frame.begin(), frame.begin() + 3);
  std::vector<uint8_t> part2(frame.begin() + 3, frame.end());

  s.convertRawPacketsToWebframes(part1);
  EXPECT_TRUE(received.empty()) << "Should not fire until frame is complete";

  s.convertRawPacketsToWebframes(part2);
  EXPECT_EQ(received, "split");
}

TEST(WebFrameSerializerParse, FragmentedTextMessage) {
  std::string received;
  WebFrameSerializer s([&](const std::string &m) { received = m; });

  // First fragment: FIN=0, opcode=text
  auto frag1 = makeServerFrame(0x01, "hel", /*fin=*/false);
  // Continuation with FIN=0
  auto frag2 = makeServerFrame(0x00, "lo ", /*fin=*/false);
  // Final continuation: FIN=1
  auto frag3 = makeServerFrame(0x00, "world", /*fin=*/true);

  s.convertRawPacketsToWebframes(frag1);
  EXPECT_TRUE(received.empty());
  s.convertRawPacketsToWebframes(frag2);
  EXPECT_TRUE(received.empty());
  s.convertRawPacketsToWebframes(frag3);
  EXPECT_EQ(received, "hello world");
}

TEST(WebFrameSerializerParse, PingFrameIgnored) {
  std::string received;
  WebFrameSerializer s([&](const std::string &m) { received = m; });

  auto ping = makeServerFrame(0x09, "ping-payload");
  auto text = makeServerFrame(0x01, "after-ping");

  std::vector<uint8_t> combined(ping);
  combined.insert(combined.end(), text.begin(), text.end());

  s.convertRawPacketsToWebframes(combined);
  EXPECT_EQ(received, "after-ping");
}

TEST(WebFrameSerializerParse, PongFrameIgnored) {
  std::string received;
  WebFrameSerializer s([&](const std::string &m) { received = m; });

  auto pong = makeServerFrame(0x0A, "");
  auto text = makeServerFrame(0x01, "after-pong");

  std::vector<uint8_t> combined(pong);
  combined.insert(combined.end(), text.begin(), text.end());

  s.convertRawPacketsToWebframes(combined);
  EXPECT_EQ(received, "after-pong");
}

TEST(WebFrameSerializerParse, CloseFrameStopsProcessing) {
  std::vector<std::string> msgs;
  WebFrameSerializer s([&](const std::string &m) { msgs.push_back(m); });

  auto close = makeServerFrame(0x08, "");
  auto text  = makeServerFrame(0x01, "should-not-arrive");

  std::vector<uint8_t> combined(close);
  combined.insert(combined.end(), text.begin(), text.end());

  s.convertRawPacketsToWebframes(combined);
  EXPECT_TRUE(msgs.empty());
}

TEST(WebFrameSerializerParse, LargePayload16Bit) {
  std::string received;
  WebFrameSerializer s([&](const std::string &m) { received = m; });

  std::string big(1000, 'A');
  auto frame = makeServerFrame(0x01, big);
  s.convertRawPacketsToWebframes(frame);

  EXPECT_EQ(received, big);
}

TEST(WebFrameSerializerParse, MaskedServerFrameDecodedCorrectly) {
  std::string received;
  WebFrameSerializer s([&](const std::string &m) { received = m; });

  auto frame = makeServerFrame(0x01, "masked", /*fin=*/true, /*masked=*/true);
  s.convertRawPacketsToWebframes(frame);

  EXPECT_EQ(received, "masked");
}

TEST(WebFrameSerializerParse, NullCallbackDoesNotCrash) {
  WebFrameSerializer s(nullptr);
  auto frame = makeServerFrame(0x01, "hello");
  EXPECT_NO_THROW(s.convertRawPacketsToWebframes(frame));
}
