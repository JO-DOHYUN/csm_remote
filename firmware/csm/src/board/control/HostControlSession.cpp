#include "board/control/HostControlSession.h"

#include "protocol/ControlProtocol.h"

namespace csm::board::control {
namespace {
constexpr uint32_t kHeartbeatTimeoutMs = 300u;
constexpr uint16_t kDefaultLeaseMs = 500u;
constexpr uint16_t kMaxLeaseMs = 2000u;

void incrementSaturating(uint32_t* value) {
  if (*value != UINT32_MAX) ++*value;
}
}  // namespace

void HostControlSession::begin(uint32_t now_ms) {
  has_heartbeat_ = false;
  armed_ = false;
  last_heartbeat_ms_ = now_ms;
  heartbeat_max_gap_ms_ = 0u;
  lease_until_ms_ = now_ms;
  timeout_count_ = 0u;
  activation_epoch_ = 0u;
}

void HostControlSession::update(uint32_t now_ms) {
  if (!armed_) return;
  if (!heartbeatAlive(now_ms) || !leaseAlive(now_ms)) {
    armed_ = false;
    lease_until_ms_ = now_ms;
    incrementSaturating(&timeout_count_);
  }
}

void HostControlSession::heartbeat(uint32_t now_ms) {
  if (has_heartbeat_) {
    const uint32_t gap = now_ms - last_heartbeat_ms_;
    if (gap > heartbeat_max_gap_ms_) heartbeat_max_gap_ms_ = gap;
  }
  has_heartbeat_ = true;
  last_heartbeat_ms_ = now_ms;
}

uint8_t HostControlSession::arm(uint32_t now_ms, uint16_t lease_ms,
                                bool backend_ready) {
  if (!heartbeatAlive(now_ms)) return ControlReasonHostTimeout;
  if (!backend_ready) return ControlReasonCanNotReady;
  armed_ = true;
  lease_until_ms_ = now_ms + normalizeLease(lease_ms);
  ++activation_epoch_;
  if (activation_epoch_ == 0u) activation_epoch_ = 1u;
  return ControlReasonOk;
}

uint8_t HostControlSession::renew(uint32_t now_ms, uint16_t lease_ms) {
  if (!heartbeatAlive(now_ms)) {
    disarm(now_ms);
    return ControlReasonHostTimeout;
  }
  if (!armed_) return ControlReasonNotArmed;
  lease_until_ms_ = now_ms + normalizeLease(lease_ms);
  return ControlReasonOk;
}

void HostControlSession::disarm(uint32_t now_ms) {
  armed_ = false;
  lease_until_ms_ = now_ms;
}

void HostControlSession::invalidate(uint32_t now_ms) {
  has_heartbeat_ = false;
  disarm(now_ms);
  last_heartbeat_ms_ = now_ms;
}

bool HostControlSession::heartbeatAlive(uint32_t now_ms) const {
  return has_heartbeat_ && now_ms - last_heartbeat_ms_ <= kHeartbeatTimeoutMs;
}

bool HostControlSession::leaseAlive(uint32_t now_ms) const {
  return armed_ && static_cast<int32_t>(lease_until_ms_ - now_ms) > 0;
}

bool HostControlSession::canAccept(uint32_t now_ms, bool backend_ready,
                                   uint8_t* reason) const {
  uint8_t result = ControlReasonOk;
  if (!heartbeatAlive(now_ms)) result = ControlReasonHostTimeout;
  else if (!armed_) result = ControlReasonNotArmed;
  else if (!leaseAlive(now_ms)) result = ControlReasonControlLeaseExpired;
  else if (!backend_ready) result = ControlReasonCanNotReady;
  if (reason != nullptr) *reason = result;
  return result == ControlReasonOk;
}

uint32_t HostControlSession::heartbeatAgeMs(uint32_t now_ms) const {
  return has_heartbeat_ ? now_ms - last_heartbeat_ms_ : UINT32_MAX;
}

uint32_t HostControlSession::leaseRemainingMs(uint32_t now_ms) const {
  return leaseAlive(now_ms) ? lease_until_ms_ - now_ms : 0u;
}

uint16_t HostControlSession::normalizeLease(uint16_t lease_ms) const {
  if (lease_ms == 0u) return kDefaultLeaseMs;
  return lease_ms > kMaxLeaseMs ? kMaxLeaseMs : lease_ms;
}

}  // namespace csm::board::control
