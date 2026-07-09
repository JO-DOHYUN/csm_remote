#include "protocol/TypedFrame.h"

#include <cstring>

namespace csm {

void wr_u16_le(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void wr_u32_le(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

void wr_i32_le(uint8_t* p, int32_t v) {
  wr_u32_le(p, static_cast<uint32_t>(v));
}

void wr_u64_le(uint8_t* p, uint64_t v) {
  for (uint8_t i = 0; i < 8; ++i) {
    p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
  }
}

void wr_i64_le(uint8_t* p, int64_t v) {
  wr_u64_le(p, static_cast<uint64_t>(v));
}

uint16_t rd_u16_le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t rd_u32_le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

uint64_t mono64_us() {
  static uint32_t last = 0;
  static uint64_t high = 0;

  const uint32_t now = micros();
  if (now < last) {
    high += (1ULL << 32);
  }
  last = now;
  return high | now;
}

bool encode_typed_frame(uint8_t* frame, size_t capacity, RecordType type,
                        const uint8_t* payload, uint16_t len, uint16_t seq,
                        uint8_t flags, size_t* written) {
  if (frame == nullptr || len > kMaxPayloadLen || (len > 0 && payload == nullptr)) {
    return false;
  }
  const size_t frame_len = encoded_typed_frame_len(len);
  if (capacity < frame_len) {
    return false;
  }

  size_t pos = 0;
  frame[pos++] = kFrameSof0;
  frame[pos++] = kFrameSof1;
  frame[pos++] = kProtocolVersion;
  frame[pos++] = static_cast<uint8_t>(type);
  frame[pos++] = flags;
  wr_u16_le(&frame[pos], seq);
  pos += 2;
  wr_u16_le(&frame[pos], len);
  pos += 2;
  if (len > 0) {
    memcpy(&frame[pos], payload, len);
    pos += len;
  }

  const uint16_t crc = crc16_ccitt(&frame[2], pos - 2);
  wr_u16_le(&frame[pos], crc);
  pos += 2;

  if (written != nullptr) {
    *written = pos;
  }
  return true;
}

bool emit_typed_record(Stream& serial, RecordType type, const uint8_t* payload,
                       uint16_t len, uint16_t& seq, uint8_t flags) {
  if (len > kMaxPayloadLen) {
    return false;
  }

  uint8_t frame[2 + 1 + 1 + 1 + 2 + 2 + kMaxPayloadLen + 2];
  size_t pos = 0;
  if (!encode_typed_frame(frame, sizeof(frame), type, payload, len, seq, flags, &pos)) {
    return false;
  }
  seq++;

  serial.write(frame, pos);
  return true;
}

}  // namespace csm
