#include "board/HostDownlinkParser.h"

#include <cstring>

#include "protocol/TypedFrame.h"

namespace csm::board {

HostDownlinkParser::HostDownlinkParser(FrameHandler frame_handler,
                                       CrcFailureHandler crc_failure_handler,
                                       void* ctx)
    : frame_handler_(frame_handler), crc_failure_handler_(crc_failure_handler), ctx_(ctx) {}

void HostDownlinkParser::reset() {
  len_ = 0;
}

void HostDownlinkParser::drop(uint16_t count) {
  if (count >= len_) {
    len_ = 0;
    return;
  }
  memmove(buffer_, buffer_ + count, len_ - count);
  len_ -= count;
}

void HostDownlinkParser::process() {
  while (len_ >= 2) {
    uint16_t start = 0;
    while (start + 1 < len_ &&
           !(buffer_[start] == kFrameSof0 && buffer_[start + 1] == kFrameSof1)) {
      start++;
    }
    if (start > 0) {
      drop(start);
    }
    if (len_ < 9) {
      return;
    }
    if (buffer_[0] != kFrameSof0 || buffer_[1] != kFrameSof1) {
      drop(1);
      continue;
    }

    const uint16_t payload_len = rd_u16_le(&buffer_[7]);
    if (payload_len > kMaxPayloadLen) {
      drop(1);
      continue;
    }

    const uint16_t frame_len = static_cast<uint16_t>(2 + 1 + 1 + 1 + 2 + 2 + payload_len + 2);
    if (len_ < frame_len) {
      return;
    }

    const uint16_t expected_crc = rd_u16_le(&buffer_[frame_len - 2]);
    const uint16_t actual_crc = crc16_ccitt(&buffer_[2], static_cast<size_t>(frame_len - 4));
    if (expected_crc != actual_crc) {
      if (crc_failure_handler_ != nullptr) {
        crc_failure_handler_(ctx_);
      }
      drop(1);
      continue;
    }

    if (frame_handler_ != nullptr) {
      frame_handler_(ctx_, buffer_[2], buffer_[3], rd_u16_le(&buffer_[5]),
                     &buffer_[9], payload_len);
    }
    drop(frame_len);
  }
}

void HostDownlinkParser::service(Stream& stream, int budget) {
  while (budget-- > 0 && stream.available() > 0) {
    const int value = stream.read();
    if (value < 0) {
      break;
    }
    if (len_ >= kBufferSize) {
      drop(1);
    }
    buffer_[len_++] = static_cast<uint8_t>(value);
  }
  process();
}

}  // namespace csm::board
