#include "board/control/VehicleCommandMapper.h"

namespace csm::board::control {
namespace {

constexpr uint16_t kDetailProfileNotConfigured = 1;
constexpr uint16_t kDetailOutputDisabled = 2;
constexpr uint16_t kDetailOutOfRange = 3;
constexpr uint16_t kDetailNoVehicleMapping = 4;

}  // namespace

void VehicleCommandMapper::begin(uint32_t) {
  profile_ = {};
}

bool VehicleCommandMapper::configure(const VehicleCommandProfile& profile) {
  if (!isValidProfile(profile)) {
    profile_ = {};
    return false;
  }
  profile_ = profile;
  profile_.configured = true;
  return true;
}

void VehicleCommandMapper::clearProfile() {
  profile_ = {};
}

VehicleCommandMapResult VehicleCommandMapper::map(const OperatorCommand& command) const {
  VehicleCommandMapResult result;

  if (!profile_.configured) {
    result.decision = authority::ControlDecisionCode::RejectedFramePolicy;
    result.detail = kDetailProfileNotConfigured;
    return result;
  }
  if (!profile_.output_enabled) {
    result.decision = authority::ControlDecisionCode::RejectedFramePolicy;
    result.detail = kDetailOutputDisabled;
    return result;
  }
  if (!isWithinOperatorCommandRange(command) || !isWithinProfileLimits(command)) {
    result.decision = authority::ControlDecisionCode::RejectedRateLimit;
    result.detail = kDetailOutOfRange;
    return result;
  }

  // Phase 1C intentionally has no real vehicle CAN mapping.
  result.decision = authority::ControlDecisionCode::RejectedFramePolicy;
  result.detail = kDetailNoVehicleMapping;
  return result;
}

bool VehicleCommandMapper::isValidProfile(const VehicleCommandProfile& profile) {
  if (!profile.configured || profile.bus == authority::kAuthorityNoBus) {
    return false;
  }
  return profile.throttle_limit_permille >= 0 &&
         profile.throttle_limit_permille <= kControlPermilleMax &&
         profile.steer_limit_permille >= 0 &&
         profile.steer_limit_permille <= kControlPermilleMax &&
         profile.brake_limit_permille >= 0 &&
         profile.brake_limit_permille <= kBrakePermilleMax;
}

bool VehicleCommandMapper::isWithinProfileLimits(const OperatorCommand& command) const {
  return command.throttle_permille >= -profile_.throttle_limit_permille &&
         command.throttle_permille <= profile_.throttle_limit_permille &&
         command.steer_permille >= -profile_.steer_limit_permille &&
         command.steer_permille <= profile_.steer_limit_permille &&
         command.brake_permille <= profile_.brake_limit_permille;
}

}  // namespace csm::board::control
