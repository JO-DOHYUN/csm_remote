#pragma once

#include <stdint.h>

namespace csm::board::authority {

static constexpr uint8_t kAuthorityNoBus = 0xFF;

enum class AutonomyAuthorityState : uint8_t {
  Unknown = 0,
  InactiveConfirmed = 1,
  ActiveConfirmed = 2,
  RecentlyActive = 3,
  Ambiguous = 4,
  ProtocolFault = 5,
};

enum class AuthorityState : uint8_t {
  BootInhibit = 0,
  ObserveOnly = 1,
  AutonomyActiveLock = 2,
  AutonomyRecentlyActive = 3,
  AutonomyAmbiguous = 4,
  AutonomyProtocolFault = 5,
  LocalHandoffPending = 6,
  LocalReady = 7,
  RemoteActive = 8,
  HostServiceActive = 9,
  FaultLockout = 10,
  Estop = 11,
};

enum class ControlSourceId : uint8_t {
  None = 0,
  Remote = 1,
  HostService = 2,
  TestOnly = 3,
  SafetyNeutral = 4,
};

enum class ControlDecisionCode : uint8_t {
  Accepted = 0,
  RejectedBuildProfile = 1,
  RejectedAutonomyActive = 2,
  RejectedAutonomyRecent = 3,
  RejectedAutonomyUnknown = 4,
  RejectedAutonomyAmbiguous = 5,
  RejectedAutonomyProtocolFault = 6,
  RejectedLocalTxInhibit = 7,
  RejectedSafetySupervisor = 8,
  RejectedSourceStale = 9,
  RejectedSourceFailsafe = 10,
  RejectedNotNeutral = 11,
  RejectedNoTakeover = 12,
  RejectedRateLimit = 13,
  RejectedFramePolicy = 14,
  RejectedFaultLockout = 16,
};

struct AutonomyCommandProfile {
  uint8_t bus = kAuthorityNoBus;
  uint32_t can_id = 0;
  uint32_t can_id_mask = 0;
  uint8_t dlc_min = 0;
  uint8_t dlc_max = 0;
  bool has_mode_bit = false;
  uint8_t mode_offset = 0;
  uint8_t mode_mask = 0;
  bool has_alive_counter = false;
  uint8_t alive_offset = 0;
  uint8_t alive_bits = 0;
  uint16_t expected_period_ms = 0;
  uint16_t active_timeout_ms = 0;
  uint16_t release_quiet_ms = 0;
};

struct AuthorityDecision {
  ControlDecisionCode code = ControlDecisionCode::RejectedAutonomyUnknown;
  ControlSourceId source = ControlSourceId::None;
  AutonomyAuthorityState autonomy_state = AutonomyAuthorityState::Unknown;
  AuthorityState authority_state = AuthorityState::BootInhibit;
  uint16_t detail = 0;
};

constexpr bool isAutonomyReleased(AutonomyAuthorityState state) {
  return state == AutonomyAuthorityState::InactiveConfirmed;
}

constexpr bool mayConsiderLocalControl(AutonomyAuthorityState state) {
  return isAutonomyReleased(state);
}

}  // namespace csm::board::authority
