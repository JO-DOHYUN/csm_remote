#include "board/control/HostCommandFreshness.h"

namespace csm::board::control {

bool HostCommandFreshness::begin(
    const HostCommandFreshnessConfig& config) {
  configured_ = config.heartbeat_max_extra_lag_ms > 0 &&
      config.heartbeat_max_extra_lag_ms < 0x80000000u &&
      config.command_max_age_ms > 0 &&
      config.command_max_age_ms < 0x80000000u &&
      config.clock_future_tolerance_ms < 0x80000000u;
  config_ = configured_ ? config : HostCommandFreshnessConfig{};
  reset();
  return configured_;
}

void HostCommandFreshness::reset() {
  anchor_valid_ = false;
  qualified_ = false;
  fault_latched_ = false;
  anchor_host_mono_ms_ = 0;
  anchor_arrival_ms_ = 0;
  last_heartbeat_host_mono_ms_ = 0;
  last_heartbeat_arrival_ms_ = 0;
  last_command_id_ = 0;
}

HostFreshnessResult HostCommandFreshness::acceptHeartbeat(
    uint32_t command_id, uint32_t host_mono_ms, uint32_t arrival_ms) {
  if (!configured_ || fault_latched_) {
    return HostFreshnessResult::FaultLatched;
  }
  if (!anchor_valid_) {
    establishAnchor(command_id, host_mono_ms, arrival_ms);
    return HostFreshnessResult::AnchorEstablished;
  }
  if (!forward(last_heartbeat_host_mono_ms_, host_mono_ms) ||
      !forward(last_command_id_, command_id)) {
    return latchFault();
  }

  const uint32_t sender_elapsed =
      host_mono_ms - last_heartbeat_host_mono_ms_;
  const uint32_t arrival_elapsed =
      arrival_ms - last_heartbeat_arrival_ms_;
  if (static_cast<uint64_t>(arrival_elapsed) >
          static_cast<uint64_t>(sender_elapsed) +
              config_.heartbeat_max_extra_lag_ms ||
      static_cast<uint64_t>(sender_elapsed) >
          static_cast<uint64_t>(arrival_elapsed) +
              config_.clock_future_tolerance_ms) {
    return latchFault();
  }

  anchor_host_mono_ms_ = host_mono_ms;
  anchor_arrival_ms_ = arrival_ms;
  last_heartbeat_host_mono_ms_ = host_mono_ms;
  last_heartbeat_arrival_ms_ = arrival_ms;
  last_command_id_ = command_id;
  qualified_ = true;
  return HostFreshnessResult::Accepted;
}

HostFreshnessResult HostCommandFreshness::acceptCommand(
    uint32_t command_id, uint32_t host_mono_ms, uint32_t arrival_ms) {
  if (!configured_ || fault_latched_) {
    return HostFreshnessResult::FaultLatched;
  }
  if (!anchor_valid_ || !qualified_) {
    return HostFreshnessResult::NotQualified;
  }
  if (!forward(last_command_id_, command_id)) {
    return HostFreshnessResult::Replay;
  }

  const uint32_t estimated_host_now =
      anchor_host_mono_ms_ + (arrival_ms - anchor_arrival_ms_);
  const int32_t signed_age =
      static_cast<int32_t>(estimated_host_now - host_mono_ms);
  if (signed_age > static_cast<int32_t>(config_.command_max_age_ms)) {
    return HostFreshnessResult::Stale;
  }
  if (signed_age <
      -static_cast<int32_t>(config_.clock_future_tolerance_ms)) {
    return HostFreshnessResult::Future;
  }

  last_command_id_ = command_id;
  return HostFreshnessResult::Accepted;
}

bool HostCommandFreshness::forward(uint32_t previous, uint32_t current) {
  const uint32_t delta = current - previous;
  return delta != 0 && delta < 0x80000000u;
}

void HostCommandFreshness::establishAnchor(
    uint32_t command_id, uint32_t host_mono_ms, uint32_t arrival_ms) {
  anchor_valid_ = true;
  anchor_host_mono_ms_ = host_mono_ms;
  anchor_arrival_ms_ = arrival_ms;
  last_heartbeat_host_mono_ms_ = host_mono_ms;
  last_heartbeat_arrival_ms_ = arrival_ms;
  last_command_id_ = command_id;
}

HostFreshnessResult HostCommandFreshness::latchFault() {
  fault_latched_ = true;
  qualified_ = false;
  return HostFreshnessResult::FaultLatched;
}

}  // namespace csm::board::control
