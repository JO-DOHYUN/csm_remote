#include "board/control/ServiceHilIntentRuntime.h"

namespace csm::board::control {
namespace {

constexpr uint16_t kDetailBadWireIntent = 1;

int16_t steeringPermille(uint8_t value) {
  const int32_t delta = static_cast<int32_t>(value) - kRemoteSteeringCenter;
  const int32_t mapped = delta * 1000 / 120;
  return static_cast<int16_t>(mapped < -1000 ? -1000
      : (mapped > 1000 ? 1000 : mapped));
}

}  // namespace

bool ServiceHilIntentRuntime::begin(uint32_t now_ms, uint8_t bus,
                                    uint16_t policy_id) {
  bus_ = bus;
  policy_id_ = policy_id;
  limiter_.begin(now_ms);
  mapper_.begin(now_ms);

  CommandLimiterConfig limiter;
  limiter.configured = true;
  limiter.throttle_min_permille = -1000;
  limiter.throttle_max_permille = 1000;
  limiter.steer_min_permille = -1000;
  limiter.steer_max_permille = 1000;
  limiter.brake_min_permille = 0;
  limiter.brake_max_permille = 1000;
  limiter.throttle_rise_step_permille = 50;
  limiter.throttle_fall_step_permille = 200;
  limiter.steer_step_permille = 30;
  limiter.steer_return_step_permille = 50;

  VehicleCommandProfile profile;
  profile.configured = true;
  profile.output_enabled = true;
  profile.mapping = VehicleCommandMapping::Vehicle0x005And0x007;
  profile.bus = bus;
  profile.policy_id = policy_id;
  profile.throttle_limit_permille = 1000;
  profile.steer_limit_permille = 1000;
  profile.brake_limit_permille = 1000;
  configured_ = bus != authority::kAuthorityNoBus &&
      limiter_.configure(limiter) && mapper_.configure(profile);
  reset(now_ms);
  return configured_;
}

void ServiceHilIntentRuntime::reset(uint32_t now_ms) {
  requested_ = {};
  requested_.source = authority::ControlSourceId::HostService;
  limiter_.begin(now_ms);
}

bool ServiceHilIntentRuntime::decode(uint32_t can_id, uint8_t dlc,
                                     const uint8_t data[8],
                                     OperatorCommand* command) const {
  if (command == nullptr || data == nullptr || dlc != 8) return false;
  if (can_id == kRemoteDriveCanId) {
    if (data[0] != kRemoteDriveHeader) return false;
    if (data[1] == kRemoteDriveStopMode) {
      for (uint8_t index = 2; index < 8; ++index) {
        if (data[index] != 0) return false;
      }
      command->throttle_permille = 0;
      return true;
    }
    if (data[1] != kRemoteDriveMode || data[5] != 0 || data[6] != 0 ||
        data[7] != 0) return false;
    const uint16_t speed = static_cast<uint16_t>(data[2]) |
        (static_cast<uint16_t>(data[3]) << 8u);
    if (speed > 1000 ||
        (data[4] != kRemoteDriveForward &&
         data[4] != kRemoteDriveReverse)) return false;
    command->throttle_permille = data[4] == kRemoteDriveForward
        ? static_cast<int16_t>(speed) : -static_cast<int16_t>(speed);
    return true;
  }
  if (can_id == kRemoteSteeringCanId) {
    if (data[0] < kRemoteSteeringMinimum ||
        data[0] > kRemoteSteeringMaximum) return false;
    for (uint8_t index = 1; index < 7; ++index) {
      if (data[index] != 0) return false;
    }
    if (data[7] != 0 && data[7] != kRemoteAuxiliaryNegative &&
        data[7] != kRemoteAuxiliaryPositive) return false;
    command->steer_permille = steeringPermille(data[0]);
    command->auxiliary_permille = data[7] == kRemoteAuxiliaryNegative
        ? -1000 : (data[7] == kRemoteAuxiliaryPositive ? 1000 : 0);
    return true;
  }
  return false;
}

ServiceHilIntentResult ServiceHilIntentRuntime::accept(
    uint32_t now_ms, uint32_t command_id, uint32_t can_id, uint8_t dlc,
    const uint8_t data[8]) {
  ServiceHilIntentResult result;
  if (!configured_ || !decode(can_id, dlc, data, &requested_)) {
    result.detail = kDetailBadWireIntent;
    return result;
  }
  requested_.command_seq = command_id;
  requested_.source_time_ms = now_ms;
  requested_.enable_request = true;
  const CommandLimitResult limited = limiter_.evaluate(now_ms, requested_);
  if (!limited.accepted) {
    result.decision = limited.decision;
    result.detail = limited.detail;
    return result;
  }
  const VehicleCommandMapResult mapped = mapper_.map(limited.command);
  if (!mapped.mapped) {
    result.decision = mapped.decision;
    result.detail = mapped.detail;
    return result;
  }
  for (uint8_t index = 0; index < mapped.frame_count; ++index) {
    if (mapped.frames[index].can_id_flags == can_id) {
      limiter_.noteAccepted(now_ms, limited.command);
      result.accepted = true;
      result.decision = authority::ControlDecisionCode::Accepted;
      result.frame = mapped.frames[index];
      return result;
    }
  }
  result.detail = kDetailBadWireIntent;
  return result;
}

}  // namespace csm::board::control
