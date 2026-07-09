#include "board/control/CanTxGateway.h"

namespace csm::board::control {
namespace {

constexpr uint16_t kDetailPolicyNotConfigured = 1;
constexpr uint16_t kDetailBuildProfileBlocked = 2;
constexpr uint16_t kDetailAuthorityRejected = 3;
constexpr uint16_t kDetailLocalInhibit = 4;
constexpr uint16_t kDetailSafetyDenied = 5;
constexpr uint16_t kDetailHardwareGateDenied = 6;
constexpr uint16_t kDetailBackendNotReady = 7;
constexpr uint16_t kDetailFramePolicy = 8;

uint32_t rawCanId(uint32_t can_id_flags) {
  return can_id_flags & ((can_id_flags & (1u << 29)) ? 0x1FFFFFFFu : 0x7FFu);
}

CanTxGatewayResult reject(authority::ControlDecisionCode decision, uint16_t detail) {
  CanTxGatewayResult result;
  result.accepted = false;
  result.decision = decision;
  result.detail = detail;
  return result;
}

}  // namespace

void CanTxGateway::begin(uint32_t) {}

bool CanTxGateway::configure(const CanTxGatewayPolicy& policy) {
  if (!isValidPolicy(policy)) {
    policy_ = {};
    return false;
  }
  policy_ = policy;
  policy_.configured = true;
  return true;
}

void CanTxGateway::clearPolicy() {
  policy_ = {};
}

CanTxGatewayResult CanTxGateway::evaluate(const CanFrameRequest& request,
                                          const CanTxGatewayInputs& inputs) const {
  if (!policy_.configured) {
    return reject(authority::ControlDecisionCode::RejectedFramePolicy,
                  kDetailPolicyNotConfigured);
  }
  if (!policy_.build_profile_allows_local_tx) {
    return reject(authority::ControlDecisionCode::RejectedBuildProfile,
                  kDetailBuildProfileBlocked);
  }
  if (inputs.authority_decision.code != authority::ControlDecisionCode::Accepted) {
    return reject(inputs.authority_decision.code, kDetailAuthorityRejected);
  }
  if (inputs.local_tx_inhibit_latched) {
    return reject(authority::ControlDecisionCode::RejectedLocalTxInhibit,
                  kDetailLocalInhibit);
  }
  if (!inputs.safety_supervisor_allows) {
    return reject(authority::ControlDecisionCode::RejectedSafetySupervisor,
                  kDetailSafetyDenied);
  }
  if (!inputs.hardware_gate_allows) {
    return reject(authority::ControlDecisionCode::RejectedHardwareGate,
                  kDetailHardwareGateDenied);
  }
  if (!inputs.backend_state.ready || inputs.backend_state.bus_off ||
      inputs.backend_state.error_passive || inputs.backend_state.tx_busy) {
    return reject(authority::ControlDecisionCode::RejectedSafetySupervisor,
                  kDetailBackendNotReady);
  }
  if (!isAllowedFrame(request)) {
    return reject(authority::ControlDecisionCode::RejectedFramePolicy,
                  kDetailFramePolicy);
  }

  CanTxGatewayResult result;
  result.accepted = true;
  result.decision = authority::ControlDecisionCode::Accepted;
  return result;
}

bool CanTxGateway::isValidPolicy(const CanTxGatewayPolicy& policy) {
  return policy.configured &&
         policy.bus != authority::kAuthorityNoBus &&
         policy.allowlist_count <= kCanTxGatewayMaxAllowlistIds;
}

bool CanTxGateway::isAllowedFrame(const CanFrameRequest& request) const {
  return request.bus == policy_.bus &&
         request.dlc <= 8 &&
         request.source != authority::ControlSourceId::None &&
         request.policy_id == policy_.policy_id &&
         isAllowedId(request.can_id_flags);
}

bool CanTxGateway::isAllowedId(uint32_t can_id_flags) const {
  const uint32_t can_id = rawCanId(can_id_flags);
  for (uint8_t i = 0; i < policy_.allowlist_count; ++i) {
    if (policy_.allowlist_ids[i] == can_id) {
      return true;
    }
  }
  return false;
}

}  // namespace csm::board::control
