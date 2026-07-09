#include "board/SafetySupervisor.h"

#include "protocol/TypedRecords.h"

namespace csm::board {

namespace {
constexpr uint32_t kHeartbeatTimeoutMs = 300;
constexpr uint32_t kControlActiveHoldMs = 100;
constexpr uint16_t kDefaultLeaseMs = 500;
constexpr uint16_t kMaxLeaseMs = 2000;
}  // namespace

void SafetySupervisor::begin(uint32_t now_ms) {
  has_heartbeat_ = false;
  armed_ = false;
  fault_lockout_ = false;
  last_heartbeat_ms_ = now_ms;
  lease_until_ms_ = now_ms;
  last_control_tx_ms_ = now_ms;
  previous_state_ = SafetyState::Boot;
  state_ = SafetyState::MonitorOnly;
  transition_counter_ = 1;
}

void SafetySupervisor::setState(SafetyState state) {
  if (state_ == state) {
    return;
  }
  previous_state_ = state_;
  state_ = state;
  transition_counter_++;
}

uint16_t SafetySupervisor::normalizeLease(uint16_t lease_ms) const {
  if (lease_ms == 0) {
    return kDefaultLeaseMs;
  }
  return lease_ms > kMaxLeaseMs ? kMaxLeaseMs : lease_ms;
}

bool SafetySupervisor::heartbeatAlive(uint32_t now_ms) const {
  return has_heartbeat_ && static_cast<uint32_t>(now_ms - last_heartbeat_ms_) <= kHeartbeatTimeoutMs;
}

bool SafetySupervisor::leaseAlive(uint32_t now_ms) const {
  return armed_ && static_cast<int32_t>(lease_until_ms_ - now_ms) > 0;
}

uint32_t SafetySupervisor::heartbeatAgeMs(uint32_t now_ms) const {
  if (!has_heartbeat_) {
    return 0xFFFFFFFFu;
  }
  return now_ms - last_heartbeat_ms_;
}

uint32_t SafetySupervisor::leaseRemainingMs(uint32_t now_ms) const {
  if (!armed_ || static_cast<int32_t>(lease_until_ms_ - now_ms) <= 0) {
    return 0;
  }
  return lease_until_ms_ - now_ms;
}

uint8_t SafetySupervisor::faultBits() const {
  uint8_t bits = 0;
  bits |= inputs_.estop_asserted ? (1u << 0) : 0;
  bits |= inputs_.field_power_ok ? 0 : (1u << 1);
  bits |= inputs_.encoder_fault ? (1u << 2) : 0;
  bits |= fault_lockout_ ? (1u << 3) : 0;
  bits |= has_heartbeat_ ? 0 : (1u << 4);
  return bits;
}

void SafetySupervisor::update(uint32_t now_ms, const SafetyInputs& inputs) {
  inputs_ = inputs;

  if (inputs_.estop_asserted) {
    armed_ = false;
    setState(SafetyState::Estop);
    return;
  }

  if (!inputs_.field_power_ok || inputs_.encoder_fault || fault_lockout_) {
    armed_ = false;
    setState(SafetyState::FaultLockout);
    return;
  }

  if (armed_ && !heartbeatAlive(now_ms)) {
    armed_ = false;
    setState(SafetyState::HostTimeout);
    return;
  }

  if (armed_ && !leaseAlive(now_ms)) {
    armed_ = false;
  }

  if (armed_) {
    if (now_ms - last_control_tx_ms_ <= kControlActiveHoldMs) {
      setState(SafetyState::ControlActive);
    } else {
      setState(SafetyState::Armed);
    }
  } else if (heartbeatAlive(now_ms)) {
    setState(SafetyState::Ready);
  } else {
    setState(SafetyState::MonitorOnly);
  }
}

uint8_t SafetySupervisor::heartbeat(uint32_t now_ms) {
  has_heartbeat_ = true;
  last_heartbeat_ms_ = now_ms;
  return ControlReasonOk;
}

uint8_t SafetySupervisor::arm(uint32_t now_ms, uint16_t lease_ms, bool control_backend_ready) {
  if (inputs_.estop_asserted) {
    return ControlReasonEstopAsserted;
  }
  if (!inputs_.field_power_ok) {
    return ControlReasonFieldPowerLost;
  }
  if (inputs_.encoder_fault) {
    return ControlReasonEncoderFault;
  }
  if (fault_lockout_) {
    return ControlReasonSafetyLockout;
  }
  if (!heartbeatAlive(now_ms)) {
    return ControlReasonHostTimeout;
  }
  if (!control_backend_ready) {
    return ControlReasonCanNotReady;
  }

  armed_ = true;
  lease_until_ms_ = now_ms + normalizeLease(lease_ms);
  last_control_tx_ms_ = now_ms - kControlActiveHoldMs - 1;
  setState(SafetyState::Armed);
  return ControlReasonOk;
}

uint8_t SafetySupervisor::renewLease(uint32_t now_ms, uint16_t lease_ms) {
  if (!heartbeatAlive(now_ms)) {
    armed_ = false;
    setState(SafetyState::HostTimeout);
    return ControlReasonHostTimeout;
  }
  if (!armed_) {
    return ControlReasonNotArmed;
  }
  lease_until_ms_ = now_ms + normalizeLease(lease_ms);
  return ControlReasonOk;
}

void SafetySupervisor::disarm(uint32_t now_ms) {
  armed_ = false;
  lease_until_ms_ = now_ms;
  setState(heartbeatAlive(now_ms) ? SafetyState::Ready : SafetyState::MonitorOnly);
}

void SafetySupervisor::clearFaultLockout(uint32_t now_ms) {
  fault_lockout_ = false;
  armed_ = false;
  setState(heartbeatAlive(now_ms) ? SafetyState::Ready : SafetyState::MonitorOnly);
}

void SafetySupervisor::setFaultLockout(uint32_t now_ms) {
  (void)now_ms;
  fault_lockout_ = true;
  armed_ = false;
  setState(SafetyState::FaultLockout);
}

void SafetySupervisor::noteControlTx(uint32_t now_ms) {
  last_control_tx_ms_ = now_ms;
  if (armed_) {
    setState(SafetyState::ControlActive);
  }
}

bool SafetySupervisor::canAcceptTx(uint32_t now_ms, bool control_backend_ready, uint8_t* reason) const {
  uint8_t local_reason = ControlReasonOk;
  bool ok = false;

  if (inputs_.estop_asserted) {
    local_reason = ControlReasonEstopAsserted;
  } else if (!inputs_.field_power_ok) {
    local_reason = ControlReasonFieldPowerLost;
  } else if (inputs_.encoder_fault) {
    local_reason = ControlReasonEncoderFault;
  } else if (fault_lockout_) {
    local_reason = ControlReasonSafetyLockout;
  } else if (!heartbeatAlive(now_ms)) {
    local_reason = ControlReasonHostTimeout;
  } else if (!armed_) {
    local_reason = ControlReasonNotArmed;
  } else if (!leaseAlive(now_ms)) {
    local_reason = ControlReasonControlLeaseExpired;
  } else if (!control_backend_ready) {
    local_reason = ControlReasonCanNotReady;
  } else {
    ok = true;
  }

  if (reason != nullptr) {
    *reason = local_reason;
  }
  return ok;
}

bool SafetySupervisor::canDriveTxGate() const {
  return armed_ &&
         !inputs_.estop_asserted &&
         inputs_.field_power_ok &&
         !inputs_.encoder_fault &&
         !fault_lockout_;
}

}  // namespace csm::board
