#include "board/authority/AuthorityManager.h"

namespace csm::board::authority {

void AuthorityManager::begin(uint32_t) {
  state_ = AuthorityState::BootInhibit;
  active_source_ = ControlSourceId::None;
  transition_counter_ = 0;
}

AuthorityDecision AuthorityManager::update(uint32_t, const AuthorityInputs& inputs) {
  if (inputs.local_tx_inhibit_latched) {
    active_source_ = ControlSourceId::None;
    setState(AuthorityState::AutonomyActiveLock);
    return reject(ControlDecisionCode::RejectedLocalTxInhibit,
                  ControlSourceId::None,
                  inputs.autonomy_state);
  }

  switch (inputs.autonomy_state) {
    case AutonomyAuthorityState::InactiveConfirmed:
      break;
    case AutonomyAuthorityState::ActiveConfirmed:
      active_source_ = ControlSourceId::None;
      setState(AuthorityState::AutonomyActiveLock);
      return reject(ControlDecisionCode::RejectedAutonomyActive,
                    ControlSourceId::None,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::RecentlyActive:
      active_source_ = ControlSourceId::None;
      setState(AuthorityState::AutonomyRecentlyActive);
      return reject(ControlDecisionCode::RejectedAutonomyRecent,
                    ControlSourceId::None,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::Ambiguous:
      active_source_ = ControlSourceId::None;
      setState(AuthorityState::AutonomyAmbiguous);
      return reject(ControlDecisionCode::RejectedAutonomyAmbiguous,
                    ControlSourceId::None,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::ProtocolFault:
      active_source_ = ControlSourceId::None;
      setState(AuthorityState::AutonomyProtocolFault);
      return reject(ControlDecisionCode::RejectedAutonomyProtocolFault,
                    ControlSourceId::None,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::Unknown:
    default:
      active_source_ = ControlSourceId::None;
      setState(AuthorityState::ObserveOnly);
      return reject(ControlDecisionCode::RejectedAutonomyUnknown,
                    ControlSourceId::None,
                    inputs.autonomy_state);
  }

  // RC may reserve the local boundary ahead of a service host only after the
  // upstream-autonomy monitor has positively released it. Unknown or active
  // autonomy always fails closed before this branch.
  if (inputs.remote_source_present || inputs.remote_takeover_request) {
    if (!inputs.remote_source_valid) {
      active_source_ = ControlSourceId::None;
      setState(AuthorityState::LocalHandoffPending);
      return reject(ControlDecisionCode::RejectedSourceStale,
                    ControlSourceId::Remote, inputs.autonomy_state);
    }
    active_source_ = ControlSourceId::Remote;
    setState(AuthorityState::RemoteActive);
    AuthorityDecision decision;
    decision.code = ControlDecisionCode::Accepted;
    decision.source = ControlSourceId::Remote;
    decision.autonomy_state = inputs.autonomy_state;
    decision.authority_state = state_;
    return decision;
  }

  if (inputs.host_service_enabled && inputs.host_service_request) {
    active_source_ = ControlSourceId::HostService;
    setState(AuthorityState::HostServiceActive);
    AuthorityDecision decision;
    decision.code = ControlDecisionCode::Accepted;
    decision.source = ControlSourceId::HostService;
    decision.autonomy_state = inputs.autonomy_state;
    decision.authority_state = state_;
    return decision;
  }

  active_source_ = ControlSourceId::None;
  setState(AuthorityState::LocalReady);
  return reject(ControlDecisionCode::RejectedNoTakeover,
                ControlSourceId::None,
                inputs.autonomy_state);
}

AuthorityDecision AuthorityManager::evaluateCommand(const control::OperatorCommand& command,
                                                     const AuthorityInputs& inputs) const {
  if (inputs.local_tx_inhibit_latched) {
    return reject(ControlDecisionCode::RejectedLocalTxInhibit,
                  command.source,
                  inputs.autonomy_state);
  }

  switch (inputs.autonomy_state) {
    case AutonomyAuthorityState::InactiveConfirmed:
      break;
    case AutonomyAuthorityState::ActiveConfirmed:
      return reject(ControlDecisionCode::RejectedAutonomyActive,
                    command.source,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::RecentlyActive:
      return reject(ControlDecisionCode::RejectedAutonomyRecent,
                    command.source,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::Ambiguous:
      return reject(ControlDecisionCode::RejectedAutonomyAmbiguous,
                    command.source,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::ProtocolFault:
      return reject(ControlDecisionCode::RejectedAutonomyProtocolFault,
                    command.source,
                    inputs.autonomy_state);
    case AutonomyAuthorityState::Unknown:
    default:
      return reject(ControlDecisionCode::RejectedAutonomyUnknown,
                    command.source,
                    inputs.autonomy_state);
  }
  if (command.source == ControlSourceId::None) {
    return reject(ControlDecisionCode::RejectedNoTakeover,
                  command.source,
                  inputs.autonomy_state);
  }
  if (command.source != active_source_) {
    return reject(ControlDecisionCode::RejectedBuildProfile,
                  command.source,
                  inputs.autonomy_state);
  }
  if (command.source == ControlSourceId::Remote) {
    if (!inputs.remote_source_valid) {
      return reject(ControlDecisionCode::RejectedSourceStale,
                    command.source, inputs.autonomy_state);
    }
  }

  AuthorityDecision decision;
  decision.code = ControlDecisionCode::Accepted;
  decision.source = command.source;
  decision.autonomy_state = inputs.autonomy_state;
  decision.authority_state = state_;
  return decision;
}

void AuthorityManager::setState(AuthorityState state) {
  if (state_ != state) {
    state_ = state;
    transition_counter_++;
  }
}

AuthorityDecision AuthorityManager::reject(ControlDecisionCode code,
                                           ControlSourceId source,
                                           AutonomyAuthorityState autonomy_state,
                                           uint16_t detail) const {
  AuthorityDecision decision;
  decision.code = code;
  decision.source = source;
  decision.autonomy_state = autonomy_state;
  decision.authority_state = state_;
  decision.detail = detail;
  return decision;
}

}  // namespace csm::board::authority
