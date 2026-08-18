#include "board/can/CanTxCadenceQueue.h"

namespace csm::board::can {
namespace {

constexpr uint32_t kExtendedFlag = 1u << 29;
constexpr uint32_t kSupportedFlags = kExtendedFlag;

}  // namespace

bool CanTxCadenceQueue::begin(const CanTxCadenceLaneConfig* configs,
                              uint8_t lane_count) {
  configured_ = false;
  lane_count_ = 0;
  for (Lane& lane : lanes_) lane = {};
  if (configs == nullptr || lane_count == 0 || lane_count > kMaxLanes) {
    return false;
  }

  for (uint8_t index = 0; index < lane_count; ++index) {
    const CanTxCadenceLaneConfig& config = configs[index];
    BuiltinCanTxFrame shape;
    shape.bus = config.bus;
    shape.can_id_flags = config.can_id_flags;
    shape.dlc = config.dlc;
    if (!validFrame(shape) || config.period_us == 0 ||
        config.period_us >= 0x80000000u ||
        config.first_phase_us >= 0x80000000u ||
        config.first_phase_us > config.period_us) {
      return false;
    }
    for (uint8_t previous = 0; previous < index; ++previous) {
      if (configs[previous].bus == config.bus &&
          configs[previous].can_id_flags == config.can_id_flags &&
          configs[previous].dlc == config.dlc) {
        return false;
      }
    }
    lanes_[index].config = config;
  }

  lane_count_ = lane_count;
  configured_ = true;
  return true;
}

CanTxCadenceEnqueueCode CanTxCadenceQueue::enqueue(
    const BuiltinCanTxFrame& frame, uint32_t now_us) {
  if (!configured_ || !validFrame(frame)) {
    return CanTxCadenceEnqueueCode::InvalidFrame;
  }

  Lane* target = nullptr;
  for (uint8_t index = 0; index < lane_count_; ++index) {
    if (frameMatches(frame, lanes_[index].config)) {
      target = &lanes_[index];
      break;
    }
  }
  if (target == nullptr) {
    return CanTxCadenceEnqueueCode::UnsupportedLane;
  }
  if (target->count >= kLaneCapacity) {
    return CanTxCadenceEnqueueCode::QueueFull;
  }

  const bool idle = target->count == 0 && !target->in_flight;
  target->q[target->tail] = frame;
  target->tail = static_cast<uint8_t>((target->tail + 1u) % kLaneCapacity);
  ++target->count;
  if (idle &&
      (!target->started || timeReached(now_us, target->next_due_us))) {
    target->started = true;
    target->next_due_us = now_us + target->config.first_phase_us;
  }
  return CanTxCadenceEnqueueCode::Accepted;
}

bool CanTxCadenceQueue::peekDue(uint8_t lane, uint32_t now_us,
                                BuiltinCanTxFrame* out) const {
  if (!configured_ || lane >= lane_count_ || out == nullptr) return false;
  const Lane& selected = lanes_[lane];
  if (selected.count == 0 || selected.in_flight ||
      !timeReached(now_us, selected.next_due_us)) {
    return false;
  }
  *out = selected.q[selected.head];
  return true;
}

bool CanTxCadenceQueue::noteHwEnqueueAccepted(uint8_t lane,
                                              uint32_t command_id,
                                              uint32_t now_us) {
  if (!configured_ || lane >= lane_count_) return false;
  Lane& selected = lanes_[lane];
  if (selected.count == 0 || selected.in_flight ||
      selected.q[selected.head].command_id != command_id) {
    return false;
  }
  selected.head =
      static_cast<uint8_t>((selected.head + 1u) % kLaneCapacity);
  --selected.count;
  selected.in_flight = true;
  selected.in_flight_command_id = command_id;
  selected.started = true;
  selected.next_due_us = now_us + selected.config.period_us;
  return true;
}

bool CanTxCadenceQueue::noteCompletion(
    const BuiltinCanTxCompletion& completion, uint32_t now_us) {
  if (!configured_ ||
      completion.frame.origin != BuiltinCanTxOrigin::HostControl) {
    return false;
  }
  for (uint8_t index = 0; index < lane_count_; ++index) {
    Lane& lane = lanes_[index];
    if (!lane.in_flight ||
        lane.in_flight_command_id != completion.frame.command_id ||
        !frameMatches(completion.frame, lane.config)) {
      continue;
    }
    if (!completion.terminal) {
      return completion.code ==
             BuiltinCanTxCompletionCode::DeadlineExceededPending;
    }
    lane.in_flight = false;
    lane.in_flight_command_id = 0;
    if (timeReached(now_us, lane.next_due_us)) {
      lane.next_due_us = now_us + lane.config.period_us;
    }
    return true;
  }
  return false;
}

bool CanTxCadenceQueue::dropHead(uint8_t lane, BuiltinCanTxFrame* out) {
  if (!configured_ || lane >= lane_count_) return false;
  Lane& selected = lanes_[lane];
  if (selected.count == 0) return false;
  if (out != nullptr) *out = selected.q[selected.head];
  selected.head =
      static_cast<uint8_t>((selected.head + 1u) % kLaneCapacity);
  --selected.count;
  return true;
}

void CanTxCadenceQueue::flushPending() {
  for (uint8_t index = 0; index < lane_count_; ++index) {
    Lane& lane = lanes_[index];
    lane.head = 0;
    lane.tail = 0;
    lane.count = 0;
  }
}

bool CanTxCadenceQueue::resetIfIdle() {
  if (hasInFlight()) return false;
  flushPending();
  for (uint8_t index = 0; index < lane_count_; ++index) {
    lanes_[index].started = false;
    lanes_[index].next_due_us = 0;
    lanes_[index].in_flight_command_id = 0;
  }
  return configured_;
}

bool CanTxCadenceQueue::hasInFlight() const {
  for (uint8_t index = 0; index < lane_count_; ++index) {
    if (lanes_[index].in_flight) return true;
  }
  return false;
}

uint16_t CanTxCadenceQueue::depth() const {
  uint16_t total = 0;
  for (uint8_t index = 0; index < lane_count_; ++index) {
    total = static_cast<uint16_t>(total + lanes_[index].count);
  }
  return total;
}

bool CanTxCadenceQueue::timeReached(uint32_t now_us, uint32_t due_us) {
  return static_cast<int32_t>(now_us - due_us) >= 0;
}

bool CanTxCadenceQueue::validFrame(const BuiltinCanTxFrame& frame) {
  if (frame.bus == 0xFFu || frame.dlc > 8u ||
      (frame.can_id_flags & ~(0x1FFFFFFFu | kSupportedFlags)) != 0) {
    return false;
  }
  const bool extended = (frame.can_id_flags & kExtendedFlag) != 0;
  const uint32_t can_id = frame.can_id_flags & 0x1FFFFFFFu;
  return extended || can_id <= 0x7FFu;
}

bool CanTxCadenceQueue::frameMatches(
    const BuiltinCanTxFrame& frame,
    const CanTxCadenceLaneConfig& config) {
  return frame.bus == config.bus &&
         frame.can_id_flags == config.can_id_flags &&
         frame.dlc == config.dlc;
}

}  // namespace csm::board::can
