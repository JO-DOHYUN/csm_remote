#include "board/authority/AutonomyAuthorityMonitor.h"

namespace csm::board::authority {
namespace {

constexpr uint16_t kDetailInvalidProfile = 1;
constexpr uint16_t kDetailModeOffsetOutOfRange = 2;
constexpr uint16_t kDetailAliveOffsetOutOfRange = 3;
constexpr uint16_t kDetailReappearance = 4;

uint32_t rawCanId(uint32_t can_id_flags) {
  return can_id_flags & ((can_id_flags & (1u << 29)) ? 0x1FFFFFFFu : 0x7FFu);
}

}  // namespace

void AutonomyAuthorityMonitor::begin(uint32_t now_ms) {
  last_upstream_command_ms_ = now_ms;
  quiet_window_satisfied_ = false;
  local_tx_inhibit_latched_ = false;
  inhibit_detail_ = 0;
  setState(AutonomyAuthorityState::Unknown);
}

bool AutonomyAuthorityMonitor::configure(const AutonomyCommandProfile& profile) {
  if (!isValidProfile(profile)) {
    profile_configured_ = false;
    noteProtocolFault(kDetailInvalidProfile);
    return false;
  }
  profile_ = profile;
  profile_configured_ = true;
  quiet_window_satisfied_ = false;
  setState(AutonomyAuthorityState::Unknown);
  return true;
}

void AutonomyAuthorityMonitor::clearProfile() {
  profile_ = {};
  profile_configured_ = false;
  quiet_window_satisfied_ = false;
  setState(AutonomyAuthorityState::Unknown);
}

void AutonomyAuthorityMonitor::observeFrame(uint32_t now_ms,
                                            const can::CanFrame8& frame,
                                            bool local_authority_active) {
  if (!profile_configured_) {
    setState(AutonomyAuthorityState::Unknown);
    return;
  }

  if (!matchesProfile(frame)) {
    return;
  }

  last_upstream_command_ms_ = now_ms;
  quiet_window_satisfied_ = false;
  setState(AutonomyAuthorityState::ActiveConfirmed);

  if (local_authority_active) {
    latchLocalTxInhibit(kDetailReappearance);
  }
}

void AutonomyAuthorityMonitor::update(uint32_t now_ms) {
  if (!profile_configured_) {
    setState(AutonomyAuthorityState::Unknown);
    quiet_window_satisfied_ = false;
    return;
  }

  const uint32_t age_ms = now_ms - last_upstream_command_ms_;
  if (state_ == AutonomyAuthorityState::ActiveConfirmed &&
      age_ms > profile_.active_timeout_ms) {
    setState(AutonomyAuthorityState::RecentlyActive);
  }

  if (state_ == AutonomyAuthorityState::RecentlyActive &&
      age_ms >= profile_.release_quiet_ms) {
    quiet_window_satisfied_ = true;
    setState(AutonomyAuthorityState::InactiveConfirmed);
  }
}

void AutonomyAuthorityMonitor::latchLocalTxInhibit(uint16_t detail) {
  local_tx_inhibit_latched_ = true;
  inhibit_detail_ = detail;
}

void AutonomyAuthorityMonitor::clearLocalTxInhibitForService() {
  local_tx_inhibit_latched_ = false;
  inhibit_detail_ = 0;
  setState(AutonomyAuthorityState::Unknown);
}

bool AutonomyAuthorityMonitor::isValidProfile(const AutonomyCommandProfile& profile) {
  if (profile.bus == kAuthorityNoBus || profile.can_id_mask == 0 || profile.dlc_min > profile.dlc_max ||
      profile.dlc_max > 8 || profile.active_timeout_ms == 0 || profile.release_quiet_ms == 0) {
    return false;
  }
  if (profile.has_mode_bit && profile.mode_offset >= profile.dlc_min) {
    return false;
  }
  if (profile.has_alive_counter && profile.alive_offset >= profile.dlc_min) {
    return false;
  }
  return true;
}

bool AutonomyAuthorityMonitor::matchesProfile(const can::CanFrame8& frame) {
  if (frame.bus != profile_.bus || frame.dlc < profile_.dlc_min || frame.dlc > profile_.dlc_max) {
    return false;
  }
  if ((rawCanId(frame.can_id_flags) & profile_.can_id_mask) !=
      (profile_.can_id & profile_.can_id_mask)) {
    return false;
  }
  if (profile_.has_mode_bit) {
    if (profile_.mode_offset >= frame.dlc) {
      noteProtocolFault(kDetailModeOffsetOutOfRange);
      return false;
    }
    if ((frame.data[profile_.mode_offset] & profile_.mode_mask) == 0) {
      return false;
    }
  }
  if (profile_.has_alive_counter && profile_.alive_offset >= frame.dlc) {
    noteProtocolFault(kDetailAliveOffsetOutOfRange);
    return false;
  }
  return true;
}

void AutonomyAuthorityMonitor::setState(AutonomyAuthorityState state) {
  state_ = state;
}

void AutonomyAuthorityMonitor::noteProtocolFault(uint16_t detail) {
  protocol_fault_count_++;
  inhibit_detail_ = detail;
  setState(AutonomyAuthorityState::ProtocolFault);
}

}  // namespace csm::board::authority

