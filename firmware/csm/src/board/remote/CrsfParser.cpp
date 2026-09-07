#include "board/remote/CrsfParser.h"

#include <string.h>

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

void saturatingIncrement(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) ++(*value);
}

uint16_t subsetToLegacyRaw(uint16_t raw, uint8_t bits) {
  const uint32_t maximum = (1u << bits) - 1u;
  const uint32_t span = 1811u - 172u;
  return static_cast<uint16_t>(172u +
      (static_cast<uint32_t>(raw) * span + maximum / 2u) / maximum);
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
  channels->valid_mask = 0xFFFFu;
  channels->count = kRcChannelCount;
  return CrsfDecodeStatus::Ok;
}

CrsfDecodeStatus decodeCrsfSubsetRcChannelsPacked(
    const CrsfFrame& frame, CrsfRcChannels* channels) {
  if (channels == nullptr) return CrsfDecodeStatus::NullOutput;
  *channels = {};
  if (frame.type != kCrsfFrameTypeSubsetRcChannelsPacked) {
    return CrsfDecodeStatus::WrongType;
  }
  if (frame.payload_len < 3u) return CrsfDecodeStatus::BadLength;
  const uint8_t config = frame.payload[0];
  if ((config & 0x80u) != 0u) {
    // Digital-switch encoding is not part of the qualified R16SM profile.
    return CrsfDecodeStatus::UnsupportedEncoding;
  }
  const uint8_t first_channel = config & 0x1Fu;
  const uint8_t channel_bits = static_cast<uint8_t>(10u + ((config >> 5u) & 0x03u));
  const uint16_t data_bits = static_cast<uint16_t>(frame.payload_len - 1u) * 8u;
  const uint8_t channel_count = static_cast<uint8_t>(data_bits / channel_bits);
  if (channel_count == 0u || first_channel >= kRcChannelCount ||
      first_channel + channel_count > kRcChannelCount) {
    return CrsfDecodeStatus::BadLength;
  }
  for (uint8_t index = 0; index < channel_count; ++index) {
    const uint16_t packed = unpackLittleEndianBits(
        &frame.payload[1], static_cast<uint16_t>(index) * channel_bits,
        channel_bits);
    const uint8_t channel = static_cast<uint8_t>(first_channel + index);
    channels->raw[channel] = subsetToLegacyRaw(packed, channel_bits);
    channels->valid_mask |= static_cast<uint16_t>(1u << channel);
  }
  channels->count = channel_count;
  return CrsfDecodeStatus::Ok;
}

CrsfDecodeStatus decodeCrsfRcChannels(const CrsfFrame& frame,
                                      CrsfRcChannels* channels) {
  if (frame.type == kCrsfFrameTypeRcChannelsPacked) {
    return decodeCrsfRcChannelsPacked(frame, channels);
  }
  if (frame.type == kCrsfFrameTypeSubsetRcChannelsPacked) {
    return decodeCrsfSubsetRcChannelsPacked(frame, channels);
  }
  return channels == nullptr ? CrsfDecodeStatus::NullOutput
                             : CrsfDecodeStatus::WrongType;
}

CrsfDecodeStatus decodeCrsfLinkStatistics(const CrsfFrame& frame,
                                          CrsfLinkStatistics* statistics) {
  if (statistics == nullptr) {
    return CrsfDecodeStatus::NullOutput;
  }
  *statistics = {};
  if (frame.type == kCrsfFrameTypeLinkStatistics) {
    if (frame.payload_len < kCrsfLinkStatisticsPayloadBytes) {
      return CrsfDecodeStatus::BadLength;
    }
    statistics->kind = CrsfLinkStatisticsKind::Legacy;
    statistics->uplink_rssi_ant1_dbm_magnitude = frame.payload[0];
    statistics->uplink_rssi_ant2_dbm_magnitude = frame.payload[1];
    statistics->uplink_link_quality = frame.payload[2];
    statistics->uplink_snr_db = static_cast<int8_t>(frame.payload[3]);
    statistics->active_antenna = frame.payload[4];
    statistics->rf_profile = frame.payload[5];
    statistics->uplink_rf_power = frame.payload[6];
    statistics->downlink_rssi_dbm_magnitude = frame.payload[7];
    statistics->downlink_link_quality = frame.payload[8];
    statistics->downlink_snr_db = static_cast<int8_t>(frame.payload[9]);
  } else if (frame.type == kCrsfFrameTypeLinkStatisticsRx ||
             frame.type == kCrsfFrameTypeLinkStatisticsTx) {
    const uint8_t required = frame.type == kCrsfFrameTypeLinkStatisticsTx ? 6u : 5u;
    if (frame.payload_len < required) return CrsfDecodeStatus::BadLength;
    statistics->kind = frame.type == kCrsfFrameTypeLinkStatisticsRx
        ? CrsfLinkStatisticsKind::Receiver
        : CrsfLinkStatisticsKind::Transmitter;
    statistics->uplink_rssi_ant1_dbm_magnitude = frame.payload[0];
    statistics->uplink_rssi_ant2_dbm_magnitude = frame.payload[0];
    statistics->uplink_link_quality = frame.payload[2];
    statistics->uplink_snr_db = static_cast<int8_t>(frame.payload[3]);
    statistics->uplink_rf_power = frame.payload[4];
  } else {
    return CrsfDecodeStatus::WrongType;
  }
  if (statistics->uplink_link_quality > 100u) {
    *statistics = {};
    return CrsfDecodeStatus::BadValue;
  }
  return CrsfDecodeStatus::Ok;
}

uint8_t buildCrsfBroadcastFrame(uint8_t frame_type,
                                const uint8_t* payload,
                                uint8_t payload_len,
                                uint8_t* output,
                                uint8_t output_capacity,
                                uint8_t sync_byte) {
  const uint8_t total = static_cast<uint8_t>(payload_len + kCrsfFrameOverheadBytes);
  if (output == nullptr || output_capacity < total ||
      payload_len > kCrsfMaxPayloadBytes) {
    return 0;
  }
  if (payload_len != 0 && payload == nullptr) {
    return 0;
  }

  output[0] = sync_byte;
  output[1] = static_cast<uint8_t>(payload_len + 2u);
  output[2] = frame_type;
  for (uint8_t i = 0; i < payload_len; ++i) {
    output[3u + i] = payload[i];
  }
  output[total - 1u] = computeCrsfFrameCrc(&output[2],
                                           static_cast<uint8_t>(payload_len + 1u));
  return total;
}

void CrsfParser::reset() {
  pos_ = 0;
}

CrsfParseResult CrsfParser::ingest(uint8_t byte) {
  if (pos_ >= kCrsfMaxFrameBytes) reset();
  buffer_[pos_++] = byte;
  return evaluate();
}

void CrsfParser::discardPrefix(uint8_t count) {
  if (count >= pos_) {
    reset();
    return;
  }
  memmove(buffer_, &buffer_[count], pos_ - count);
  pos_ = static_cast<uint8_t>(pos_ - count);
}

void CrsfParser::noteReject(CrsfParseStatus status) {
  if (status == CrsfParseStatus::RejectedAddress) {
    saturatingIncrement(&rejected_address_total_);
  } else if (status == CrsfParseStatus::RejectedLength) {
    saturatingIncrement(&rejected_length_total_);
  } else if (status == CrsfParseStatus::RejectedCrc) {
    saturatingIncrement(&rejected_crc_total_);
  }
}

uint32_t CrsfParser::malformedTotal() const {
  const uint64_t total = static_cast<uint64_t>(rejected_address_total_) +
      rejected_length_total_ + rejected_crc_total_;
  return total > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(total);
}

CrsfParseResult CrsfParser::evaluate() {
  CrsfParseStatus last_reject = CrsfParseStatus::Waiting;
  for (uint8_t attempt = 0; attempt < kCrsfMaxFrameBytes; ++attempt) {
    if (pos_ == 0u) break;
    if (buffer_[0] != kR16smCrsfAddress) {
      noteReject(CrsfParseStatus::RejectedAddress);
      last_reject = CrsfParseStatus::RejectedAddress;
      discardPrefix(1u);
      continue;
    }
    if (pos_ < 2u) break;
    if (buffer_[1] < kCrsfMinLengthField ||
        buffer_[1] > kCrsfMaxLengthField) {
      noteReject(CrsfParseStatus::RejectedLength);
      last_reject = CrsfParseStatus::RejectedLength;
      discardPrefix(1u);
      continue;
    }
    const uint8_t expected_total = static_cast<uint8_t>(buffer_[1] + 2u);
    if (pos_ < expected_total) break;
    const uint8_t crc_len = static_cast<uint8_t>(expected_total - 3u);
    const uint8_t expected_crc = computeCrsfFrameCrc(&buffer_[2], crc_len);
    const uint8_t actual_crc = buffer_[expected_total - 1u];
    if (expected_crc != actual_crc) {
      noteReject(CrsfParseStatus::RejectedCrc);
      last_reject = CrsfParseStatus::RejectedCrc;
      discardPrefix(1u);
      continue;
    }

    CrsfParseResult result;
    result.status = CrsfParseStatus::FrameReady;
    result.malformed_total = malformedTotal();
    result.frame.address = buffer_[0];
    result.frame.length = buffer_[1];
    result.frame.type = buffer_[2];
    result.frame.payload_len = static_cast<uint8_t>(buffer_[1] - 2u);
    for (uint8_t i = 0; i < result.frame.payload_len; ++i) {
      result.frame.payload[i] = buffer_[3u + i];
    }
    result.frame.crc = actual_crc;
    discardPrefix(expected_total);
    return result;
  }
  CrsfParseResult result;
  result.status = last_reject;
  result.malformed_total = malformedTotal();
  return result;
}

}  // namespace csm::board::remote
