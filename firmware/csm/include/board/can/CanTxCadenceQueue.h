#pragma once

#include <stdint.h>

#include "board/can/BuiltinCanTxOwner.h"

namespace csm::board::can {

struct CanTxCadenceLaneConfig {
  uint8_t bus = 0xFFu;
  uint32_t can_id_flags = 0;
  uint8_t dlc = 0;
  uint32_t period_us = 0;
  uint32_t first_phase_us = 0;
};

enum class CanTxCadenceEnqueueCode : uint8_t {
  Accepted,
  UnsupportedLane,
  QueueFull,
  InvalidFrame,
};

// Fixed mechanical pacing for already-shaped CAN frames. The queue never
// interprets payload bytes or creates, replaces, or reorders commands.
class CanTxCadenceQueue {
 public:
  static constexpr uint8_t kMaxLanes = 3;
  static constexpr uint8_t kLaneCapacity = 8;
  static constexpr uint16_t kTotalCapacity =
      kMaxLanes * kLaneCapacity;

  bool begin(const CanTxCadenceLaneConfig* configs, uint8_t lane_count);
  CanTxCadenceEnqueueCode enqueue(const BuiltinCanTxFrame& frame,
                                  uint32_t now_us);
  bool peekDue(uint8_t lane, uint32_t now_us,
               BuiltinCanTxFrame* out) const;
  bool noteHwEnqueueAccepted(uint8_t lane, uint32_t command_id,
                             uint32_t now_us);
  bool noteCompletion(const BuiltinCanTxCompletion& completion,
                      uint32_t now_us);
  bool dropHead(uint8_t lane, BuiltinCanTxFrame* out = nullptr);
  void flushPending();
  bool resetIfIdle();
  bool hasInFlight() const;
  uint16_t depth() const;
  uint16_t capacity() const { return kTotalCapacity; }

 private:
  struct Lane {
    CanTxCadenceLaneConfig config = {};
    BuiltinCanTxFrame q[kLaneCapacity] = {};
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;
    bool started = false;
    bool in_flight = false;
    uint32_t in_flight_command_id = 0;
    uint32_t next_due_us = 0;
  };

  static bool timeReached(uint32_t now_us, uint32_t due_us);
  static bool validFrame(const BuiltinCanTxFrame& frame);
  static bool frameMatches(const BuiltinCanTxFrame& frame,
                           const CanTxCadenceLaneConfig& config);

  Lane lanes_[kMaxLanes] = {};
  uint8_t lane_count_ = 0;
  bool configured_ = false;
};

}  // namespace csm::board::can
