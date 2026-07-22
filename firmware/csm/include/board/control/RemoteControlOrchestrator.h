#pragma once

#include <stdint.h>

#include "board/authority/AuthorityManager.h"
#include "board/can/CanTypes.h"
#include "board/control/CanTxGateway.h"
#include "board/control/CommandLimiter.h"
#include "board/control/VehicleCommandMapper.h"
#include "board/remote/RemoteControlSource.h"

namespace csm::board::control {

enum class RemoteControlOrchestratorStage : uint8_t {
  NotStarted = 0,
  Dependency = 1,
  AuthorityUpdate = 2,
  RemoteSource = 3,
  AuthorityCommand = 4,
  CommandLimiter = 5,
  VehicleMapper = 6,
  CanTxGateway = 7,
  Accepted = 8,
};

struct RemoteControlOrchestratorInputs {
  remote::M4RemoteMailboxSnapshot mailbox_snapshot = {};
  uint32_t output_sequence = 0;
  authority::AutonomyAuthorityState autonomy_state =
      authority::AutonomyAuthorityState::Unknown;
  bool local_tx_inhibit_latched = true;
  bool estop_asserted = false;
  bool fault_lockout = false;
  bool safety_supervisor_allows = false;
  bool remote_source_present = false;
  bool remote_handoff_qualified = false;
  bool remote_takeover_request = false;
  bool remote_release_request = false;
  bool hardware_gate_allows = false;
  can::CanBackendState backend_state = {};
};

struct RemoteControlOrchestratorDeps {
  authority::AuthorityManager* authority_manager = nullptr;
  CommandLimiter* command_limiter = nullptr;
  const VehicleCommandMapper* vehicle_mapper = nullptr;
  const CanTxGateway* can_tx_gateway = nullptr;
};

struct RemoteControlOrchestratorResult {
  bool accepted = false;
  RemoteControlOrchestratorStage stage = RemoteControlOrchestratorStage::NotStarted;
  authority::ControlDecisionCode decision =
      authority::ControlDecisionCode::RejectedBuildProfile;
  uint16_t detail = 0;
  uint8_t frame_count = 0;
  authority::AuthorityDecision authority_decision = {};
  OperatorCommand command = {};
  remote::RemoteLinkState remote_link_state = remote::RemoteLinkState::NotConfigured;
  CanFrameRequest frames[kVehicleCommandMapperMaxFrames] = {};
};

class RemoteControlOrchestrator {
 public:
  void begin(uint32_t now_ms);
  bool configureRemoteSource(const remote::RemoteControlSourceConfig& config) {
    return remote_source_.configure(config);
  }

  RemoteControlOrchestratorResult tick(uint32_t now_ms,
                                       const RemoteControlOrchestratorInputs& inputs,
                                       const RemoteControlOrchestratorDeps& deps);

  const remote::RemoteControlSource& remoteSource() const { return remote_source_; }

 private:
  static bool hasDeps(const RemoteControlOrchestratorDeps& deps);
  static bool isRemoteSnapshotUsable(const remote::M4RemoteMailboxSnapshot& snapshot);
  static authority::AuthorityInputs buildAuthorityInputs(
      const RemoteControlOrchestratorInputs& inputs);
  static CanTxGatewayInputs buildGatewayInputs(
      const RemoteControlOrchestratorInputs& inputs,
      const authority::AuthorityDecision& authority_decision);
  static RemoteControlOrchestratorResult reject(RemoteControlOrchestratorStage stage,
                                                authority::ControlDecisionCode decision,
                                                uint16_t detail);

  remote::RemoteControlSource remote_source_ = {};
};

}  // namespace csm::board::control
