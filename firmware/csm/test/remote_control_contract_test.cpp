#include <cstdio>
#include <cstring>

#include "board/authority/AuthorityManager.h"
#include "board/control/CanTxGateway.h"
#include "board/control/CommandLimiter.h"
#include "board/control/RemoteControlOrchestrator.h"
#include "board/control/RemoteControlRuntime.h"
#include "board/control/VehicleCommandMapper.h"
#include "board/remote/CrsfParser.h"
#include "board/remote/M4RemoteMailboxReader.h"
#include "board/remote/M4RemoteMailboxWriter.h"
#include "board/remote/RcNormalizer.h"
#include "board/remote/RemoteSharedMemory.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void packChannels(const uint16_t channels[16], uint8_t payload[22]) {
  std::memset(payload, 0, 22);
  uint32_t bit_offset = 0;
  for (uint8_t channel = 0; channel < 16; ++channel) {
    for (uint8_t bit = 0; bit < 11; ++bit, ++bit_offset) {
      if ((channels[channel] & (1u << bit)) != 0) {
        payload[bit_offset / 8u] |= static_cast<uint8_t>(1u << (bit_offset % 8u));
      }
    }
  }
}

csm::board::remote::CrsfFrame parseFrame(const uint8_t* bytes, uint8_t length,
                                         csm::board::remote::CrsfParseStatus* status) {
  csm::board::remote::CrsfParser parser;
  parser.reset();
  csm::board::remote::CrsfParseResult result;
  for (uint8_t index = 0; index < length; ++index) result = parser.ingest(bytes[index]);
  *status = result.status;
  return result.frame;
}

void crsfChannelsDecodeAndNormalize() {
  using namespace csm::board::remote;
  uint16_t raw[16];
  for (uint8_t index = 0; index < 16; ++index) raw[index] = 992;
  raw[1] = 1811;  // CH2 drive.
  raw[3] = 172;   // CH4 steering.
  uint8_t payload[kCrsfRcChannelsPackedPayloadBytes];
  packChannels(raw, payload);
  uint8_t bytes[kCrsfMaxFrameBytes] = {};
  const uint8_t length = buildCrsfBroadcastFrame(
      kCrsfFrameTypeRcChannelsPacked, payload, sizeof(payload), bytes, sizeof(bytes));
  CHECK(length == 26);

  CrsfParseStatus parse_status = CrsfParseStatus::Waiting;
  const CrsfFrame frame = parseFrame(bytes, length, &parse_status);
  CHECK(parse_status == CrsfParseStatus::FrameReady);
  CrsfRcChannels decoded;
  CHECK(decodeCrsfRcChannelsPacked(frame, &decoded) == CrsfDecodeStatus::Ok);
  for (uint8_t index = 0; index < 16; ++index) CHECK(decoded.raw[index] == raw[index]);

  RcNormalizer normalizer;
  RcNormalizerConfig config;
  config.configured = true;
  CHECK(normalizer.configure(config));
  const RcNormalizeResult normalized =
      normalizer.normalizeCrsfChannels(100, 7, decoded, 96, 42, 0);
  CHECK(normalized.accepted);
  CHECK(normalized.sample.ch[1] == 1000);
  CHECK(normalized.sample.ch[3] == -1000);
  CHECK(normalized.sample.ch[0] == 0);

  bytes[length - 1u] ^= 0x01u;
  parseFrame(bytes, length, &parse_status);
  CHECK(parse_status == CrsfParseStatus::RejectedCrc);

  const uint8_t heartbeat_payload[2] = {0x00, 0xC8};
  const uint8_t heartbeat_len = buildCrsfBroadcastFrame(
      kCrsfFrameTypeHeartbeat, heartbeat_payload, sizeof(heartbeat_payload),
      bytes, sizeof(bytes));
  CHECK(heartbeat_len == 6);
  const CrsfFrame heartbeat = parseFrame(bytes, heartbeat_len, &parse_status);
  CHECK(parse_status == CrsfParseStatus::FrameReady);
  CHECK(heartbeat.type == kCrsfFrameTypeHeartbeat);
  CHECK(heartbeat.payload_len == 2);
  CHECK(heartbeat.payload[0] == 0x00 && heartbeat.payload[1] == 0xC8);
}

void upstreamAutonomyPrecedesRemoteReservation() {
  using namespace csm::board;
  authority::AuthorityManager manager;
  manager.begin(0);
  authority::AuthorityInputs inputs;
  inputs.autonomy_state = authority::AutonomyAuthorityState::ActiveConfirmed;
  inputs.host_service_enabled = true;
  inputs.host_service_request = true;
  inputs.safety_supervisor_allows = true;
  inputs.remote_source_present = true;
  inputs.remote_source_valid = true;
  inputs.remote_takeover_request = true;

  auto decision = manager.update(10, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedAutonomyActive);
  CHECK(manager.activeSource() == authority::ControlSourceId::None);

  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  decision = manager.update(15, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedNotNeutral);
  CHECK(decision.source == authority::ControlSourceId::Remote);

  inputs.remote_handoff_qualified = true;
  decision = manager.update(20, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::Accepted);
  CHECK(manager.activeSource() == authority::ControlSourceId::Remote);

  inputs.estop_asserted = true;
  inputs.autonomy_state = authority::AutonomyAuthorityState::ActiveConfirmed;
  decision = manager.update(30, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedSafetySupervisor);
  CHECK(manager.activeSource() == authority::ControlSourceId::None);

  inputs.estop_asserted = false;
  inputs.fault_lockout = true;
  decision = manager.update(31, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedFaultLockout);

  inputs.fault_lockout = false;
  inputs.local_tx_inhibit_latched = true;
  decision = manager.update(32, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedLocalTxInhibit);

  inputs.local_tx_inhibit_latched = false;
  inputs.safety_supervisor_allows = false;
  decision = manager.update(33, inputs);
  CHECK(decision.code == authority::ControlDecisionCode::RejectedSafetySupervisor);

  control::OperatorCommand command;
  command.source = authority::ControlSourceId::Remote;
  authority::AuthorityInputs evaluation = inputs;
  evaluation.safety_supervisor_allows = true;
  evaluation.estop_asserted = true;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedSafetySupervisor);
  evaluation.estop_asserted = false;
  evaluation.fault_lockout = true;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedFaultLockout);
  evaluation.fault_lockout = false;
  evaluation.local_tx_inhibit_latched = true;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedLocalTxInhibit);
  evaluation.local_tx_inhibit_latched = false;
  evaluation.safety_supervisor_allows = false;
  CHECK(manager.evaluateCommand(command, evaluation).code ==
        authority::ControlDecisionCode::RejectedSafetySupervisor);
}

void frozenMailboxCannotRemainFresh() {
  using namespace csm::board::remote;
  RcSample sample;
  sample.sample_state = RcSampleState::Ok;
  sample.seq = 1;
  M4RemoteMailboxWriter writer;
  M4RemoteMailboxFrame frame;
  writer.reset();
  writer.clearFrame(&frame);
  CHECK(writer.publishSample(sample, &frame).accepted);

  M4RemoteMailboxReader reader;
  reader.begin(0);
  CHECK(reader.updateFromMailboxFrame(10, frame));
  CHECK(reader.hasFreshUsableSample());
  CHECK(!reader.updateFromMailboxFrame(111, frame));
  CHECK(reader.snapshot().link_state == RemoteLinkState::Stale);
  CHECK(reader.snapshot().age_ms == 101);
}

void drivePayloadMatchesVehicleBenchGoldenFrames() {
  using namespace csm::board;
  control::VehicleCommandMapper mapper;
  mapper.begin(0);
  control::VehicleCommandProfile profile;
  profile.configured = true;
  profile.output_enabled = true;
  profile.mapping = control::VehicleCommandMapping::VehicleBench0x005And0x007;
  profile.bus = 1;
  profile.policy_id = 0x5243;
  profile.throttle_limit_permille = 1000;
  profile.steer_limit_permille = 1000;
  profile.brake_limit_permille = 1000;
  CHECK(mapper.configure(profile));

  auto check = [&](int16_t throttle, const uint8_t expected[8]) {
    control::OperatorCommand command;
    command.source = authority::ControlSourceId::Remote;
    command.throttle_permille = throttle;
    const control::VehicleCommandMapResult mapped = mapper.map(command);
    CHECK(mapped.mapped);
    CHECK(mapped.frame_count == 2);
    CHECK(mapped.frames[0].can_id_flags == control::kRemoteDriveCanId);
    CHECK(mapped.frames[0].dlc == 8);
    CHECK(std::memcmp(mapped.frames[0].data, expected, 8) == 0);
  };

  const uint8_t stop[8] =
      {0xAA, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  const uint8_t forward_80[8] =
      {0xAA, 0x52, 0x20, 0x03, 0x50, 0x00, 0x00, 0x00};
  const uint8_t forward_100[8] =
      {0xAA, 0x52, 0xE8, 0x03, 0x50, 0x00, 0x00, 0x00};
  const uint8_t reverse_80[8] =
      {0xAA, 0x52, 0x20, 0x03, 0x60, 0x00, 0x00, 0x00};
  const uint8_t reverse_100[8] =
      {0xAA, 0x52, 0xE8, 0x03, 0x60, 0x00, 0x00, 0x00};
  check(0, stop);
  check(800, forward_80);
  check(1000, forward_100);
  check(-800, reverse_80);
  check(-1000, reverse_100);
}

void remotePreemptsAutonomyAndMapsCh4Ch5Ch10Ch11() {
  using namespace csm::board;
  authority::AuthorityManager authority_manager;
  control::CommandLimiter limiter;
  control::VehicleCommandMapper mapper;
  control::CanTxGateway gateway;
  control::RemoteControlOrchestrator orchestrator;
  authority_manager.begin(0);
  limiter.begin(0);
  mapper.begin(0);
  gateway.begin(0);
  orchestrator.begin(0);

  control::CommandLimiterConfig limiter_config;
  limiter_config.configured = true;
  limiter_config.throttle_min_permille = -1000;
  limiter_config.throttle_max_permille = 1000;
  limiter_config.steer_min_permille = -1000;
  limiter_config.steer_max_permille = 1000;
  limiter_config.brake_min_permille = 0;
  limiter_config.brake_max_permille = 1000;
  limiter_config.throttle_rise_step_permille = 1000;
  limiter_config.throttle_fall_step_permille = 1000;
  limiter_config.steer_step_permille = 1000;
  limiter_config.steer_return_step_permille = 1000;
  CHECK(limiter.configure(limiter_config));

  control::VehicleCommandProfile vehicle_profile;
  vehicle_profile.configured = true;
  vehicle_profile.output_enabled = true;
  vehicle_profile.mapping = control::VehicleCommandMapping::VehicleBench0x005And0x007;
  vehicle_profile.bus = 1;
  vehicle_profile.policy_id = 0x5243;
  vehicle_profile.throttle_limit_permille = 1000;
  vehicle_profile.steer_limit_permille = 1000;
  vehicle_profile.brake_limit_permille = 1000;
  CHECK(mapper.configure(vehicle_profile));

  control::CanTxGatewayPolicy gateway_policy;
  gateway_policy.configured = true;
  gateway_policy.build_profile_allows_local_tx = true;
  gateway_policy.bus = 1;
  gateway_policy.policy_id = 0x5243;
  gateway_policy.allowlist_count = 2;
  gateway_policy.allowlist_ids[0] = control::kRemoteDriveCanId;
  gateway_policy.allowlist_ids[1] = control::kRemoteSteeringCanId;
  CHECK(gateway.configure(gateway_policy));

  remote::RemoteControlSourceConfig source_config;
  source_config.drive_channel_index = 1;
  source_config.steering_channel_index = 3;
  source_config.auxiliary_channel_index = 4;
  source_config.steering_overlay_channel_index = 9;
  source_config.momentary_overlay_channel_index = 10;
  source_config.drive_deadband_permille = 20;
  source_config.steering_deadband_permille = 20;
  source_config.auxiliary_threshold_permille = 500;
  CHECK(orchestrator.configureRemoteSource(source_config));

  control::RemoteControlOrchestratorInputs inputs;
  inputs.mailbox_snapshot.sample_present = true;
  inputs.mailbox_snapshot.integrity_ok = true;
  inputs.mailbox_snapshot.link_state = remote::RemoteLinkState::Valid;
  inputs.mailbox_snapshot.sample.sample_state = remote::RcSampleState::Ok;
  inputs.mailbox_snapshot.sample.seq = 99;
  inputs.mailbox_snapshot.sample.ch[1] = 10;
  inputs.mailbox_snapshot.sample.ch[3] = 0;
  inputs.mailbox_snapshot.sample.ch[4] = 0;
  inputs.mailbox_snapshot.sample.ch[9] = 0;
  inputs.mailbox_snapshot.sample.ch[10] = -1000;
  inputs.output_sequence = 6;
  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  inputs.local_tx_inhibit_latched = false;
  inputs.safety_supervisor_allows = true;
  inputs.remote_source_present = true;
  inputs.remote_handoff_qualified = true;
  inputs.remote_takeover_request = true;
  inputs.hardware_gate_allows = true;
  inputs.backend_state.ready = true;

  control::RemoteControlOrchestratorDeps deps;
  deps.authority_manager = &authority_manager;
  deps.command_limiter = &limiter;
  deps.vehicle_mapper = &mapper;
  deps.can_tx_gateway = &gateway;
  const auto drive_deadband = orchestrator.tick(10, inputs, deps);
  CHECK(drive_deadband.accepted);
  CHECK(drive_deadband.command.throttle_permille == 0);
  CHECK(drive_deadband.frames[0].data[1] == control::kRemoteDriveStopMode);

  inputs.mailbox_snapshot.sample.ch[1] = 1000;
  inputs.mailbox_snapshot.sample.ch[3] = -1000;
  inputs.output_sequence = 7;
  const auto result = orchestrator.tick(20, inputs, deps);
  CHECK(result.accepted);
  CHECK(result.command.command_seq == 7);
  CHECK(result.command.throttle_permille == 1000);
  CHECK(result.command.steer_permille == -1000);
  CHECK(result.authority_decision.source == authority::ControlSourceId::Remote);
  CHECK(result.command.auxiliary_permille == 0);
  CHECK(result.frame_count == 2);
  CHECK(result.frames[0].can_id_flags == control::kRemoteDriveCanId);
  CHECK(result.frames[0].data[0] == control::kRemoteDriveHeader);
  CHECK(result.frames[0].data[1] == control::kRemoteDriveMode);
  CHECK(result.frames[0].data[2] == 0xE8);
  CHECK(result.frames[0].data[3] == 0x03);
  CHECK(result.frames[0].data[4] == control::kRemoteDriveForward);
  CHECK(result.frames[1].can_id_flags == control::kRemoteSteeringCanId);
  CHECK(result.frames[1].data[0] == control::kRemoteSteeringMinimum);
  for (uint8_t index = 1; index < 8; ++index) {
    CHECK(result.frames[1].data[index] == 0);
  }

  inputs.output_sequence = 8;
  inputs.mailbox_snapshot.sample.ch[3] = 10;
  auto deadband = orchestrator.tick(40, inputs, deps);
  CHECK(deadband.accepted);
  CHECK(deadband.command.steer_permille == 0);
  CHECK(deadband.frames[1].data[0] == control::kRemoteSteeringCenter);

  inputs.output_sequence = 9;
  inputs.mailbox_snapshot.sample.ch[3] = 1000;
  inputs.mailbox_snapshot.sample.ch[4] = -1000;
  auto auxiliary_negative = orchestrator.tick(60, inputs, deps);
  CHECK(auxiliary_negative.accepted);
  CHECK(auxiliary_negative.command.throttle_permille == 0);
  CHECK(auxiliary_negative.command.steer_permille == 0);
  CHECK(auxiliary_negative.command.auxiliary_permille == -1000);
  CHECK(auxiliary_negative.frames[0].data[1] == control::kRemoteDriveStopMode);
  for (uint8_t index = 0; index < 7; ++index) {
    CHECK(auxiliary_negative.frames[1].data[index] == 0);
  }
  CHECK(auxiliary_negative.frames[1].data[7] == control::kRemoteAuxiliaryNegative);

  inputs.output_sequence = 10;
  inputs.mailbox_snapshot.sample.ch[4] = 1000;
  auto auxiliary_positive = orchestrator.tick(80, inputs, deps);
  CHECK(auxiliary_positive.accepted);
  CHECK(auxiliary_positive.command.auxiliary_permille == 1000);
  for (uint8_t index = 0; index < 7; ++index) {
    CHECK(auxiliary_positive.frames[1].data[index] == 0);
  }
  CHECK(auxiliary_positive.frames[1].data[7] == control::kRemoteAuxiliaryPositive);

  inputs.output_sequence = 11;
  inputs.mailbox_snapshot.sample.ch[4] = 0;
  auto steering_positive = orchestrator.tick(100, inputs, deps);
  CHECK(steering_positive.accepted);
  CHECK(steering_positive.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(steering_positive.frames[1].data[7] == 0);

  inputs.output_sequence = 12;
  inputs.mailbox_snapshot.sample.ch[9] = 1000;
  auto steering_overlay_positive = orchestrator.tick(120, inputs, deps);
  CHECK(steering_overlay_positive.accepted);
  CHECK(steering_overlay_positive.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(steering_overlay_positive.frames[1].data[7] == control::kRemoteAuxiliaryPositive);

  inputs.output_sequence = 13;
  inputs.mailbox_snapshot.sample.ch[9] = -1000;
  auto steering_overlay_negative = orchestrator.tick(140, inputs, deps);
  CHECK(steering_overlay_negative.accepted);
  CHECK(steering_overlay_negative.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(steering_overlay_negative.frames[1].data[7] == control::kRemoteAuxiliaryNegative);

  inputs.output_sequence = 14;
  inputs.mailbox_snapshot.sample.ch[9] = 1000;
  inputs.mailbox_snapshot.sample.ch[10] = 1000;
  auto momentary_overlay_positive = orchestrator.tick(160, inputs, deps);
  CHECK(momentary_overlay_positive.accepted);
  CHECK(momentary_overlay_positive.frames[1].data[0] == control::kRemoteSteeringMaximum);
  CHECK(momentary_overlay_positive.frames[1].data[7] == control::kRemoteAuxiliaryNegative);

  inputs.output_sequence = 15;
  inputs.mailbox_snapshot.sample.ch[4] = 1000;
  auto auxiliary_precedence = orchestrator.tick(180, inputs, deps);
  CHECK(auxiliary_precedence.accepted);
  for (uint8_t index = 0; index < 7; ++index) {
    CHECK(auxiliary_precedence.frames[1].data[index] == 0);
  }
  CHECK(auxiliary_precedence.frames[1].data[7] == control::kRemoteAuxiliaryPositive);
}

void runtimeHandoffLossAndFaultPolicy() {
  using namespace csm::board;
  control::RemoteControlRuntime runtime;
  control::RemoteControlRuntimeConfig config;
  config.configured = true;
  config.local_can_tx_enabled = true;
  config.mapping = control::VehicleCommandMapping::VehicleBench0x005And0x007;
  config.bus = 1;
  config.policy_id = 0x5243;
  config.cycle_period_ms = 5;
  config.steering_period_ms = 20;
  config.frame_gap_ms = 2;
  config.m4_heartbeat_timeout_ms = 100;
  config.neutral_qualification_ms = 500;
  config.release_qualification_ms = 1000;
  config.neutral_deadband_permille = 50;
  config.drive_deadband_permille = 20;
  config.steering_deadband_permille = 20;
  config.auxiliary_threshold_permille = 500;
  config.steering_step_permille = 30;
  config.steering_return_step_permille = 50;
  config.max_forward_rpm = 500;
  config.max_reverse_rpm = 500;
  config.max_steering_deci_degree = 450;
  CHECK(runtime.begin(0, 0x1234, config));

  control::RemoteControlRuntimeInputs inputs;
  inputs.hard_safety_allows = true;
  inputs.local_tx_inhibit_latched = false;
  inputs.hardware_gate_allows = true;
  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  inputs.backend_state.ready = true;

  remote::M4RemoteMailboxWriter writer;
  remote::M4RemoteMailboxFrame mailbox;
  remote::RemoteFrontendDiagnostics diagnostics;
  writer.reset();
  writer.clearFrame(&mailbox);
  const uint32_t m4_boot_id = remote::initializeRemoteSharedMemoryForM4();
  uint32_t heartbeat = 0;

  remote::RcSample sample;
  sample.sample_state = remote::RcSampleState::Ok;
  sample.seq = 1;
  auto publish = [&](uint32_t now_ms) {
    sample.m4_time_ms = now_ms;
    CHECK(writer.publishSample(sample, &mailbox).accepted);
    CHECK(remote::publishRemoteSharedSample(
        m4_boot_id, ++heartbeat, mailbox, diagnostics));
  };

  control::CanFrameRequest first_motion_frame;
  bool saw_first_motion_frame = false;
  bool drive_period_ok = true;
  bool steering_period_ok = true;
  bool has_previous_drive = false;
  bool has_previous_steering = false;
  uint32_t previous_drive_ms = 0;
  uint32_t previous_steering_ms = 0;
  auto serviceRange = [&](uint32_t begin_ms, uint32_t end_ms,
                          bool refresh_frontend) {
    uint32_t emitted_frames = 0;
    for (uint32_t now_ms = begin_ms; now_ms <= end_ms; ++now_ms) {
      if (refresh_frontend && (now_ms % 20u) == 0u) publish(now_ms);
      const auto output = runtime.service(now_ms, inputs);
      if (output.frame_ready) {
        ++emitted_frames;
        const uint32_t can_id = output.frame.can_id_flags & 0x7FFu;
        if (can_id == control::kRemoteDriveCanId) {
          if (has_previous_drive && now_ms - previous_drive_ms != 5u) {
            drive_period_ok = false;
          }
          previous_drive_ms = now_ms;
          has_previous_drive = true;
        } else if (can_id == control::kRemoteSteeringCanId) {
          if (has_previous_steering && now_ms - previous_steering_ms != 20u) {
            steering_period_ok = false;
          }
          previous_steering_ms = now_ms;
          has_previous_steering = true;
        }
        if (!saw_first_motion_frame &&
            output.frame.can_id_flags == control::kRemoteSteeringCanId &&
            output.frame.data[0] != control::kRemoteSteeringCenter) {
          first_motion_frame = output.frame;
          saw_first_motion_frame = true;
        }
        runtime.noteCanTxResult(now_ms, true);
      }
    }
    return emitted_frames;
  };

  // Until RC is qualified, the released bench emits only the explicit 0x005
  // stop contract at 200 Hz.
  CHECK(serviceRange(0, 499, true) == 100);
  CHECK(serviceRange(500, 500, true) == 1);
  CHECK(runtime.status().frontend_alive);
  CHECK(runtime.status().remote_reserved);
  CHECK(runtime.status().remote_valid);
  CHECK(runtime.status().handoff_qualified);
  CHECK(runtime.status().active_source == authority::ControlSourceId::Remote);
  CHECK(runtime.status().neutral_cycles >= 50);

  sample.ch[1] = 1000;
  sample.ch[3] = -1000;
  ++sample.seq;
  serviceRange(501, 540, true);
  CHECK(saw_first_motion_frame);
  CHECK(first_motion_frame.can_id_flags == control::kRemoteSteeringCanId);
  CHECK(first_motion_frame.data[0] < control::kRemoteSteeringCenter);
  CHECK(first_motion_frame.data[0] > control::kRemoteSteeringMinimum);
  CHECK(first_motion_frame.data[7] == 0);
  CHECK(drive_period_ok);
  CHECK(steering_period_ok);
  CHECK(runtime.status().control_cycles > 0);

  // Upstream autonomy states fail closed even with a fresh, qualified RC
  // source and motion command. No neutral fallback may be synthesized.
  const authority::AutonomyAuthorityState blocked_states[] = {
      authority::AutonomyAuthorityState::Unknown,
      authority::AutonomyAuthorityState::ActiveConfirmed,
      authority::AutonomyAuthorityState::RecentlyActive,
      authority::AutonomyAuthorityState::ProtocolFault,
  };
  uint32_t blocked_begin_ms = 541;
  for (const auto state : blocked_states) {
    has_previous_drive = false;
    has_previous_steering = false;
    inputs.autonomy_state = state;
    CHECK(serviceRange(blocked_begin_ms, blocked_begin_ms + 39u, true) == 0);
    blocked_begin_ms += 40u;
  }

  inputs.autonomy_state = authority::AutonomyAuthorityState::InactiveConfirmed;
  inputs.local_tx_inhibit_latched = true;
  CHECK(serviceRange(701, 740, true) == 0);
  inputs.local_tx_inhibit_latched = false;
  has_previous_drive = false;
  has_previous_steering = false;
  CHECK(serviceRange(741, 780, true) > 0);

  sample.sample_state = remote::RcSampleState::Stale;
  sample.ch[1] = 0;
  sample.ch[3] = 0;
  ++sample.seq;
  const uint32_t tx_before_link_loss = runtime.status().can_tx_success;
  CHECK(serviceRange(781, 800, true) >= 2);
  CHECK(!runtime.status().remote_valid);
  CHECK(runtime.status().remote_reserved);
  CHECK(!runtime.status().release_qualified);
  CHECK(runtime.status().neutral_cycles > 50);
  CHECK(runtime.status().can_tx_success > tx_before_link_loss);

  CHECK(serviceRange(801, 1800, true) >= 99);
  CHECK(runtime.status().release_qualified);
  CHECK(!runtime.status().remote_reserved);
  CHECK(runtime.status().host_control_allowed);

  sample.sample_state = remote::RcSampleState::ProtocolFault;
  ++sample.seq;
  CHECK(serviceRange(1801, 2940, true) >= 113);
  CHECK(!runtime.status().release_qualified);
  CHECK(runtime.status().remote_reserved);
  CHECK(!runtime.status().host_control_allowed);

  CHECK(serviceRange(2941, 3060, false) >= 11);
  CHECK(!runtime.status().frontend_alive);
  CHECK(runtime.status().remote_reserved);
  CHECK(runtime.status().ipc_rejects == 0);
  CHECK(runtime.status().last_ipc_reject_detail == 0);
}

}  // namespace

int main() {
  crsfChannelsDecodeAndNormalize();
  upstreamAutonomyPrecedesRemoteReservation();
  frozenMailboxCannotRemainFresh();
  drivePayloadMatchesVehicleBenchGoldenFrames();
  remotePreemptsAutonomyAndMapsCh4Ch5Ch10Ch11();
  runtimeHandoffLossAndFaultPolicy();
  if (failures != 0) {
    std::fprintf(stderr, "%d remote control contract checks failed\n", failures);
    return 1;
  }
  std::puts("remote control contract checks passed");
  return 0;
}
