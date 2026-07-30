#include "board/uplink/ProductDownlinkRouter.h"

#include <string.h>

#include "protocol/HostCommands.h"

namespace csm::board::uplink {

void ProductDownlinkRouter::begin(
    PassthroughHandler passthrough_handler, void* context) {
  passthrough_handler_ = passthrough_handler;
  context_ = context;
  reset();
  counters_ = {};
}

void ProductDownlinkRouter::reset() { length_ = 0; }

void ProductDownlinkRouter::drop(uint16_t count) {
  if (count >= length_) {
    length_ = 0;
    return;
  }
  memmove(buffer_, &buffer_[count], length_ - count);
  length_ = static_cast<uint16_t>(length_ - count);
}

bool ProductDownlinkRouter::process() {
  while (length_ >= 2) {
    uint16_t start = 0;
    while (start + 1 < length_ &&
           !(buffer_[start] == csm::kFrameSof0 &&
             buffer_[start + 1] == csm::kFrameSof1)) {
      ++start;
    }
    if (start != 0) {
      counters_.framing_failure_total += start;
      drop(start);
    }
    if (length_ < 9) return true;
    if (buffer_[0] != csm::kFrameSof0 ||
        buffer_[1] != csm::kFrameSof1) {
      ++counters_.framing_failure_total;
      drop(1);
      continue;
    }

    const uint16_t payload_length = csm::rd_u16_le(&buffer_[7]);
    if (payload_length > csm::kMaxPayloadLen) {
      ++counters_.framing_failure_total;
      drop(1);
      continue;
    }
    const uint16_t frame_length =
        static_cast<uint16_t>(csm::encoded_typed_frame_len(payload_length));
    if (length_ < frame_length) return true;

    const uint16_t expected_crc =
        csm::rd_u16_le(&buffer_[frame_length - 2]);
    const uint16_t actual_crc =
        csm::crc16_ccitt(&buffer_[2], frame_length - 4);
    if (expected_crc != actual_crc) {
      ++counters_.crc_failure_total;
      drop(1);
      continue;
    }

    ++counters_.frame_total;
    const bool is_legacy_app_ack =
        buffer_[2] == csm::kProtocolVersion &&
        buffer_[3] == static_cast<uint8_t>(csm::RecordType::AppRxCommitAck);
    if (is_legacy_app_ack) {
      if (payload_length == csm::kAppRxCommitAckPayloadLen) {
        ++counters_.legacy_ack_ignored_total;
      } else {
        ++counters_.legacy_ack_malformed_total;
      }
    } else {
      if (passthrough_handler_ != nullptr &&
          !passthrough_handler_(context_, buffer_, frame_length)) {
        ++counters_.passthrough_overflow_total;
        return false;
      }
      ++counters_.passthrough_total;
    }
    drop(frame_length);
  }
  return true;
}

bool ProductDownlinkRouter::feed(const uint8_t* bytes, uint16_t length) {
  if (bytes == nullptr && length != 0) return false;
  for (uint16_t index = 0; index < length; ++index) {
    if (length_ == kBufferCapacity) {
      ++counters_.framing_failure_total;
      drop(1);
    }
    buffer_[length_++] = bytes[index];
    if (!process()) return false;
  }
  return true;
}

}  // namespace csm::board::uplink
