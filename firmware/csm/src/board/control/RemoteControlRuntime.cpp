#include "board/control/RemoteControlRuntime.h"

#include <string.h>

namespace csm::board::control {
namespace {

void saturatingIncrement(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) ++(*value);
}

}  // namespace

bool RemoteControlRuntime::begin(uint32_t now_ms, uint32_t m7_boot_id,
                                 const RemoteControlRuntimeConfig& config) {
  if (!config.configured || config.bus == authority::kAuthorityNoBus ||
      config.semantic_update_period_ms == 0u ||
      config.semantic_update_period_ms > 100u ||
      config.neutral_deadband_permille > 250u ||
      config.drive_deadband_permille > 100u ||
      config.steering_deadband_permille > 100u ||
      config.auxiliary_threshold_permille < 100u ||
      config.auxiliary_threshold_permille > 1000u ||
      config.drive_channel_index >= remote::kRcChannelCount ||
      config.steering_channel_index >= remote::kRcChannelCount ||
      config.drive_channel_index == config.steering_channel_index) {
    return false;
  }
  config_ = config;
  status_ = {};
  status_.configured = true;
  status_.semantic_output_enabled = config.semantic_output_enabled;
  status_.remote_reserved = true;
  memset(source_lanes_, 0, sizeof(source_lanes_));

  mailbox_reader_.begin(now_ms);
  command_limiter_.begin(now_ms);
  vehicle_mapper_.begin(now_ms);
  remote_source_.begin(now_ms);

  remote::RemoteControlSourceConfig source_config;
  source_config.drive_channel_index = config.drive_channel_index;
  source_config.steering_channel_index = config.steering_channel_index;
  source_config.auxiliary_channel_index = 4;
  source_config.steering_overlay_channel_index = 9;
  source_config.momentary_overlay_channel_index = 10;
  source_config.drive_deadband_permille = config.drive_deadband_permille;
  source_config.steering_deadband_permille = config.steering_deadband_permille;
  source_config.auxiliary_threshold_permille = config.auxiliary_threshold_permille;
  source_config.invert_drive = config.invert_drive;
  source_config.invert_steering = config.invert_steering;
  if (!remote_source_.configure(source_config)) return false;

  CommandLimiterConfig limiter;
  limiter.configured = true;
  limiter.throttle_min_permille = -1000;
  limiter.throttle_max_permille = 1000;
  limiter.steer_min_permille = -1000;
  limiter.steer_max_permille = 1000;
  limiter.brake_min_permille = 0;
  limiter.brake_max_permille = 1000;
  limiter.min_command_interval_ms = config.semantic_update_period_ms;
  // Preserve the qualified semantic slew per elapsed update quantum. It is
  // not a CAN release clock and may only produce a new latest state image.
  limiter.throttle_rise_step_permille = static_cast<uint16_t>(
      50u * config.semantic_update_period_ms / 20u);
  limiter.throttle_fall_step_permille = static_cast<uint16_t>(
      200u * config.semantic_update_period_ms / 20u);
  limiter.steer_step_permille = static_cast<uint16_t>(
      config.steering_step_permille_per_20ms *
      config.semantic_update_period_ms / 20u);
  limiter.steer_return_step_permille = static_cast<uint16_t>(
      config.steering_return_step_permille_per_20ms *
      config.semantic_update_period_ms / 20u);
  if (!command_limiter_.configure(limiter)) return false;

  VehicleCommandProfile mapper;
  mapper.configured = true;
  mapper.output_enabled = config.semantic_output_enabled;
  mapper.mapping = config.mapping;
  mapper.bus = config.bus;
  mapper.policy_id = config.policy_id;
  mapper.throttle_limit_permille = 1000;
  mapper.steer_limit_permille = 1000;
  mapper.brake_limit_permille = 1000;
  if (!vehicle_mapper_.configure(mapper)) return false;

  return true;
}

RemoteControlRuntimeOutput RemoteControlRuntime::service(
    uint32_t now_ms) {
  RemoteControlRuntimeOutput output;
  if (!status_.configured) return output;

  const remote::RemoteSharedSampleReadResult shared =
      remote::readRemoteSharedSample(last_shared_sequence_);
  if (!shared.accepted) {
    if (shared.detail != 0u) {
      saturatingIncrement(&status_.ipc_rejects);
      status_.last_ipc_reject_detail = static_cast<uint8_t>(shared.detail);
    }
  } else if (shared.new_sample) {
    status_.last_ipc_reject_detail = 0;
    last_shared_sequence_ = shared.shared_sequence;
    last_frontend_seen_ms_ = now_ms;
    frontend_seen_ = true;
    status_.shared_sequence = shared.shared_sequence;
    status_.m4_boot_id = shared.slot.m4_boot_id;
    status_.frontend_diagnostics = shared.slot.diagnostics;
    status_.raw_ch2 = shared.slot.diagnostics.raw_ch2;
    status_.raw_ch4 = shared.slot.diagnostics.raw_ch4;
    if (observed_m4_boot_id_ != shared.slot.m4_boot_id) {
      observed_m4_boot_id_ = shared.slot.m4_boot_id;
      status_.remote_reserved = true;
      command_limiter_.begin(now_ms);
      invalidateSource();
    }
    mailbox_reader_.updateFromMailboxFrame(now_ms, shared.slot.mailbox);
  } else {
    mailbox_reader_.update(now_ms, config_.m4_heartbeat_timeout_ms);
  }

  updateRemoteState(now_ms);
  publishTelemetry(now_ms);

  if (!config_.semantic_output_enabled || !status_.remote_valid) {
    invalidateSource();
    return output;
  }
  if (now_ms - last_semantic_update_ms_ < config_.semantic_update_period_ms) {
    output.source_valid = status_.source_image_valid;
    output.image_generation = status_.source_image_generation;
    output.lease_sequence = status_.source_lease_sequence;
    memcpy(output.lanes, source_lanes_, sizeof(output.lanes));
    return output;
  }
  last_semantic_update_ms_ = now_ms;
  if (!buildSourceImage(now_ms, &output)) {
    saturatingIncrement(&status_.semantic_rejects);
    invalidateSource();
    return output;
  }
  saturatingIncrement(&status_.semantic_updates);
  return output;
}

void RemoteControlRuntime::updateRemoteState(uint32_t now_ms) {
  status_.frontend_alive = frontend_seen_ &&
      now_ms - last_frontend_seen_ms_ <= config_.m4_heartbeat_timeout_ms;
  const remote::M4RemoteMailboxSnapshot& snapshot = mailbox_reader_.snapshot();
  status_.link_state = snapshot.link_state;
  status_.sample_age_ms = snapshot.age_ms;
  status_.link_quality = snapshot.sample.link_quality;
  status_.rssi_magnitude = snapshot.sample.rssi_hint;
  status_.drive_permille = snapshot.sample.ch[config_.drive_channel_index];
  status_.steering_permille = snapshot.sample.ch[config_.steering_channel_index];
  status_.auxiliary_permille = snapshot.sample.ch[4];
  status_.remote_valid = status_.frontend_alive &&
      mailbox_reader_.hasFreshUsableSample();
  status_.remote_reserved = status_.remote_valid;
}

void RemoteControlRuntime::publishTelemetry(uint32_t now_ms) {
  if (now_ms - last_telemetry_ms_ < 20u) return;
  last_telemetry_ms_ = now_ms;
  remote::RemoteTelemetrySlot telemetry;
  telemetry.m7_time_ms = now_ms;
  telemetry.authority_state = static_cast<uint8_t>(
      status_.source_image_valid ? authority::AuthorityState::RemoteActive
                                 : authority::AuthorityState::LocalReady);
  telemetry.remote_link_state = static_cast<uint8_t>(status_.link_state);
  telemetry.flags = (status_.remote_valid ? 1u : 0u) |
      (status_.source_image_valid ? 1u << 2 : 0u);
  const char* mode = status_.source_image_valid ? "RC ACTIVE" : "CSM SAFE";
  strncpy(telemetry.flight_mode, mode, sizeof(telemetry.flight_mode) - 1u);
  (void)remote::publishRemoteTelemetry(telemetry);
}

bool RemoteControlRuntime::buildSourceImage(
    uint32_t now_ms, RemoteControlRuntimeOutput* output) {
  remote_source_.update(now_ms, mailbox_reader_.snapshot(), true, false);
  OperatorCommand command = remote_source_.command();
  command.command_seq = status_.source_lease_sequence + 1u;
  const CommandLimitResult limited = command_limiter_.evaluate(now_ms, command);
  status_.last_decision = limited.decision;
  if (!limited.accepted) return false;
  const VehicleCommandMapResult result = vehicle_mapper_.map(limited.command);
  status_.last_decision = result.decision;
  if (!result.mapped || result.frame_count == 0u) return false;

  command_limiter_.noteAccepted(now_ms, limited.command);
  status_.last_decision = authority::ControlDecisionCode::Accepted;

  control_island::LaneExecutionImage next[control_island::kLaneCount] = {};
  for (uint8_t index = 0; index < result.frame_count; ++index) {
    const CanFrameRequest& frame = result.frames[index];
    uint8_t lane = control_island::kLaneCount;
    if (frame.can_id_flags == control_island::kLaneIds[control_island::kLane005]) {
      lane = control_island::kLane005;
    } else if (frame.can_id_flags ==
               control_island::kLaneIds[control_island::kLane007]) {
      lane = control_island::kLane007;
    }
    if (lane >= control_island::kLaneCount || frame.dlc != 8u) return false;
    next[lane].valid = 1u;
    memcpy(next[lane].data, frame.data, 8u);
  }
  // RC owns only 005/007. Lane 364 remains invalid and is resolved by M4's
  // frozen safe-wire policy (SuppressTx).
  if (next[control_island::kLane005].valid == 0u ||
      next[control_island::kLane007].valid == 0u) {
    return false;
  }
  const bool changed = !status_.source_image_valid ||
      !sameImage(next, source_lanes_);
  if (changed) {
    ++status_.source_image_generation;
    if (status_.source_image_generation == 0u) {
      status_.source_image_generation = 1u;
    }
  }
  ++status_.source_lease_sequence;
  if (status_.source_lease_sequence == 0u) status_.source_lease_sequence = 1u;
  for (uint8_t lane = 0; lane < control_island::kLaneCount; ++lane) {
    next[lane].value_generation = status_.source_image_generation;
  }
  memcpy(source_lanes_, next, sizeof(source_lanes_));
  status_.source_image_valid = true;
  output->source_valid = true;
  output->image_changed = changed;
  output->image_generation = status_.source_image_generation;
  output->lease_sequence = status_.source_lease_sequence;
  memcpy(output->lanes, source_lanes_, sizeof(output->lanes));
  return true;
}

void RemoteControlRuntime::invalidateSource() {
  status_.source_image_valid = false;
}

bool RemoteControlRuntime::sameImage(
    const control_island::LaneExecutionImage* lhs,
    const control_island::LaneExecutionImage* rhs) {
  for (uint8_t lane = 0; lane < control_island::kLaneCount; ++lane) {
    if (lhs[lane].valid != rhs[lane].valid ||
        memcmp(lhs[lane].data, rhs[lane].data, 8u) != 0) {
      return false;
    }
  }
  return true;
}

}  // namespace csm::board::control
