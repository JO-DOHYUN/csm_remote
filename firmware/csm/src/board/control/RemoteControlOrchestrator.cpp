#include "board/control/RemoteControlOrchestrator.h"

namespace csm::board::control {
namespace {

constexpr uint16_t kDetailMissingDependency = 1;
constexpr uint16_t kDetailNoRemoteCommand = 2;
constexpr uint16_t kDetailNoMappedFrames = 3;

}  // namespace

void RemoteControlOrchestrator::begin(uint32_t now_ms) {
  remote_source_.begin(now_ms);
}

RemoteControlOrchestratorResult RemoteControlOrchestrator::tick(
    uint32_t now_ms,
    const RemoteControlOrchestratorInputs& inputs,
    const RemoteControlOrchestratorDeps& deps) {
  if (!hasDeps(deps)) {
    return reject(RemoteControlOrchestratorStage::Dependency,
                  authority::ControlDecisionCode::RejectedBuildProfile,
                  kDetailMissingDependency);
  }

  remote_source_.update(now_ms,
                        inputs.mailbox_snapshot,
                        inputs.remote_handoff_qualified,
                        inputs.remote_takeover_request,
                        inputs.remote_release_request);

  authority::AuthorityInputs authority_inputs = buildAuthorityInputs(inputs);
  authority_inputs.remote_source_present = inputs.remote_source_present;
  authority_inputs.remote_source_valid = isRemoteSnapshotUsable(inputs.mailbox_snapshot);
  authority_inputs.remote_handoff_qualified = inputs.remote_handoff_qualified;
  authority_inputs.remote_takeover_request = inputs.remote_takeover_request;
  authority_inputs.remote_release_request = inputs.remote_release_request;

  const authority::AuthorityDecision authority_update =
      deps.authority_manager->update(now_ms, authority_inputs);
  if (authority_update.code != authority::ControlDecisionCode::Accepted) {
    RemoteControlOrchestratorResult result =
        reject(RemoteControlOrchestratorStage::AuthorityUpdate,
               authority_update.code,
               authority_update.detail);
    result.authority_decision = authority_update;
    result.remote_link_state = remote_source_.linkState();
    return result;
  }

  OperatorCommand command = remote_source_.command();
  command.command_seq = inputs.output_sequence;
  if (command.source == authority::ControlSourceId::None) {
    RemoteControlOrchestratorResult result =
        reject(RemoteControlOrchestratorStage::RemoteSource,
               authority::ControlDecisionCode::RejectedNoTakeover,
               kDetailNoRemoteCommand);
    result.authority_decision = authority_update;
    result.remote_link_state = remote_source_.linkState();
    return result;
  }

  const authority::AuthorityDecision command_authority =
      deps.authority_manager->evaluateCommand(command, authority_inputs);
  if (command_authority.code != authority::ControlDecisionCode::Accepted) {
    RemoteControlOrchestratorResult result =
        reject(RemoteControlOrchestratorStage::AuthorityCommand,
               command_authority.code,
               command_authority.detail);
    result.authority_decision = command_authority;
    result.command = command;
    result.remote_link_state = remote_source_.linkState();
    return result;
  }

  const CommandLimitResult limited =
      deps.command_limiter->evaluate(now_ms, command);
  if (!limited.accepted) {
    RemoteControlOrchestratorResult result =
        reject(RemoteControlOrchestratorStage::CommandLimiter,
               limited.decision,
               limited.detail);
    result.authority_decision = command_authority;
    result.command = limited.command;
    result.remote_link_state = remote_source_.linkState();
    return result;
  }

  const VehicleCommandMapResult mapped =
      deps.vehicle_mapper->map(limited.command);
  if (!mapped.mapped) {
    RemoteControlOrchestratorResult result =
        reject(RemoteControlOrchestratorStage::VehicleMapper,
               mapped.decision,
               mapped.detail);
    result.authority_decision = command_authority;
    result.command = limited.command;
    result.remote_link_state = remote_source_.linkState();
    return result;
  }
  if (mapped.frame_count == 0) {
    RemoteControlOrchestratorResult result =
        reject(RemoteControlOrchestratorStage::VehicleMapper,
               authority::ControlDecisionCode::RejectedFramePolicy,
               kDetailNoMappedFrames);
    result.authority_decision = command_authority;
    result.command = limited.command;
    result.remote_link_state = remote_source_.linkState();
    return result;
  }

  const CanTxGatewayInputs gateway_inputs =
      buildGatewayInputs(inputs, command_authority);
  for (uint8_t i = 0; i < mapped.frame_count; ++i) {
    const CanTxGatewayResult gateway_result =
        deps.can_tx_gateway->evaluate(mapped.frames[i], gateway_inputs);
    if (!gateway_result.accepted) {
      RemoteControlOrchestratorResult result =
          reject(RemoteControlOrchestratorStage::CanTxGateway,
                 gateway_result.decision,
                 gateway_result.detail);
      result.authority_decision = command_authority;
      result.command = limited.command;
      result.frame_count = i;
      result.remote_link_state = remote_source_.linkState();
      return result;
    }
  }

  deps.command_limiter->noteAccepted(now_ms, limited.command);

  RemoteControlOrchestratorResult result;
  result.accepted = true;
  result.stage = RemoteControlOrchestratorStage::Accepted;
  result.decision = authority::ControlDecisionCode::Accepted;
  result.frame_count = mapped.frame_count;
  for (uint8_t i = 0; i < mapped.frame_count; ++i) {
    result.frames[i] = mapped.frames[i];
  }
  result.authority_decision = command_authority;
  result.command = limited.command;
  result.remote_link_state = remote_source_.linkState();
  return result;
}

bool RemoteControlOrchestrator::hasDeps(const RemoteControlOrchestratorDeps& deps) {
  return deps.authority_manager != nullptr &&
         deps.command_limiter != nullptr &&
         deps.vehicle_mapper != nullptr &&
         deps.can_tx_gateway != nullptr;
}

bool RemoteControlOrchestrator::isRemoteSnapshotUsable(
    const remote::M4RemoteMailboxSnapshot& snapshot) {
  return snapshot.sample_present &&
         snapshot.integrity_ok &&
         remote::isUsableRemoteLink(snapshot.link_state) &&
         remote::isUsableRcSampleState(snapshot.sample.sample_state);
}

authority::AuthorityInputs RemoteControlOrchestrator::buildAuthorityInputs(
    const RemoteControlOrchestratorInputs& inputs) {
  authority::AuthorityInputs authority_inputs;
  authority_inputs.autonomy_state = inputs.autonomy_state;
  authority_inputs.local_tx_inhibit_latched = inputs.local_tx_inhibit_latched;
  authority_inputs.estop_asserted = inputs.estop_asserted;
  authority_inputs.fault_lockout = inputs.fault_lockout;
  authority_inputs.safety_supervisor_allows = inputs.safety_supervisor_allows;
  return authority_inputs;
}

CanTxGatewayInputs RemoteControlOrchestrator::buildGatewayInputs(
    const RemoteControlOrchestratorInputs& inputs,
    const authority::AuthorityDecision& authority_decision) {
  CanTxGatewayInputs gateway_inputs;
  gateway_inputs.authority_decision = authority_decision;
  gateway_inputs.local_tx_inhibit_latched = inputs.local_tx_inhibit_latched;
  gateway_inputs.safety_supervisor_allows = inputs.safety_supervisor_allows;
  gateway_inputs.backend_state = inputs.backend_state;
  return gateway_inputs;
}

RemoteControlOrchestratorResult RemoteControlOrchestrator::reject(
    RemoteControlOrchestratorStage stage,
    authority::ControlDecisionCode decision,
    uint16_t detail) {
  RemoteControlOrchestratorResult result;
  result.stage = stage;
  result.decision = decision;
  result.detail = detail;
  return result;
}

}  // namespace csm::board::control
