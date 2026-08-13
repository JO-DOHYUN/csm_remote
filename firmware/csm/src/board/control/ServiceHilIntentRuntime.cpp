#include "board/control/ServiceHilIntentRuntime.h"

namespace csm::board::control {
namespace {

constexpr uint16_t kDetailBadWireIntent = 1;
constexpr uint16_t kDetailInactive = 2;
constexpr uint32_t kReferenceLimiterPeriodMs = 20;

constexpr uint16_t scaleLimiterStep(uint16_t reference_step) {
  return static_cast<uint16_t>(
      reference_step * kServiceHilDrivePeriodMs / kReferenceLimiterPeriodMs);
}

static_assert(scaleLimiterStep(50) == 12, "5 ms throttle rise step changed");
static_assert(scaleLimiterStep(200) == 50, "5 ms throttle fall step changed");
static_assert(scaleLimiterStep(30) == 7, "5 ms steering rise step changed");
static_assert(scaleLimiterStep(50) == 12, "5 ms steering return step changed");

void saturatingAdd(uint32_t increment, uint32_t* value) {
  if (value == nullptr || increment == 0) return;
  if (increment > UINT32_MAX - *value) {
    *value = UINT32_MAX;
  } else {
    *value += increment;
  }
}

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
  // The product limiter constants are specified per 20 ms. Service/HIL
  // advances on the 5 ms M7 drive release, so scale each step by elapsed
  // release time. Otherwise ARM would make the same command ramp four times
  // faster than RC merely because its release period is shorter.
  limiter.throttle_rise_step_permille = scaleLimiterStep(50);
  limiter.throttle_fall_step_permille = scaleLimiterStep(200);
  limiter.steer_step_permille = scaleLimiterStep(30);
  limiter.steer_return_step_permille = scaleLimiterStep(50);

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

bool ServiceHilIntentRuntime::activate(uint32_t now_ms) {
  reset(now_ms);
  if (!configured_ ||
      !release_schedule_.begin(now_ms + kServiceHilDrivePeriodMs,
                               kServiceHilDrivePeriodMs,
                               kServiceHilSteeringPeriodMs,
                               kServiceHilEhbPeriodMs,
                               kServiceHilEhbPhaseOffsetMs)) {
    return false;
  }
  status_.active = true;
  // Establish a deterministic neutral baseline so the first non-neutral intent
  // cannot bypass the step limiter merely because it arrived before release 1.
  limiter_.noteAccepted(now_ms, limited_);
  return true;
}

void ServiceHilIntentRuntime::reset(uint32_t now_ms) {
  requested_ = {};
  requested_.source = authority::ControlSourceId::HostService;
  requested_.enable_request = true;
  limited_ = requested_;
  status_ = {};
  drive_command_id_ = 0;
  steering_command_id_ = 0;
  ehb_command_id_ = 0;
  drive_updated_ms_ = now_ms;
  steering_updated_ms_ = now_ms;
  ehb_updated_ms_ = now_ms;
  ehb_request_ = kServiceHilEhbNeutral;
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
  if (can_id == kServiceHilEhbCanId) {
    for (uint8_t index = 0; index < 7; ++index) {
      if (data[index] != 0) return false;
    }
    const uint8_t request = data[7];
    if (request != kServiceHilEhbNeutral &&
        (request < kServiceHilEhbMinimum ||
         request > kServiceHilEhbMaximum)) return false;
    return true;
  }
  return false;
}

ServiceHilIntentResult ServiceHilIntentRuntime::accept(
    uint32_t now_ms, uint32_t command_id, uint32_t can_id, uint8_t dlc,
    const uint8_t data[8]) {
  ServiceHilIntentResult result;
  if (!configured_ || !status_.active) {
    result.decision = authority::ControlDecisionCode::RejectedNoTakeover;
    result.detail = kDetailInactive;
    return result;
  }
  if (!configured_ || !decode(can_id, dlc, data, &requested_)) {
    result.detail = kDetailBadWireIntent;
    return result;
  }
  requested_.command_seq = command_id;
  requested_.source_time_ms = now_ms;
  requested_.enable_request = true;
  if (can_id == kRemoteDriveCanId) {
    drive_command_id_ = command_id;
    drive_updated_ms_ = now_ms;
    status_.drive_present = true;
    status_.drive_stale = false;
  } else if (can_id == kRemoteSteeringCanId) {
    steering_command_id_ = command_id;
    steering_updated_ms_ = now_ms;
    status_.steering_present = true;
    status_.steering_stale = false;
  } else if (can_id == kServiceHilEhbCanId) {
    ehb_request_ = data[7];
    ehb_command_id_ = command_id;
    ehb_updated_ms_ = now_ms;
    status_.ehb_present = true;
    status_.ehb_stale = false;
  }
  result.accepted = true;
  result.decision = authority::ControlDecisionCode::Accepted;
  return result;
}

ServiceHilReleaseBatch ServiceHilIntentRuntime::poll(uint32_t now_ms) {
  ServiceHilReleaseBatch batch;
  if (!configured_ || !status_.active) return batch;

  const ControlReleaseBatch releases = release_schedule_.poll(now_ms);
  batch.drive_missed_releases = releases.drive.missed_releases;
  batch.steering_missed_releases = releases.steering.missed_releases;
  batch.ehb_missed_releases = releases.brake.missed_releases;
  saturatingAdd(releases.drive.missed_releases,
                &status_.drive_missed_releases);
  saturatingAdd(releases.steering.missed_releases,
                &status_.steering_missed_releases);
  saturatingAdd(releases.brake.missed_releases,
                &status_.ehb_missed_releases);
  if (!releases.drive.due && !releases.steering.due && !releases.brake.due) {
    return batch;
  }

  status_.drive_stale =
      !laneFresh(status_.drive_present, drive_updated_ms_, now_ms);
  status_.steering_stale =
      !laneFresh(status_.steering_present, steering_updated_ms_, now_ms);
  batch.drive_stale = status_.drive_stale;
  batch.steering_stale = status_.steering_stale;
  status_.ehb_stale =
      !laneFresh(status_.ehb_present, ehb_updated_ms_, now_ms);
  batch.ehb_stale = status_.ehb_stale;

  if (releases.drive.due) {
    OperatorCommand target = requested_;
    target.source = authority::ControlSourceId::HostService;
    target.source_time_ms = now_ms;
    target.command_seq = releases.drive.sequence;
    target.enable_request = true;
    if (status_.drive_stale) target.throttle_permille = 0;
    if (status_.steering_stale) {
      target.steer_permille = 0;
      target.auxiliary_permille = 0;
    }
    const CommandLimitResult limited = limiter_.evaluate(now_ms, target);
    if (!limited.accepted) {
      batch.decision = limited.decision;
      batch.detail = limited.detail;
      return batch;
    }
    limited_ = limited.command;
    limiter_.noteAccepted(now_ms, limited_);
  }

  const VehicleCommandMapResult mapped = mapper_.map(limited_);
  if (!mapped.mapped) {
    batch.decision = mapped.decision;
    batch.detail = mapped.detail;
    return batch;
  }
  for (uint8_t index = 0; index < mapped.frame_count; ++index) {
    const uint32_t can_id = mapped.frames[index].can_id_flags & 0x7FFu;
    if (can_id == kRemoteDriveCanId && releases.drive.due) {
      addRelease(mapped.frames[index], drive_command_id_, &batch);
    } else if (can_id == kRemoteSteeringCanId && releases.steering.due) {
      addRelease(mapped.frames[index], steering_command_id_, &batch);
    }
  }
  if (releases.brake.due) {
    CanFrameRequest frame;
    frame.source = authority::ControlSourceId::HostService;
    frame.command_seq = releases.brake.sequence;
    frame.bus = bus_;
    frame.can_id_flags = kServiceHilEhbCanId;
    frame.dlc = 8;
    frame.data[7] = status_.ehb_stale ? kServiceHilEhbNeutral : ehb_request_;
    frame.policy_id = policy_id_;
    addRelease(frame, ehb_command_id_, &batch);
  }
  if (batch.count != 0) {
    saturatingAdd(1, &status_.release_batches);
    saturatingAdd(batch.count, &status_.released_frames);
  }
  return batch;
}

bool ServiceHilIntentRuntime::laneFresh(bool present,
                                        uint32_t last_update_ms,
                                        uint32_t now_ms) const {
  return present &&
      static_cast<uint32_t>(now_ms - last_update_ms) <=
          kServiceHilIntentFreshnessMs;
}

void ServiceHilIntentRuntime::addRelease(const CanFrameRequest& frame,
                                         uint32_t command_id,
                                         ServiceHilReleaseBatch* batch) {
  if (batch == nullptr || batch->count >= kServiceHilReleaseMaxFrames) return;
  ServiceHilReleaseItem& item = batch->items[batch->count++];
  item.command_id = command_id;
  item.frame = frame;
}

}  // namespace csm::board::control
