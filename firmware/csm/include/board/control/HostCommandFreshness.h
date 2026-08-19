#pragma once

#include <stdint.h>

namespace csm::board::control {

struct HostCommandFreshnessConfig {
  uint32_t heartbeat_max_extra_lag_ms = 0;
  uint32_t command_max_age_ms = 0;
  uint32_t clock_future_tolerance_ms = 0;
};

enum class HostFreshnessResult : uint8_t {
  Accepted = 0,
  AnchorEstablished = 1,
  NotQualified = 2,
  FaultLatched = 3,
  Stale = 4,
  Future = 5,
  Replay = 6,
};

// Fixed-state sender-time expiry and replay protection. This class never
// schedules, stores, retries, or otherwise owns an actuator command.
class HostCommandFreshness {
 public:
  bool begin(const HostCommandFreshnessConfig& config);
  void reset();

  HostFreshnessResult acceptHeartbeat(uint32_t command_id,
                                      uint32_t host_mono_ms,
                                      uint32_t arrival_ms);
  HostFreshnessResult acceptCommand(uint32_t command_id,
                                    uint32_t host_mono_ms,
                                    uint32_t arrival_ms);

  bool qualified() const { return qualified_; }
  bool timingQualified() const { return timing_qualified_; }
  bool faultLatched() const { return fault_latched_; }
  uint32_t observedHeartbeatExtraLagMs() const {
    return observed_heartbeat_extra_lag_ms_;
  }
  uint32_t observedCommandAgeMs() const { return observed_command_age_ms_; }
  uint32_t observedCommandFutureLeadMs() const {
    return observed_command_future_lead_ms_;
  }

 private:
  static bool forward(uint32_t previous, uint32_t current);
  void establishAnchor(uint32_t command_id, uint32_t host_mono_ms,
                       uint32_t arrival_ms);
  HostFreshnessResult latchFault();

  HostCommandFreshnessConfig config_ = {};
  bool configured_ = false;
  bool timing_qualified_ = false;
  bool anchor_valid_ = false;
  bool qualified_ = false;
  bool fault_latched_ = false;
  uint32_t anchor_host_mono_ms_ = 0;
  uint32_t anchor_arrival_ms_ = 0;
  uint32_t last_heartbeat_host_mono_ms_ = 0;
  uint32_t last_heartbeat_arrival_ms_ = 0;
  uint32_t last_command_id_ = 0;
  uint32_t observed_heartbeat_extra_lag_ms_ = 0;
  uint32_t observed_command_age_ms_ = 0;
  uint32_t observed_command_future_lead_ms_ = 0;
};

}  // namespace csm::board::control
