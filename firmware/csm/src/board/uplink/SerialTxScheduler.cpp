#include "board/uplink/SerialTxScheduler.h"

#include <Arduino.h>
#include <string.h>

#if defined(SERIAL_CDC)
#include "USB/PluggableUSBSerial.h"
#endif

#ifndef BOARD_SERIAL_TX_CHUNK_BYTES
#define BOARD_SERIAL_TX_CHUNK_BYTES 512
#endif

namespace csm::board::uplink {

void SerialTxScheduler::begin(const SerialTxSchedulerConfig& config) {
  config_ = config;
  frame_len_ = 0;
  frame_offset_ = 0;
  blocked_since_ms_ = 0;
  counters_ = {};
}

uint32_t SerialTxScheduler::queuedBytes() const {
  if (!hasActiveFrame()) {
    return 0;
  }
  return frame_len_ - frame_offset_;
}

uint32_t SerialTxScheduler::freeBytes() const {
  return hasActiveFrame() ? 0 : kFrameCapacity;
}

SerialTxAdmissionResult SerialTxScheduler::admissionResult(
    uint32_t len, UplinkPriority /*priority*/) const {
  if (len == 0 || len > kFrameCapacity) {
    return SerialTxAdmissionResult::NoSpace;
  }
  if (hasActiveFrame()) {
    return SerialTxAdmissionResult::NotReady;
  }
  return SerialTxAdmissionResult::Accept;
}

bool SerialTxScheduler::canEnqueue(uint32_t len, UplinkPriority priority) const {
  return admissionResult(len, priority) == SerialTxAdmissionResult::Accept;
}

void SerialTxScheduler::noteAdmissionFailure(uint32_t len, UplinkPriority priority) {
  switch (admissionResult(len, priority)) {
    case SerialTxAdmissionResult::NotReady:
    case SerialTxAdmissionResult::NoSpace:
      counters_.enqueue_fail_total++;
      break;
    case SerialTxAdmissionResult::Accept:
    default:
      break;
  }
}

bool SerialTxScheduler::enqueueAtomic(const uint8_t* data, uint32_t len,
                                      UplinkPriority priority) {
  if (data == nullptr) {
    counters_.enqueue_fail_total++;
    return false;
  }
  if (!canEnqueue(len, priority)) {
    noteAdmissionFailure(len, priority);
    return false;
  }

  memcpy(frame_, data, len);
  frame_len_ = len;
  frame_offset_ = 0;
  if (frame_len_ > counters_.high_water_bytes) {
    counters_.high_water_bytes = frame_len_;
  }
  return true;
}

void SerialTxScheduler::discardActiveFrame() {
  resetFrame();
  blocked_since_ms_ = 0;
}

SerialTxServiceResult SerialTxScheduler::service(uint32_t byte_budget, uint32_t now_ms,
                                                 uint32_t now_us) {
  SerialTxServiceResult result;

#if defined(SERIAL_CDC)
  if (byte_budget == 0 || !hasActiveFrame()) {
    return result;
  }

  const uint32_t start_us = now_us;
  const uint32_t configured_bytes =
      config_.max_bytes_per_pump == 0 ? byte_budget : config_.max_bytes_per_pump;
  const uint32_t pump_budget = byte_budget < configured_bytes ? byte_budget : configured_bytes;
  const uint32_t max_writes =
      config_.max_writes_per_pump == 0 ? 0xFFFFFFFFu : config_.max_writes_per_pump;
  uint32_t writes = 0;

  while (hasActiveFrame() && result.actual_bytes < pump_budget && writes < max_writes) {
    if (config_.drain_time_budget_us > 0 &&
        static_cast<uint32_t>(micros() - start_us) >= config_.drain_time_budget_us) {
      break;
    }

    uint32_t n = queuedBytes();
    const uint32_t budget_remaining = pump_budget - result.actual_bytes;
    if (n > budget_remaining) {
      n = budget_remaining;
    }
    if (n > BOARD_SERIAL_TX_CHUNK_BYTES) {
      n = BOARD_SERIAL_TX_CHUNK_BYTES;
    }
    if (n == 0) {
      break;
    }

    uint32_t actual = 0;
    _SerialUSB.send_nb(&frame_[frame_offset_], n, &actual, true);
    if (actual > n) {
      actual = n;
    }
    writes++;
    counters_.write_attempt_total++;
    result.writes_attempted++;
    counters_.last_requested_bytes = n;
    counters_.last_actual_bytes = actual;
    counters_.requested_bytes_total += n;
    counters_.actual_bytes_total += actual;
    result.requested_bytes += n;
    result.actual_bytes += actual;

    if (actual == 0) {
      counters_.actual_zero_total++;
      noteBackpressure(now_ms, result);
      break;
    }

    frame_offset_ += actual;

    if (actual == n) {
      counters_.write_complete_total++;
      result.writes_completed++;
    } else {
      counters_.partial_write_total++;
      result.partial_writes++;
      noteBackpressure(now_ms, result);
      break;
    }

    if (blocked_since_ms_ != 0) {
      const uint32_t duration = now_ms - blocked_since_ms_;
      if (duration > counters_.backpressure_max_duration_ms) {
        counters_.backpressure_max_duration_ms = duration;
      }
      blocked_since_ms_ = 0;
      result.backpressure_event = true;
      result.backpressure_duration_ms = duration;
    }

    if (!hasActiveFrame()) {
      resetFrame();
    }
  }
#else
  (void)byte_budget;
  (void)now_ms;
  (void)now_us;
#endif
  return result;
}

void SerialTxScheduler::resetFrame() {
  frame_len_ = 0;
  frame_offset_ = 0;
}

void SerialTxScheduler::noteBackpressure(uint32_t now_ms,
                                         SerialTxServiceResult& result) {
  if (blocked_since_ms_ == 0) {
    blocked_since_ms_ = now_ms;
    counters_.backpressure_total++;
  } else {
    const uint32_t duration = now_ms - blocked_since_ms_;
    if (duration > counters_.backpressure_max_duration_ms) {
      counters_.backpressure_max_duration_ms = duration;
    }
    result.backpressure_duration_ms = duration;
  }
}

}  // namespace csm::board::uplink
