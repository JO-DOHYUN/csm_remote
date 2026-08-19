#include "board/control/HostControlAuthorityGate.h"

namespace csm::board::control {

void HostControlAuthorityGate::reset() {
  phase_ = HostControlPhase::Inactive;
  close_reason_ = HostControlCloseReason::None;
}

bool HostControlAuthorityGate::activate(bool lease_alive,
                                        bool authority_allowed,
                                        uint8_t active_host_slots) {
  if (phase_ != HostControlPhase::Inactive || !lease_alive ||
      !authority_allowed || active_host_slots != 0) {
    return false;
  }
  phase_ = HostControlPhase::Active;
  close_reason_ = HostControlCloseReason::None;
  return true;
}

bool HostControlAuthorityGate::beginClose(HostControlCloseReason reason,
                                          uint8_t active_host_slots) {
  if (reason == HostControlCloseReason::None) return false;
  const bool newly_closed = phase_ == HostControlPhase::Active;
  close_reason_ = reason;
  phase_ = active_host_slots == 0 ? HostControlPhase::Inactive
                                  : HostControlPhase::Draining;
  return newly_closed;
}

void HostControlAuthorityGate::observeHostSlots(uint8_t active_host_slots) {
  if (phase_ == HostControlPhase::Draining && active_host_slots == 0) {
    phase_ = HostControlPhase::Inactive;
  }
}

}  // namespace csm::board::control
