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

  remote::initializeRemoteSharedMemoryForM7(m7_boot_id);
  mailbox_reader_.begin(now_ms);
  authority_manager_.begin(now_ms);
  command_limiter_.begin(now_ms);
  vehicle_mapper_.begin(now_ms);
  can_tx_gateway_.begin(now_ms);
  orchestrator_.begin(now_ms);

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
  if (!orchestrator_.configureRemoteSource(source_config)) return false;

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

  CanTxGatewayPolicy gateway;
  gateway.configured = true;
  gateway.build_profile_allows_local_tx = config.semantic_output_enabled;
  gateway.bus = config.bus;
  gateway.policy_id = config.policy_id;
  gateway.allowlist_count = 2;
  gateway.allowlist_ids[0] = kRemoteDriveCanId;
  gateway.allowlist_ids[1] = kRemoteSteeringCanId;
  return can_tx_gateway_.configure(gateway);
}

RemoteControlRuntimeOutput RemoteControlRuntime::service(
    uint32_t now_ms, const RemoteControlRuntimeInputs& inputs) {
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
      status_.handoff_qualified = false;
      status_.release_qualified = false;
      status_.remote_reserved = true;
      neutral_timer_active_ = false;
      release_timer_active_ = false;
      command_limiter_.begin(now_ms);
      invalidateSource();
    }
    mailbox_reader_.updateFromMailboxFrame(now_ms, shared.slot.mailbox);
  } else {
    mailbox_reader_.update(now_ms, config_.m4_heartbeat_timeout_ms);
  }

  updateRemoteState(now_ms);
  status_.host_control_allowed = status_.frontend_alive &&
      status_.release_qualified && !status_.remote_reserved;
  publishTelemetry(now_ms);

  if (!config_.semantic_output_enabled || inputs.host_output_reserved ||
      !status_.remote_valid || !status_.handoff_qualified ||
      !inputs.hard_safety_allows || inputs.estop_asserted ||
      inputs.fault_lockout || inputs.local_tx_inhibit_latched ||
      !inputs.backend_state.ready || inputs.backend_state.bus_off ||
      inputs.backend_state.error_passive) {
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
  if (!buildSourceImage(now_ms, inputs, &output)) {
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
  status_.neutral_now = status_.remote_valid && isNeutralSample(snapshot);

  if (status_.remote_valid) {
    status_.remote_reserved = true;
    status_.release_qualified = false;
    release_timer_active_ = false;
    if (!status_.handoff_qualified) {
      if (status_.neutral_now) {
        if (!neutral_timer_active_) {
          neutral_timer_active_ = true;
          neutral_since_ms_ = now_ms;
        }
        if (now_ms - neutral_since_ms_ >= config_.neutral_qualification_ms) {
          status_.handoff_qualified = true;
        }
      } else {
        neutral_timer_active_ = false;
      }
    }
    return;
  }

  status_.handoff_qualified = false;
  neutral_timer_active_ = false;
  if (!status_.frontend_alive ||
      status_.link_state == remote::RemoteLinkState::Malformed ||
      status_.link_state == remote::RemoteLinkState::ProtocolFault) {
    status_.remote_reserved = true;
    status_.release_qualified = false;
    release_timer_active_ = false;
    return;
  }
  if (!release_timer_active_) {
    release_timer_active_ = true;
    release_since_ms_ = now_ms;
  }
  if (now_ms - release_since_ms_ >= config_.release_qualification_ms) {
    status_.release_qualified = true;
    status_.remote_reserved = false;
  } else {
    status_.remote_reserved = true;
  }
}

void RemoteControlRuntime::publishTelemetry(uint32_t now_ms) {
  if (now_ms - last_telemetry_ms_ < 20u) return;
  last_telemetry_ms_ = now_ms;
  remote::RemoteTelemetrySlot telemetry;
  telemetry.m7_time_ms = now_ms;
  telemetry.authority_state = static_cast<uint8_t>(status_.authority_state);
  telemetry.remote_link_state = static_cast<uint8_t>(status_.link_state);
  telemetry.flags = (status_.remote_valid ? 1u : 0u) |
      (status_.handoff_qualified ? 1u << 1 : 0u) |
      (status_.source_image_valid ? 1u << 2 : 0u);
  const char* mode = status_.source_image_valid ? "RC ACTIVE" : "CSM SAFE";
  strncpy(telemetry.flight_mode, mode, sizeof(telemetry.flight_mode) - 1u);
  (void)remote::publishRemoteTelemetry(telemetry);
}

bool RemoteControlRuntime::isNeutralSample(
    const remote::M4RemoteMailboxSnapshot& snapshot) const {
  return snapshot.sample_present && snapshot.integrity_ok &&
      snapshot.sample.sample_state == remote::RcSampleState::Ok &&
      (snapshot.sample.ch[config_.drive_channel_index] >=
       -static_cast<int16_t>(config_.neutral_deadband_permille)) &&
      (snapshot.sample.ch[config_.drive_channel_index] <=
       static_cast<int16_t>(config_.neutral_deadband_permille)) &&
      (snapshot.sample.ch[config_.steering_channel_index] >=
       -static_cast<int16_t>(config_.neutral_deadband_permille)) &&
      (snapshot.sample.ch[config_.steering_channel_index] <=
       static_cast<int16_t>(config_.neutral_deadband_permille));
}

bool RemoteControlRuntime::buildSourceImage(
    uint32_t now_ms, const RemoteControlRuntimeInputs& inputs,
    RemoteControlRuntimeOutput* output) {
  RemoteControlOrchestratorInputs orchestrator_inputs;
  orchestrator_inputs.mailbox_snapshot = mailbox_reader_.snapshot();
  orchestrator_inputs.output_sequence = status_.source_lease_sequence + 1u;
  orchestrator_inputs.autonomy_state = inputs.autonomy_state;
  orchestrator_inputs.local_tx_inhibit_latched = inputs.local_tx_inhibit_latched;
  orchestrator_inputs.estop_asserted = inputs.estop_asserted;
  orchestrator_inputs.fault_lockout = inputs.fault_lockout;
  orchestrator_inputs.safety_supervisor_allows = inputs.hard_safety_allows;
  orchestrator_inputs.remote_source_present = status_.remote_valid;
  orchestrator_inputs.remote_handoff_qualified = status_.handoff_qualified;
  orchestrator_inputs.remote_takeover_request = status_.remote_valid;
  orchestrator_inputs.backend_state = inputs.backend_state;
  RemoteControlOrchestratorDeps deps;
  deps.authority_manager = &authority_manager_;
  deps.command_limiter = &command_limiter_;
  deps.vehicle_mapper = &vehicle_mapper_;
  deps.can_tx_gateway = &can_tx_gateway_;
  const RemoteControlOrchestratorResult result =
      orchestrator_.tick(now_ms, orchestrator_inputs, deps);
  status_.authority_state = result.authority_decision.authority_state;
  status_.active_source = result.authority_decision.source;
  status_.last_decision = result.decision;
  if (!result.accepted || result.frame_count == 0u) return false;

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
  // RC does not command EHB; its coherent source image explicitly owns a
  // neutral lane instead of mixing the previous Host lane into one snapshot.
  next[control_island::kLane364].valid = 1u;
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
