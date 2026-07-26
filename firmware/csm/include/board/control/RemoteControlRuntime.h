#pragma once

#include <stdint.h>

#include "board/authority/AuthorityManager.h"
#include "board/control/ControlReleaseSchedule.h"
#include "board/control/RemoteControlOrchestrator.h"
#include "board/remote/M4RemoteMailboxReader.h"
#include "board/remote/RemoteSharedMemory.h"

namespace csm::board::control {

struct RemoteControlRuntimeConfig {
  bool configured = false;
  bool local_can_tx_enabled = false;
  VehicleCommandMapping mapping = VehicleCommandMapping::None;
  uint8_t bus = authority::kAuthorityNoBus;
  uint16_t policy_id = 0;
  uint16_t cycle_period_ms = 5;
  uint16_t steering_period_ms = 20;
  uint16_t frame_gap_ms = 0;
  uint16_t m4_heartbeat_timeout_ms = 100;
  uint16_t neutral_qualification_ms = 500;
  uint16_t release_qualification_ms = 1000;
  uint16_t neutral_deadband_permille = 50;
  uint16_t drive_deadband_permille = 20;
  uint16_t steering_deadband_permille = 20;
  uint16_t auxiliary_threshold_permille = 500;
  uint16_t steering_step_permille = 30;
  uint16_t steering_return_step_permille = 50;
  uint16_t max_forward_rpm = 500;
  uint16_t max_reverse_rpm = 500;
  uint16_t max_steering_deci_degree = 450;
  bool invert_drive = false;
  bool invert_steering = false;
};

struct RemoteControlRuntimeInputs {
  bool hard_safety_allows = false;
  bool estop_asserted = false;
  bool fault_lockout = false;
  bool local_tx_inhibit_latched = true;
  bool hardware_gate_allows = false;
  bool host_service_active = false;
  authority::AutonomyAuthorityState autonomy_state =
      authority::AutonomyAuthorityState::Unknown;
  can::CanBackendState backend_state = {};
};

struct RemoteControlRuntimeStatus {
  bool configured = false;
  bool local_can_tx_enabled = false;
  bool frontend_alive = false;
  bool remote_reserved = true;
  bool remote_valid = false;
  bool neutral_now = false;
  bool handoff_qualified = false;
  bool release_qualified = false;
  bool host_control_allowed = false;
  remote::RemoteLinkState link_state = remote::RemoteLinkState::NoFrame;
  authority::AuthorityState authority_state = authority::AuthorityState::BootInhibit;
  authority::ControlSourceId active_source = authority::ControlSourceId::None;
  authority::ControlDecisionCode last_decision =
      authority::ControlDecisionCode::RejectedBuildProfile;
  uint32_t m4_boot_id = 0;
  uint32_t shared_sequence = 0;
  uint32_t sample_age_ms = 0;
  int16_t drive_permille = 0;
  int16_t steering_permille = 0;
  int16_t auxiliary_permille = 0;
  uint16_t raw_ch2 = 0;
  uint16_t raw_ch4 = 0;
  uint8_t link_quality = remote::kRemoteMetricUnknown;
  uint8_t rssi_magnitude = remote::kRemoteMetricUnknown;
  uint32_t control_cycles = 0;
  uint32_t neutral_cycles = 0;
  uint32_t drive_release_misses = 0;
  uint32_t steering_release_misses = 0;
  uint32_t cycle_deadline_misses = 0;
  uint32_t can_tx_success = 0;
  uint32_t can_tx_failed = 0;
  bool can_tx_inhibit_latched = false;
  uint32_t ipc_rejects = 0;
  uint8_t last_ipc_reject_detail = 0;
  remote::RemoteFrontendDiagnostics frontend_diagnostics = {};
};

struct RemoteControlRuntimeOutput {
  bool frame_ready = false;
  CanFrameRequest frame = {};
};

class RemoteControlRuntime {
 public:
  bool begin(uint32_t now_ms, uint32_t m7_boot_id,
             const RemoteControlRuntimeConfig& config);
  RemoteControlRuntimeOutput service(uint32_t now_ms,
                                     const RemoteControlRuntimeInputs& inputs);
  // FIFO enqueue acceptance advances the bounded frame batch, but it is not
  // physical CAN transmission evidence.
  void noteCanTxEnqueueResult(uint32_t now_ms, bool accepted);
  // Only the built-in CAN owner's hardware completion journal calls this.
  void noteCanTxCompletion(uint32_t now_ms, bool transmitted);

  bool hostControlAllowed() const { return status_.host_control_allowed; }
  const RemoteControlRuntimeConfig& config() const { return config_; }
  const RemoteControlRuntimeStatus& status() const { return status_; }
  const remote::M4RemoteMailboxSnapshot& mailboxSnapshot() const {
    return mailbox_reader_.snapshot();
  }

 private:
  void updateRemoteState(uint32_t now_ms);
  void requestImmediateSilence(uint32_t now_ms);
  void beginCycle(uint32_t now_ms, const RemoteControlRuntimeInputs& inputs,
                  uint32_t drive_release_sequence,
                  bool steering_release_due);
  bool scheduleMappedFrames(uint32_t ready_ms,
                            const VehicleCommandMapResult& mapped,
                            const CanTxGatewayInputs& gateway_inputs);
  bool scheduleSafetyStop(uint32_t now_ms,
                          const RemoteControlRuntimeInputs& inputs);
  void latchCanTxInhibit(uint32_t now_ms);
  void publishTelemetry(uint32_t now_ms);
  bool isNeutralSample(const remote::M4RemoteMailboxSnapshot& snapshot) const;

  RemoteControlRuntimeConfig config_ = {};
  RemoteControlRuntimeStatus status_ = {};
  remote::M4RemoteMailboxReader mailbox_reader_ = {};
  authority::AuthorityManager authority_manager_ = {};
  CommandLimiter command_limiter_ = {};
  VehicleCommandMapper vehicle_mapper_ = {};
  CanTxGateway can_tx_gateway_ = {};
  RemoteControlOrchestrator orchestrator_ = {};
  ControlReleaseSchedule release_schedule_ = {};

  uint32_t last_shared_sequence_ = 0;
  uint32_t last_frontend_seen_ms_ = 0;
  uint32_t neutral_since_ms_ = 0;
  uint32_t release_since_ms_ = 0;
  uint32_t next_frame_ms_ = 0;
  uint32_t last_telemetry_ms_ = 0;
  uint32_t cycle_sequence_ = 0;
  uint32_t observed_m4_boot_id_ = 0;
  bool frontend_seen_ = false;
  bool neutral_timer_active_ = false;
  bool release_timer_active_ = false;
  bool require_silent_cycle_ = true;
  bool immediate_stop_pending_ = true;
  bool prior_remote_valid_ = false;
  uint8_t pending_frame_index_ = 0;
  uint8_t pending_frame_count_ = 0;
  CanFrameRequest pending_frames_[kVehicleCommandMapperMaxFrames] = {};
};

}  // namespace csm::board::control
