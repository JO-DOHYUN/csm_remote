#pragma once

#include <stdint.h>

#include "board/authority/AuthorityManager.h"
#include "board/control/RemoteControlOrchestrator.h"
#include "board/control_island/ControlIslandContract.h"
#include "board/remote/M4RemoteMailboxReader.h"
#include "board/remote/RemoteSharedMemory.h"

namespace csm::board::control {

// M7-only RC semantic producer. It owns channel interpretation, elapsed-time
// limiting and HNO1 payload mapping; it owns no CAN clock or physical queue.
struct RemoteControlRuntimeConfig {
  bool configured = false;
  bool semantic_output_enabled = false;
  VehicleCommandMapping mapping = VehicleCommandMapping::None;
  uint8_t bus = authority::kAuthorityNoBus;
  uint16_t policy_id = 0;
  uint16_t semantic_update_period_ms = 5;
  uint16_t m4_heartbeat_timeout_ms = 100;
  uint16_t neutral_qualification_ms = 500;
  uint16_t release_qualification_ms = 1000;
  uint16_t neutral_deadband_permille = 50;
  uint16_t drive_deadband_permille = 20;
  uint16_t steering_deadband_permille = 20;
  uint16_t auxiliary_threshold_permille = 500;
  uint16_t steering_step_permille_per_20ms = 30;
  uint16_t steering_return_step_permille_per_20ms = 50;
  uint16_t max_forward_rpm = 500;
  uint16_t max_reverse_rpm = 500;
  uint16_t max_steering_deci_degree = 450;
  uint8_t drive_channel_index = 1;
  uint8_t steering_channel_index = 3;
  bool invert_drive = false;
  bool invert_steering = false;
};

struct RemoteControlRuntimeInputs {
  bool hard_safety_allows = false;
  bool estop_asserted = false;
  bool fault_lockout = false;
  bool local_tx_inhibit_latched = true;
  bool host_output_reserved = false;
  authority::AutonomyAuthorityState autonomy_state =
      authority::AutonomyAuthorityState::Unknown;
  can::CanBackendState backend_state = {};
};

struct RemoteControlRuntimeStatus {
  bool configured = false;
  bool semantic_output_enabled = false;
  bool frontend_alive = false;
  bool remote_reserved = true;
  bool remote_valid = false;
  bool neutral_now = false;
  bool handoff_qualified = false;
  bool release_qualified = false;
  bool host_control_allowed = false;
  bool source_image_valid = false;
  remote::RemoteLinkState link_state = remote::RemoteLinkState::NoFrame;
  authority::AuthorityState authority_state = authority::AuthorityState::BootInhibit;
  authority::ControlSourceId active_source = authority::ControlSourceId::None;
  authority::ControlDecisionCode last_decision =
      authority::ControlDecisionCode::RejectedBuildProfile;
  uint32_t m4_boot_id = 0;
  uint32_t shared_sequence = 0;
  uint32_t sample_age_ms = 0;
  uint32_t source_image_generation = 0;
  uint32_t source_lease_sequence = 0;
  int16_t drive_permille = 0;
  int16_t steering_permille = 0;
  int16_t auxiliary_permille = 0;
  uint16_t raw_ch2 = 0;
  uint16_t raw_ch4 = 0;
  uint8_t link_quality = remote::kRemoteMetricUnknown;
  uint8_t rssi_magnitude = remote::kRemoteMetricUnknown;
  uint32_t semantic_updates = 0;
  uint32_t semantic_rejects = 0;
  uint32_t ipc_rejects = 0;
  uint8_t last_ipc_reject_detail = 0;
  remote::RemoteFrontendDiagnostics frontend_diagnostics = {};
};

struct RemoteControlRuntimeOutput {
  bool source_valid = false;
  bool image_changed = false;
  uint32_t image_generation = 0;
  uint32_t lease_sequence = 0;
  control_island::LaneExecutionImage lanes[control_island::kLaneCount] = {};
};

class RemoteControlRuntime {
 public:
  bool begin(uint32_t now_ms, uint32_t m7_boot_id,
             const RemoteControlRuntimeConfig& config);
  RemoteControlRuntimeOutput service(uint32_t now_ms,
                                     const RemoteControlRuntimeInputs& inputs);

  bool hostControlAllowed() const { return status_.host_control_allowed; }
  const RemoteControlRuntimeConfig& config() const { return config_; }
  const RemoteControlRuntimeStatus& status() const { return status_; }
  const remote::M4RemoteMailboxSnapshot& mailboxSnapshot() const {
    return mailbox_reader_.snapshot();
  }

 private:
  void updateRemoteState(uint32_t now_ms);
  void publishTelemetry(uint32_t now_ms);
  bool isNeutralSample(const remote::M4RemoteMailboxSnapshot& snapshot) const;
  bool buildSourceImage(uint32_t now_ms,
                        const RemoteControlRuntimeInputs& inputs,
                        RemoteControlRuntimeOutput* output);
  void invalidateSource();
  static bool sameImage(const control_island::LaneExecutionImage* lhs,
                        const control_island::LaneExecutionImage* rhs);

  RemoteControlRuntimeConfig config_ = {};
  RemoteControlRuntimeStatus status_ = {};
  remote::M4RemoteMailboxReader mailbox_reader_ = {};
  authority::AuthorityManager authority_manager_ = {};
  CommandLimiter command_limiter_ = {};
  VehicleCommandMapper vehicle_mapper_ = {};
  CanTxGateway can_tx_gateway_ = {};
  RemoteControlOrchestrator orchestrator_ = {};
  control_island::LaneExecutionImage source_lanes_[control_island::kLaneCount] = {};

  uint32_t last_shared_sequence_ = 0;
  uint32_t last_frontend_seen_ms_ = 0;
  uint32_t neutral_since_ms_ = 0;
  uint32_t release_since_ms_ = 0;
  uint32_t last_semantic_update_ms_ = 0;
  uint32_t last_telemetry_ms_ = 0;
  uint32_t observed_m4_boot_id_ = 0;
  bool frontend_seen_ = false;
  bool neutral_timer_active_ = false;
  bool release_timer_active_ = false;
};

}  // namespace csm::board::control
