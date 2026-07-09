#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace csm::board {

class HostDownlinkParser {
 public:
  using FrameHandler = void (*)(void* ctx, uint8_t version, uint8_t record_type,
                                uint16_t seq, const uint8_t* payload, uint16_t len);
  using CrcFailureHandler = void (*)(void* ctx);

  HostDownlinkParser(FrameHandler frame_handler, CrcFailureHandler crc_failure_handler,
                     void* ctx = nullptr);

  void reset();
  void service(Stream& stream, int budget);

 private:
  void drop(uint16_t count);
  void process();

  static constexpr uint16_t kBufferSize = 192;

  FrameHandler frame_handler_ = nullptr;
  CrcFailureHandler crc_failure_handler_ = nullptr;
  void* ctx_ = nullptr;
  uint8_t buffer_[kBufferSize] = {};
  uint16_t len_ = 0;
};

}  // namespace csm::board
