#include "board/control/VehicleCommandMapper.h"

namespace csm::board::control {
namespace {

constexpr uint16_t kDetailProfileNotConfigured = 1;
constexpr uint16_t kDetailOutputDisabled = 2;
constexpr uint16_t kDetailOutOfRange = 3;
constexpr uint16_t kDetailNoVehicleMapping = 4;

uint8_t mapSteering(int16_t permille) {
  const int32_t output = static_cast<int32_t>(kRemoteSteeringCenter) +
      (static_cast<int32_t>(permille) * 120) / 1000;
  if (output < kRemoteSteeringMinimum) return kRemoteSteeringMinimum;
  if (output > kRemoteSteeringMaximum) return kRemoteSteeringMaximum;
  return static_cast<uint8_t>(output);
}

CanFrameRequest makeFrame(const OperatorCommand& command,
                          const VehicleCommandProfile& profile,
                          uint32_t can_id) {
  CanFrameRequest frame;
  frame.source = command.source;
  frame.command_seq = command.command_seq;
  frame.bus = profile.bus;
  frame.can_id_flags = can_id;
  frame.dlc = 8;
  frame.policy_id = profile.policy_id;
  frame.rate_bucket = 1;
  return frame;
}

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

  switch (profile_.mapping) {
    case VehicleCommandMapping::MdpsBench0x007: {
      CanFrameRequest frame = makeFrame(command, profile_, kRemoteSteeringCanId);
      if (command.auxiliary_permille != 0) {
        frame.data[7] = command.auxiliary_permille < 0
            ? kRemoteAuxiliaryNegative
            : kRemoteAuxiliaryPositive;
      } else {
        frame.data[0] = mapSteering(command.steer_permille);
        if (command.momentary_overlay_permille > 0) {
          frame.data[7] = kRemoteAuxiliaryNegative;
        } else if (command.steering_overlay_permille < 0) {
          frame.data[7] = kRemoteAuxiliaryNegative;
        } else if (command.steering_overlay_permille > 0) {
          frame.data[7] = kRemoteAuxiliaryPositive;
        }
      }
      result.frames[result.frame_count++] = frame;
      result.mapped = true;
      result.decision = authority::ControlDecisionCode::Accepted;
      result.detail = 0;
      return result;
    }
    case VehicleCommandMapping::None:
    default:
      result.decision = authority::ControlDecisionCode::RejectedFramePolicy;
      result.detail = kDetailNoVehicleMapping;
      return result;
  }
}

bool VehicleCommandMapper::isValidProfile(const VehicleCommandProfile& profile) {
  if (!profile.configured || profile.bus == authority::kAuthorityNoBus) {
    return false;
  }
  if (profile.mapping != VehicleCommandMapping::None &&
      profile.mapping != VehicleCommandMapping::MdpsBench0x007) {
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
