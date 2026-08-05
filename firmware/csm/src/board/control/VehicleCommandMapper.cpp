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

CanFrameRequest makeDriveFrame(const OperatorCommand& command,
                               const VehicleCommandProfile& profile) {
  CanFrameRequest frame = makeFrame(command, profile, kRemoteDriveCanId);
  frame.data[0] = kRemoteDriveHeader;
  const int32_t signed_speed = command.throttle_permille;
  const uint16_t requested_speed = static_cast<uint16_t>(
      signed_speed < 0 ? -signed_speed : signed_speed);
  if (requested_speed <= kRemoteDriveDeadbandPermille) {
    frame.data[1] = kRemoteDriveStopMode;
    return frame;
  }
  uint16_t speed = static_cast<uint16_t>(
      ((requested_speed + (kRemoteDriveStepPermille / 2u)) /
       kRemoteDriveStepPermille) * kRemoteDriveStepPermille);
  if (speed < kRemoteDriveMinimumPermille) {
    speed = kRemoteDriveMinimumPermille;
  }
  if (speed > 1000u) {
    speed = 1000u;
  }
  frame.data[1] = kRemoteDriveMode;
  frame.data[2] = static_cast<uint8_t>(speed & 0xFFu);
  frame.data[3] = static_cast<uint8_t>((speed >> 8u) & 0xFFu);
  frame.data[4] = signed_speed > 0 ? kRemoteDriveForward : kRemoteDriveReverse;
  return frame;
}

CanFrameRequest makeSteeringFrame(const OperatorCommand& command,
                                  const VehicleCommandProfile& profile) {
  CanFrameRequest frame = makeFrame(command, profile, kRemoteSteeringCanId);
  // Every auxiliary value is an overlay on a valid current/limited steering
  // command. A zero byte0 is outside the 10..250 product contract.
  frame.data[0] = mapSteering(command.steer_permille);
  if (command.auxiliary_permille != 0) {
    frame.data[7] = command.auxiliary_permille < 0
        ? kRemoteAuxiliaryNegative
        : kRemoteAuxiliaryPositive;
  } else {
    if (command.momentary_overlay_permille > 0) {
      frame.data[7] = kRemoteAuxiliaryNegative;
    } else if (command.steering_overlay_permille < 0) {
      frame.data[7] = kRemoteAuxiliaryNegative;
    } else if (command.steering_overlay_permille > 0) {
      frame.data[7] = kRemoteAuxiliaryPositive;
    }
  }
  return frame;
}

}  // namespace

void ServiceSteeringCenterGuard::reset() {
  started_ms_ = 0;
  steering_ = kRemoteSteeringCenter;
  active_ = false;
  timed_out_ = false;
}

bool ServiceSteeringCenterGuard::expired(uint32_t now_ms) const {
  return active_ && now_ms - started_ms_ >= kServiceSteeringCenterMaxHoldMs;
}

ServiceSteeringCenterDecision ServiceSteeringCenterGuard::apply(
    uint32_t now_ms, uint8_t steering, uint8_t requested_auxiliary) {
  ServiceSteeringCenterDecision decision;
  decision.steering = steering < kRemoteSteeringMinimum
      ? kRemoteSteeringMinimum
      : (steering > kRemoteSteeringMaximum ? kRemoteSteeringMaximum
                                           : steering);

  if (requested_auxiliary != kRemoteAuxiliaryNegative) {
    reset();
    decision.steering = steering < kRemoteSteeringMinimum
        ? kRemoteSteeringMinimum
        : (steering > kRemoteSteeringMaximum ? kRemoteSteeringMaximum
                                             : steering);
    decision.auxiliary = 0;
    return decision;
  }

  steering_ = decision.steering;
  if (timed_out_) {
    decision.timeout_release = true;
    return decision;
  }
  if (!active_) {
    active_ = true;
    started_ms_ = now_ms;
  }
  if (expired(now_ms)) {
    active_ = false;
    timed_out_ = true;
    decision.timeout_release = true;
    return decision;
  }
  decision.auxiliary = kRemoteAuxiliaryNegative;
  return decision;
}

bool ServiceSteeringCenterGuard::pollTimeoutRelease(uint32_t now_ms,
                                                    uint8_t* steering) {
  if (!expired(now_ms)) return false;
  active_ = false;
  timed_out_ = true;
  if (steering != nullptr) *steering = steering_;
  return true;
}

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
    case VehicleCommandMapping::Vehicle0x005And0x007: {
      result.frames[result.frame_count++] = makeDriveFrame(command, profile_);
      result.frames[result.frame_count++] = makeSteeringFrame(command, profile_);
      result.mapped = true;
      result.decision = authority::ControlDecisionCode::Accepted;
      result.detail = 0;
      return result;
    }
    case VehicleCommandMapping::VehicleMdps0x007Only: {
      result.frames[result.frame_count++] = makeSteeringFrame(command, profile_);
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

VehicleCommandMapResult VehicleCommandMapper::mapSafetyStop(
    uint32_t command_seq) const {
  VehicleCommandMapResult result;
  if (!profile_.configured || !profile_.output_enabled ||
      (profile_.mapping != VehicleCommandMapping::Vehicle0x005And0x007 &&
       profile_.mapping != VehicleCommandMapping::VehicleMdps0x007Only)) {
    result.decision = authority::ControlDecisionCode::RejectedFramePolicy;
    result.detail = kDetailProfileNotConfigured;
    return result;
  }
  OperatorCommand command;
  command.source = authority::ControlSourceId::SafetyNeutral;
  command.command_seq = command_seq;
  if (profile_.mapping == VehicleCommandMapping::Vehicle0x005And0x007) {
    result.frames[result.frame_count++] = makeDriveFrame(command, profile_);
  }
  result.frames[result.frame_count++] = makeSteeringFrame(command, profile_);
  result.mapped = true;
  result.decision = authority::ControlDecisionCode::Accepted;
  return result;
}

bool VehicleCommandMapper::isValidProfile(const VehicleCommandProfile& profile) {
  if (!profile.configured || profile.bus == authority::kAuthorityNoBus) {
    return false;
  }
  if (profile.mapping != VehicleCommandMapping::None &&
      profile.mapping != VehicleCommandMapping::Vehicle0x005And0x007 &&
      profile.mapping != VehicleCommandMapping::VehicleMdps0x007Only) {
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
