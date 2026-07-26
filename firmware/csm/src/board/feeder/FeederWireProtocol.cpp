#include "board/feeder/FeederWireProtocol.h"

#include <string.h>

namespace csm::board::feeder {
namespace {

uint16_t readU16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         (static_cast<uint16_t>(input[1]) << 8U);
}

uint32_t readU32(const uint8_t* input) {
  return static_cast<uint32_t>(input[0]) |
         (static_cast<uint32_t>(input[1]) << 8U) |
         (static_cast<uint32_t>(input[2]) << 16U) |
         (static_cast<uint32_t>(input[3]) << 24U);
}

uint32_t forwardDistance(uint32_t actual, uint32_t expected) {
  return actual - expected;
}

}  // namespace

void FeederWireDecoder::begin(FeederCanFrameFn frame_fn, void* context) {
  frame_fn_ = frame_fn;
  context_ = context;
  encoded_length_ = 0;
  discard_until_delimiter_ = false;
  session_valid_ = false;
  packet_sequence_valid_ = false;
  frame_sequence_valid_ = false;
  status_valid_ = false;
  expected_packet_sequence_ = 0;
  expected_frame_sequence_ = 0;
  last_packet_mono_us_ = 0;
  last_status_mono_us_ = 0;
  stats_ = {};
  status_ = {};
}

void FeederWireDecoder::setCallback(FeederCanFrameFn frame_fn,
                                    void* context) {
  frame_fn_ = frame_fn;
  context_ = context;
}

void FeederWireDecoder::resetFraming() {
  encoded_length_ = 0;
  discard_until_delimiter_ = true;
}

void FeederWireDecoder::push(const uint8_t* bytes, size_t length,
                             uint64_t arrival_mono_us) {
  if (bytes == nullptr) {
    return;
  }
  for (size_t index = 0; index < length; ++index) {
    const uint8_t byte = bytes[index];
    if (byte == 0U) {
      if (discard_until_delimiter_) {
        discard_until_delimiter_ = false;
        encoded_length_ = 0;
        continue;
      }
      if (encoded_length_ == 0U) {
        ++stats_.empty_delimiters;
        continue;
      }
      finishPacket(arrival_mono_us);
      encoded_length_ = 0;
      continue;
    }
    if (discard_until_delimiter_) {
      continue;
    }
    if (encoded_length_ >= sizeof(encoded_)) {
      ++stats_.encoded_overflow;
      encoded_length_ = 0;
      discard_until_delimiter_ = true;
      continue;
    }
    encoded_[encoded_length_++] = byte;
  }
}

void FeederWireDecoder::finishPacket(uint64_t arrival_mono_us) {
  const size_t raw_length =
      cobsDecode(encoded_, encoded_length_, raw_, sizeof(raw_));
  if (raw_length < kFeederWireHeaderSize + sizeof(uint32_t)) {
    ++stats_.cobs_failures;
    return;
  }

  const uint32_t received_crc = readU32(&raw_[raw_length - sizeof(uint32_t)]);
  const uint32_t calculated_crc =
      crc32c(raw_, raw_length - sizeof(uint32_t));
  if (received_crc != calculated_crc) {
    ++stats_.crc_failures;
    return;
  }

  if (!acceptHeader(raw_, raw_length - sizeof(uint32_t), arrival_mono_us)) {
    return;
  }
  ++stats_.packets_ok;
  last_packet_mono_us_ = arrival_mono_us;
}

bool FeederWireDecoder::acceptHeader(const uint8_t* raw, size_t content_length,
                                     uint64_t arrival_mono_us) {
  if (content_length < kFeederWireHeaderSize ||
      raw[0] != kFeederWireVersion || raw[3] != kFeederWireHeaderSize ||
      readU32(&raw[20]) != kFeederWireContractId) {
    ++stats_.contract_failures;
    return false;
  }

  const uint8_t type = raw[1];
  const uint32_t boot_id = readU32(&raw[4]);
  const uint32_t packet_sequence = readU32(&raw[8]);
  const uint16_t payload_size = readU16(&raw[12]);
  const uint8_t item_count = raw[14];
  const uint8_t item_size = raw[15];
  const uint32_t source_time_us = readU32(&raw[16]);
  if (content_length !=
      static_cast<size_t>(kFeederWireHeaderSize) + payload_size) {
    ++stats_.length_failures;
    return false;
  }

  if (type == kFeederWireTypeCanBatch) {
    if (item_count == 0U || item_count > kFeederWireMaxCanItems ||
        item_size != kFeederWireCanItemSize ||
        payload_size !=
            static_cast<uint16_t>(item_count * kFeederWireCanItemSize)) {
      ++stats_.length_failures;
      return false;
    }
  } else if (type == kFeederWireTypeStatus) {
    if (item_count != 0U || item_size != 0U ||
        payload_size != kFeederWireStatusPayloadSize) {
      ++stats_.length_failures;
      return false;
    }
  } else {
    ++stats_.contract_failures;
    return false;
  }

  if (!session_valid_ || boot_id != stats_.current_boot_id) {
    if (session_valid_) {
      ++stats_.boot_changes;
    }
    session_valid_ = true;
    packet_sequence_valid_ = false;
    frame_sequence_valid_ = false;
    // Status belongs to one feeder boot epoch. A CAN batch may be the first
    // packet from a restarted feeder, so never expose the prior boot's source
    // health while waiting for the new status packet.
    status_valid_ = false;
    status_ = {};
    last_status_mono_us_ = 0;
    stats_.current_boot_id = boot_id;
  }
  if (!acceptPacketSequence(packet_sequence)) {
    return false;
  }

  const uint8_t* payload = &raw[kFeederWireHeaderSize];
  if (type == kFeederWireTypeCanBatch) {
    ++stats_.can_batches;
    for (uint8_t index = 0; index < item_count; ++index) {
      const uint8_t* item =
          &payload[static_cast<size_t>(index) * kFeederWireCanItemSize];
      FeederCanFrame frame;
      frame.source_sequence = readU32(&item[0]);
      frame.source_timestamp_us = readU32(&item[4]);
      frame.can_id_flags = readU32(&item[8]);
      frame.dlc_flags = item[12];
      memcpy(frame.data, &item[16], sizeof(frame.data));
      const uint32_t source_age_us =
          source_time_us - frame.source_timestamp_us;
      frame.estimated_mono_us =
          source_age_us <= 100000U && arrival_mono_us >= source_age_us
              ? arrival_mono_us - source_age_us
              : arrival_mono_us;
      if (!acceptFrameSequence(frame.source_sequence)) {
        continue;
      }
      if (frame_fn_ == nullptr || !frame_fn_(context_, frame)) {
        ++stats_.callback_rejects;
      }
      ++stats_.frames_ok;
    }
    return true;
  }

  if (type == kFeederWireTypeStatus) {
    uint32_t* values = &status_.uptime_ms;
    for (size_t index = 0;
         index < kFeederWireStatusPayloadSize / sizeof(uint32_t); ++index) {
      values[index] = readU32(&payload[index * sizeof(uint32_t)]);
    }
    static_assert(sizeof(FeederStatus) == kFeederWireStatusPayloadSize,
                  "status fields must remain contiguous uint32 values");
    status_valid_ = true;
    last_status_mono_us_ = arrival_mono_us;
    ++stats_.status_packets;
    return true;
  }

  // The packet type and shape were validated before sequence state changed.
  return false;
}

bool FeederWireDecoder::acceptPacketSequence(uint32_t sequence) {
  if (!packet_sequence_valid_) {
    packet_sequence_valid_ = true;
    expected_packet_sequence_ = sequence + 1U;
    stats_.last_packet_sequence = sequence;
    return true;
  }
  if (sequence == expected_packet_sequence_) {
    ++expected_packet_sequence_;
  } else {
    const uint32_t distance =
        forwardDistance(sequence, expected_packet_sequence_);
    if (distance < 0x80000000U) {
      stats_.packet_sequence_gaps += distance;
      expected_packet_sequence_ = sequence + 1U;
    } else {
      ++stats_.packet_duplicates_or_reorders;
      return false;
    }
  }
  stats_.last_packet_sequence = sequence;
  return true;
}

bool FeederWireDecoder::acceptFrameSequence(uint32_t sequence) {
  if (!frame_sequence_valid_) {
    frame_sequence_valid_ = true;
    expected_frame_sequence_ = sequence + 1U;
    stats_.last_frame_sequence = sequence;
    return true;
  }
  if (sequence == expected_frame_sequence_) {
    ++expected_frame_sequence_;
  } else {
    const uint32_t distance =
        forwardDistance(sequence, expected_frame_sequence_);
    if (distance < 0x80000000U) {
      stats_.frame_sequence_gaps += distance;
      expected_frame_sequence_ = sequence + 1U;
    } else {
      ++stats_.frame_duplicates_or_reorders;
      return false;
    }
  }
  stats_.last_frame_sequence = sequence;
  return true;
}

uint32_t FeederWireDecoder::crc32c(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8U; ++bit) {
      const uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0x82F63B78U & mask);
    }
  }
  return ~crc;
}

size_t FeederWireDecoder::cobsDecode(const uint8_t* input, size_t length,
                                     uint8_t* output, size_t capacity) {
  if (input == nullptr || output == nullptr || length == 0U) {
    return 0;
  }
  size_t read_index = 0;
  size_t write_index = 0;
  while (read_index < length) {
    const uint8_t code = input[read_index++];
    if (code == 0U) {
      return 0;
    }
    const size_t copied = static_cast<size_t>(code - 1U);
    if (read_index + copied > length || write_index + copied > capacity) {
      return 0;
    }
    for (size_t index = 0; index < copied; ++index) {
      output[write_index++] = input[read_index++];
    }
    if (code != 0xFFU && read_index < length) {
      if (write_index >= capacity) {
        return 0;
      }
      output[write_index++] = 0;
    }
  }
  return write_index;
}

}  // namespace csm::board::feeder
