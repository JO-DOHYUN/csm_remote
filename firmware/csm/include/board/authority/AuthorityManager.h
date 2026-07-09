#pragma once

#include <stdint.h>

#include "board/authority/AuthorityTypes.h"
#include "board/control/OperatorCommand.h"

namespace csm::board::authority {

struct AuthorityInputs {
  AutonomyAuthorityState autonomy_state = AutonomyAuthorityState::Unknown;
  bool local_tx_inhibit_latched = false;
  bool estop_asserted = false;
  bool fault_lockout = false;
  bool safety_supervisor_allows = false;
  bool remote_source_valid = false;
  bool remote_source_neutral = false;
  bool remote_takeover_request = false;
  bool remote_release_request = false;
  bool host_service_enabled = false;
  bool host_service_request = false;
};

class AuthorityManager {
 public:
  void begin(uint32_t now_ms);
  AuthorityDecision update(uint32_t now_ms, const AuthorityInputs& inputs);

  AuthorityDecision evaluateCommand(const control::OperatorCommand& command,
                                    const AuthorityInputs& inputs) const;

  AuthorityState state() const { return state_; }
  ControlSourceId activeSource() const { return active_source_; }
  uint32_t transitionCounter() const { return transition_counter_; }

 private:
  void setState(AuthorityState state);
  AuthorityDecision reject(ControlDecisionCode code,
                           ControlSourceId source,
                           AutonomyAuthorityState autonomy_state,
                           uint16_t detail = 0) const;

  AuthorityState state_ = AuthorityState::BootInhibit;
  ControlSourceId active_source_ = ControlSourceId::None;
  uint32_t transition_counter_ = 0;
};

}  // namespace csm::board::authority

