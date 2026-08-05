#include "board/control/RemoteControlRuntime.h"

#include <string.h>

namespace csm::board::control {
namespace {

bool timeReached(uint32_t now_ms, uint32_t deadline_ms) {
  return (now_ms - deadline_ms) < 0x80000000u;
}

void saturatingAdd(uint32_t increment, uint32_t* value) {
  if (value == nullptr || increment == 0) return;
  if (increment > UINT32_MAX - *value) {
    *value = UINT32_MAX;
  } else {
    *value += increment;
  }
}

int16_t absoluteValue(int16_t value) {
  return value < 0 ? static_cast<int16_t>(-value) : value;
}

}  // namespace

bool RemoteControlRuntime::begin(uint32_t now_ms, uint32_t m7_boot_id,
                                 const RemoteControlRuntimeConfig& config) {
  if (!config.configured || config.bus == authority::kAuthorityNoBus ||
      config.cycle_period_ms < 5 || config.cycle_period_ms > 100 ||
      config.steering_period_ms < config.cycle_period_ms ||
      config.steering_period_ms > 100 ||
      (config.steering_period_ms % config.cycle_period_ms) != 0 ||
      config.frame_gap_ms >= config.cycle_period_ms ||
      config.neutral_deadband_permille > 250 ||
      config.drive_deadband_permille > 100 ||
      config.steering_deadband_permille > 100 ||
      config.auxiliary_threshold_permille < 100 ||
      config.auxiliary_threshold_permille > 1000 ||
      config.steering_step_permille == 0 ||
      config.steering_return_step_permille == 0) {
    return false;
  }
  config_ = config;
  status_ = {};
  status_.configured = true;
  status_.local_can_tx_enabled = config.local_can_tx_enabled;
  status_.remote_reserved = true;

  remote::initializeRemoteSharedMemoryForM7(m7_boot_id);
  mailbox_reader_.begin(now_ms);
  authority_manager_.begin(now_ms);
  command_limiter_.begin(now_ms);
  vehicle_mapper_.begin(now_ms);
  can_tx_gateway_.begin(now_ms);
  orchestrator_.begin(now_ms);

  remote::RemoteControlSourceConfig source_config;
  source_config.drive_channel_index = 1;
  source_config.steering_channel_index = 3;
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
  limiter.throttle_rise_step_permille = static_cast<uint16_t>(
      50u * config.cycle_period_ms / 20u);
  limiter.throttle_fall_step_permille = static_cast<uint16_t>(
      200u * config.cycle_period_ms / 20u);
  limiter.steer_step_permille = static_cast<uint16_t>(
      config.steering_step_permille * config.cycle_period_ms / 20u);
  limiter.steer_return_step_permille = static_cast<uint16_t>(
      config.steering_return_step_permille * config.cycle_period_ms / 20u);
  if (!command_limiter_.configure(limiter)) return false;

  VehicleCommandProfile mapper;
  mapper.configured = true;
  mapper.output_enabled = config.local_can_tx_enabled;
  mapper.mapping = config.mapping;
  mapper.bus = config.bus;
  mapper.policy_id = config.policy_id;
  mapper.throttle_limit_permille = 1000;
  mapper.steer_limit_permille = 1000;
  mapper.brake_limit_permille = 1000;
  if (!vehicle_mapper_.configure(mapper)) return false;

  CanTxGatewayPolicy gateway;
  gateway.configured = true;
  gateway.build_profile_allows_local_tx = config.local_can_tx_enabled;
  gateway.bus = config.bus;
  gateway.policy_id = config.policy_id;
  if (config.mapping == VehicleCommandMapping::Vehicle0x005And0x007) {
    gateway.allowlist_count = 2;
    gateway.allowlist_ids[0] = kRemoteDriveCanId;
    gateway.allowlist_ids[1] = kRemoteSteeringCanId;
  } else if (config.mapping == VehicleCommandMapping::VehicleMdps0x007Only) {
    gateway.allowlist_count = 1;
    gateway.allowlist_ids[0] = kRemoteSteeringCanId;
  }
  if (!can_tx_gateway_.configure(gateway)) return false;

  return release_schedule_.begin(now_ms, config.cycle_period_ms,
                                 config.steering_period_ms);
}

RemoteControlRuntimeOutput RemoteControlRuntime::service(
    uint32_t now_ms, const RemoteControlRuntimeInputs& inputs) {
  RemoteControlRuntimeOutput output;
  if (!status_.configured) return output;

  const remote::RemoteSharedSampleReadResult shared =
      remote::readRemoteSharedSample(last_shared_sequence_);
  if (!shared.accepted) {
    if (shared.detail != 0) {
      ++status_.ipc_rejects;
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
      requestImmediateSilence(now_ms);
    }
    mailbox_reader_.updateFromMailboxFrame(now_ms, shared.slot.mailbox);
  } else {
    mailbox_reader_.update(now_ms, config_.m4_heartbeat_timeout_ms);
  }

  updateRemoteState(now_ms);
  status_.host_control_allowed = status_.frontend_alive &&
      status_.release_qualified && !status_.remote_reserved;

  if (prior_remote_valid_ && !status_.remote_valid) {
    requestImmediateSilence(now_ms);
  }
  prior_remote_valid_ = status_.remote_valid;

  publishTelemetry(now_ms);

  if (!config_.local_can_tx_enabled) {
    pending_frame_index_ = pending_frame_count_ = 0;
    return output;
  }

  const ControlReleaseBatch releases = release_schedule_.poll(now_ms);
  saturatingAdd(releases.steering.missed_releases,
                &status_.steering_release_misses);
  saturatingAdd(releases.drive.missed_releases,
                &status_.drive_release_misses);
  saturatingAdd(releases.drive.missed_releases,
                &status_.cycle_deadline_misses);
  if (releases.drive.due && releases.drive.scheduled_ms != now_ms) {
    saturatingAdd(1u, &status_.cycle_deadline_misses);
  }

  // Source loss is an asynchronous safety boundary. Prepare the exact drive
  // stop in this service call when the periodic lane is not already releasing.
  // The periodic phase remains absolute; only the adjacent slot is suppressed.
  if (immediate_stop_pending_ && !releases.drive.due &&
      scheduleSafetyStop(now_ms, inputs, true)) {
    immediate_stop_pending_ = false;
    require_silent_cycle_ = false;
    release_schedule_.noteDriveDispatch(now_ms);
  }

  if (releases.drive.due) {
    if (pending_frame_index_ < pending_frame_count_) {
      saturatingAdd(1u, &status_.cycle_deadline_misses);
      pending_frame_index_ = pending_frame_count_ = 0;
      requestImmediateSilence(now_ms);
    }
    beginCycle(now_ms, inputs, releases.drive.sequence,
               releases.steering.due);
  }

  if (pending_frame_index_ < pending_frame_count_ &&
      timeReached(now_ms, next_frame_ms_)) {
    output.frame_ready = true;
    output.frame = pending_frames_[pending_frame_index_];
  }
  return output;
}

void RemoteControlRuntime::noteCanTxEnqueueResult(uint32_t now_ms,
                                                  bool accepted,
                                                  bool terminal_failure) {
  if (pending_frame_index_ >= pending_frame_count_) return;
  if (accepted) {
    ++pending_frame_index_;
    next_frame_ms_ = now_ms + config_.frame_gap_ms;
    if (pending_frame_index_ >= pending_frame_count_) {
      pending_frame_index_ = pending_frame_count_ = 0;
    }
    return;
  }

  saturatingAdd(1u, &status_.can_tx_failed);
  saturatingAdd(1u, &status_.cycle_deadline_misses);
  const bool safety_neutral =
      pending_frames_[pending_frame_index_].source ==
      authority::ControlSourceId::SafetyNeutral;
  if (terminal_failure || safety_neutral) {
    latchCanTxInhibit(now_ms);
  } else {
    requestImmediateSilence(now_ms);
  }
}

void RemoteControlRuntime::noteCanTxCompletion(uint32_t now_ms,
                                               bool transmitted) {
  if (transmitted) {
    saturatingAdd(1u, &status_.can_tx_success);
    return;
  }
  saturatingAdd(1u, &status_.can_tx_failed);
  latchCanTxInhibit(now_ms);
}

void RemoteControlRuntime::clearCanTxInhibitForService(uint32_t now_ms) {
  status_.can_tx_inhibit_latched = false;
  requestImmediateSilence(now_ms);
}

void RemoteControlRuntime::updateRemoteState(uint32_t now_ms) {
  status_.frontend_alive = frontend_seen_ &&
      (now_ms - last_frontend_seen_ms_ <= config_.m4_heartbeat_timeout_ms);
  const remote::M4RemoteMailboxSnapshot& snapshot = mailbox_reader_.snapshot();
  status_.link_state = snapshot.link_state;
  status_.sample_age_ms = snapshot.age_ms;
  status_.link_quality = snapshot.sample.link_quality;
  status_.rssi_magnitude = snapshot.sample.rssi_hint;
  status_.drive_permille = snapshot.sample.ch[1];
  status_.steering_permille = snapshot.sample.ch[3];
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
  if (!status_.frontend_alive) {
    status_.remote_reserved = true;
    status_.release_qualified = false;
    release_timer_active_ = false;
    return;
  }

  if (status_.link_state == remote::RemoteLinkState::Malformed ||
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

void RemoteControlRuntime::requestImmediateSilence(uint32_t now_ms) {
  require_silent_cycle_ = true;
  immediate_stop_pending_ = true;
  command_limiter_.begin(now_ms);
  pending_frame_index_ = pending_frame_count_ = 0;
}

void RemoteControlRuntime::beginCycle(
    uint32_t now_ms, const RemoteControlRuntimeInputs& inputs,
    uint32_t drive_release_sequence, bool steering_release_due) {
  if (!inputs.hard_safety_allows || !inputs.hardware_gate_allows ||
      inputs.local_tx_inhibit_latched || status_.can_tx_inhibit_latched ||
      !inputs.backend_state.ready || inputs.backend_state.bus_off ||
      inputs.backend_state.error_passive) {
    pending_frame_index_ = pending_frame_count_ = 0;
    require_silent_cycle_ = true;
    return;
  }
  if (inputs.host_service_active && status_.host_control_allowed &&
      !status_.remote_reserved) {
    pending_frame_index_ = pending_frame_count_ = 0;
    return;
  }

  cycle_sequence_ = drive_release_sequence;
  if (require_silent_cycle_) {
    if (scheduleSafetyStop(now_ms, inputs,
                           immediate_stop_pending_ || steering_release_due)) {
      require_silent_cycle_ = false;
      immediate_stop_pending_ = false;
    }
    return;
  }
  if (status_.remote_valid && status_.handoff_qualified) {
    RemoteControlOrchestratorInputs orchestrator_inputs;
    orchestrator_inputs.mailbox_snapshot = mailbox_reader_.snapshot();
    orchestrator_inputs.output_sequence = cycle_sequence_;
    orchestrator_inputs.autonomy_state = inputs.autonomy_state;
    orchestrator_inputs.local_tx_inhibit_latched =
        inputs.local_tx_inhibit_latched;
    orchestrator_inputs.estop_asserted = inputs.estop_asserted;
    orchestrator_inputs.fault_lockout = inputs.fault_lockout;
    orchestrator_inputs.safety_supervisor_allows = inputs.hard_safety_allows;
    orchestrator_inputs.remote_source_present = status_.remote_reserved;
    orchestrator_inputs.remote_handoff_qualified = status_.handoff_qualified;
    orchestrator_inputs.remote_takeover_request = true;
    orchestrator_inputs.hardware_gate_allows = inputs.hardware_gate_allows;
    orchestrator_inputs.backend_state = inputs.backend_state;
    RemoteControlOrchestratorDeps deps;
    deps.authority_manager = &authority_manager_;
    deps.command_limiter = &command_limiter_;
    deps.vehicle_mapper = &vehicle_mapper_;
    deps.can_tx_gateway = &can_tx_gateway_;
    const RemoteControlOrchestratorResult result =
        orchestrator_.tick(now_ms, orchestrator_inputs, deps);
    status_.last_decision = result.decision;
    status_.authority_state = authority_manager_.state();
    status_.active_source = authority_manager_.activeSource();
    if (result.accepted) {
      pending_frame_count_ = 0;
      pending_frame_index_ = 0;
      for (uint8_t i = 0; i < result.frame_count; ++i) {
        const uint32_t can_id = result.frames[i].can_id_flags & 0x7FFu;
        if (can_id == kRemoteSteeringCanId && !steering_release_due) {
          continue;
        }
        pending_frames_[pending_frame_count_++] = result.frames[i];
      }
      next_frame_ms_ = now_ms;
      ++status_.control_cycles;
      return;
    }
  }
  scheduleSafetyStop(now_ms, inputs, steering_release_due);
}

bool RemoteControlRuntime::scheduleMappedFrames(
    uint32_t ready_ms, const VehicleCommandMapResult& mapped,
    const CanTxGatewayInputs& gateway_inputs) {
  if (!mapped.mapped || mapped.frame_count == 0) return false;
  for (uint8_t i = 0; i < mapped.frame_count; ++i) {
    const CanTxGatewayResult gate =
        can_tx_gateway_.evaluate(mapped.frames[i], gateway_inputs);
    if (!gate.accepted) {
      status_.last_decision = gate.decision;
      return false;
    }
  }
  pending_frame_count_ = mapped.frame_count;
  pending_frame_index_ = 0;
  for (uint8_t i = 0; i < mapped.frame_count; ++i) {
    pending_frames_[i] = mapped.frames[i];
  }
  next_frame_ms_ = ready_ms;
  return true;
}

bool RemoteControlRuntime::scheduleSafetyStop(
    uint32_t now_ms, const RemoteControlRuntimeInputs& inputs,
    bool steering_release_due) {
  if (inputs.local_tx_inhibit_latched || status_.can_tx_inhibit_latched ||
      inputs.autonomy_state != authority::AutonomyAuthorityState::InactiveConfirmed) {
    return false;
  }
  VehicleCommandMapResult mapped =
      vehicle_mapper_.mapSafetyStop(cycle_sequence_);
  if (!steering_release_due && mapped.mapped) {
    uint8_t retained = 0;
    for (uint8_t i = 0; i < mapped.frame_count; ++i) {
      if ((mapped.frames[i].can_id_flags & 0x7FFu) == kRemoteSteeringCanId) {
        continue;
      }
      mapped.frames[retained++] = mapped.frames[i];
    }
    mapped.frame_count = retained;
    mapped.mapped = retained != 0;
  }
  CanTxGatewayInputs gateway_inputs;
  gateway_inputs.authority_decision.code = authority::ControlDecisionCode::Accepted;
  gateway_inputs.authority_decision.source = authority::ControlSourceId::SafetyNeutral;
  gateway_inputs.authority_decision.autonomy_state = inputs.autonomy_state;
  gateway_inputs.local_tx_inhibit_latched = false;
  gateway_inputs.safety_supervisor_allows = inputs.hard_safety_allows;
  gateway_inputs.hardware_gate_allows = inputs.hardware_gate_allows;
  gateway_inputs.backend_state = inputs.backend_state;
  if (!scheduleMappedFrames(now_ms, mapped, gateway_inputs)) return false;
  ++status_.neutral_cycles;
  return true;
}

void RemoteControlRuntime::latchCanTxInhibit(uint32_t now_ms) {
  status_.can_tx_inhibit_latched = true;
  requestImmediateSilence(now_ms);
}

void RemoteControlRuntime::publishTelemetry(uint32_t now_ms) {
  if (now_ms - last_telemetry_ms_ < 100u) return;
  last_telemetry_ms_ = now_ms;
  remote::RemoteTelemetrySlot telemetry;
  telemetry.m7_time_ms = now_ms;
  telemetry.authority_state = static_cast<uint8_t>(status_.authority_state);
  telemetry.remote_link_state = static_cast<uint8_t>(status_.link_state);
  telemetry.flags = (status_.remote_valid ? 0x01u : 0u) |
                    (status_.handoff_qualified ? 0x02u : 0u) |
                    (status_.release_qualified ? 0x04u : 0u);
  const char* mode = status_.handoff_qualified ? "RC ACTIVE" :
      (status_.remote_valid ? "RC NEUTRAL" :
       (status_.release_qualified ? "RC RELEASED" : "RC LOST"));
  strncpy(telemetry.flight_mode, mode, sizeof(telemetry.flight_mode) - 1u);
  remote::publishRemoteTelemetry(telemetry);
}

bool RemoteControlRuntime::isNeutralSample(
    const remote::M4RemoteMailboxSnapshot& snapshot) const {
  return absoluteValue(snapshot.sample.ch[1]) <=
             static_cast<int16_t>(config_.neutral_deadband_permille) &&
         absoluteValue(snapshot.sample.ch[3]) <=
             static_cast<int16_t>(config_.neutral_deadband_permille);
}

}  // namespace csm::board::control
