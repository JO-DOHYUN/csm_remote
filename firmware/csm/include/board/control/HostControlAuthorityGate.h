#pragma once

#include <stdint.h>

namespace csm::board::control {

enum class HostControlPhase : uint8_t {
  Inactive = 0,
  Active = 1,
  Draining = 2,
};

enum class HostControlCloseReason : uint8_t {
  None = 0,
  HostDisarm = 1,
  TransportEpochClosed = 2,
  AuthorityPreempted = 3,
  LeaseExpired = 4,
  FreshnessFault = 6,
};

// Single owner for the Host-to-RC exclusion state. It owns no CAN frame and
// no lease timer: the caller closes admission and the lease first, requests
// HW cancellation, and keeps RC blocked until the Host journal reaches zero.
class HostControlAuthorityGate {
 public:
  void reset();
  bool activate(bool authority_active, bool authority_allowed,
                uint8_t active_control_slots);
  bool beginClose(HostControlCloseReason reason,
                  uint8_t active_host_slots);
  void observeHostSlots(uint8_t active_host_slots);

  HostControlPhase phase() const { return phase_; }
  HostControlCloseReason closeReason() const { return close_reason_; }
  bool admissionOpen() const { return phase_ == HostControlPhase::Active; }
  bool rcAllowed() const { return phase_ == HostControlPhase::Inactive; }

 private:
  HostControlPhase phase_ = HostControlPhase::Inactive;
  HostControlCloseReason close_reason_ = HostControlCloseReason::None;
};

}  // namespace csm::board::control
