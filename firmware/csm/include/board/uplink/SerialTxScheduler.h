#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/uplink/UplinkPriority.h"
#include "protocol/TypedFrame.h"

namespace csm::board::uplink {

enum class SerialTxAdmissionResult : uint8_t {
  Accept = 0,
  NotReady,
  NoSpace,
};

struct SerialTxSchedulerConfig {
  uint32_t drain_time_budget_us = 0;
  uint32_t max_writes_per_pump = 0;
  uint32_t max_bytes_per_pump = 0;
};

struct SerialTxCounters {
  uint32_t enqueue_fail_total = 0;
  uint32_t ring_clear_total = 0;
  uint32_t ring_cleared_bytes_total = 0;
  uint32_t backpressure_total = 0;
  uint32_t high_water_bytes = 0;
  uint32_t last_requested_bytes = 0;
  uint32_t last_actual_bytes = 0;
  uint32_t requested_bytes_total = 0;
  uint32_t actual_bytes_total = 0;
  uint32_t actual_zero_total = 0;
  uint32_t partial_write_total = 0;
  uint32_t write_attempt_total = 0;
  uint32_t write_complete_total = 0;
  uint32_t backpressure_max_duration_ms = 0;
};

struct SerialTxServiceResult {
  uint32_t requested_bytes = 0;
  uint32_t actual_bytes = 0;
  uint32_t writes_attempted = 0;
  uint32_t writes_completed = 0;
  uint32_t partial_writes = 0;
  bool backpressure_event = false;
  uint32_t backpressure_duration_ms = 0;
};

class SerialTxScheduler {
 public:
  void begin(const SerialTxSchedulerConfig& config);

  uint32_t queuedBytes() const;
  uint32_t freeBytes() const;
  uint32_t capacityBytes() const { return kFrameCapacity; }
  uint32_t normalLimitBytes() const { return kFrameCapacity; }
  bool backpressureActive() const { return blocked_since_ms_ != 0; }
  bool hasActiveFrame() const { return frame_len_ > frame_offset_; }

  SerialTxAdmissionResult admissionResult(uint32_t len, UplinkPriority priority) const;
  bool canEnqueue(uint32_t len, UplinkPriority priority) const;
  void noteAdmissionFailure(uint32_t len, UplinkPriority priority);
  bool enqueueAtomic(const uint8_t* data, uint32_t len, UplinkPriority priority);
  void discardActiveFrame();
  SerialTxServiceResult service(uint32_t byte_budget, uint32_t now_ms, uint32_t now_us);

  const SerialTxCounters& counters() const { return counters_; }

 private:
  static constexpr uint32_t kFrameCapacity =
      static_cast<uint32_t>(csm::encoded_typed_frame_len(csm::kMaxPayloadLen));

  uint8_t frame_[kFrameCapacity] = {};
  uint32_t frame_len_ = 0;
  uint32_t frame_offset_ = 0;
  uint32_t blocked_since_ms_ = 0;
  SerialTxSchedulerConfig config_;
  SerialTxCounters counters_;

  void resetFrame();
  void noteBackpressure(uint32_t now_ms, SerialTxServiceResult& result);
};

}  // namespace csm::board::uplink
