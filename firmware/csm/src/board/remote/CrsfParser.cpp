#include "board/remote/CrsfParser.h"

namespace csm::board::remote {
namespace {

uint8_t crc8DvbS2Update(uint8_t crc, uint8_t byte) {
  crc ^= byte;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x80u) ? static_cast<uint8_t>((crc << 1) ^ 0xD5u)
                        : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}

uint16_t unpackLittleEndianBits(const uint8_t* data,
                                uint16_t bit_offset,
                                uint8_t bit_count) {
  uint16_t value = 0;
  for (uint8_t bit = 0; bit < bit_count; ++bit) {
    const uint16_t source_bit = bit_offset + bit;
    const uint8_t source_byte = static_cast<uint8_t>(source_bit / 8u);
    const uint8_t source_mask = static_cast<uint8_t>(1u << (source_bit % 8u));
    if ((data[source_byte] & source_mask) != 0) {
      value |= static_cast<uint16_t>(1u << bit);
    }
  }
  return value;
}

}  // namespace

uint8_t computeCrsfFrameCrc(const uint8_t* type_and_payload, uint8_t len) {
  uint8_t crc = 0;
  if (type_and_payload == nullptr) {
    return crc;
  }
  for (uint8_t i = 0; i < len; ++i) {
    crc = crc8DvbS2Update(crc, type_and_payload[i]);
  }
  return crc;
}

CrsfDecodeStatus decodeCrsfRcChannelsPacked(const CrsfFrame& frame,
                                            CrsfRcChannels* channels) {
  if (channels == nullptr) {
    return CrsfDecodeStatus::NullOutput;
  }
  *channels = {};

  if (frame.type != kCrsfFrameTypeRcChannelsPacked) {
    return CrsfDecodeStatus::WrongType;
  }
  if (frame.payload_len != kCrsfRcChannelsPackedPayloadBytes) {
    return CrsfDecodeStatus::BadLength;
  }

  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    channels->raw[i] = unpackLittleEndianBits(frame.payload,
                                              static_cast<uint16_t>(i) * 11u,
                                              11);
  }
  channels->count = kRcChannelCount;
  return CrsfDecodeStatus::Ok;
}

void CrsfParser::reset() {
  pos_ = 0;
  expected_total_ = 0;
}

CrsfParseResult CrsfParser::ingest(uint8_t byte) {
  if (pos_ == 0) {
    buffer_[pos_++] = byte;
    return {};
  }

  if (pos_ == 1) {
    if (byte < kCrsfMinLengthField || byte > kCrsfMaxLengthField) {
      return reject(CrsfParseStatus::RejectedLength);
    }
    buffer_[pos_++] = byte;
    expected_total_ = static_cast<uint8_t>(byte + 2u);
    return {};
  }

  buffer_[pos_++] = byte;
  if (pos_ < expected_total_) {
    return {};
  }

  return emitFrame();
}

CrsfParseResult CrsfParser::reject(CrsfParseStatus status) {
  ++malformed_total_;
  reset();

  CrsfParseResult result;
  result.status = status;
  result.malformed_total = malformed_total_;
  return result;
}

CrsfParseResult CrsfParser::emitFrame() {
  CrsfParseResult result;
  result.malformed_total = malformed_total_;

  const uint8_t crc_len = static_cast<uint8_t>(expected_total_ - 3u);
  const uint8_t expected_crc = computeCrsfFrameCrc(&buffer_[2], crc_len);
  const uint8_t actual_crc = buffer_[expected_total_ - 1u];
  if (expected_crc != actual_crc) {
    return reject(CrsfParseStatus::RejectedCrc);
  }

  result.status = CrsfParseStatus::FrameReady;
  result.frame.address = buffer_[0];
  result.frame.length = buffer_[1];
  result.frame.type = buffer_[2];
  result.frame.payload_len = static_cast<uint8_t>(buffer_[1] - 2u);
  for (uint8_t i = 0; i < result.frame.payload_len; ++i) {
    result.frame.payload[i] = buffer_[3u + i];
  }
  result.frame.crc = actual_crc;
  reset();
  return result;
}

}  // namespace csm::board::remote
