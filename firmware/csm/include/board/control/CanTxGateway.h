#pragma once

#include <stdint.h>

#include "board/authority/AuthorityTypes.h"
#include "board/can/CanTypes.h"

namespace csm::board::control {

static constexpr uint8_t kCanTxGatewayMaxAllowlistIds = 8;

struct CanFrameRequest {
  authority::ControlSourceId source = authority::ControlSourceId::None;
  uint32_t command_seq = 0;
  uint8_t bus = authority::kAuthorityNoBus;
  uint32_t can_id_flags = 0;
  uint8_t dlc = 0;
  uint8_t data[8] = {};
  uint16_t policy_id = 0;
  uint16_t rate_bucket = 0;
};

struct CanTxGatewayPolicy {
  bool configured = false;
  bool build_profile_allows_local_tx = false;
  uint8_t bus = authority::kAuthorityNoBus;
  uint16_t policy_id = 0;
  uint8_t allowlist_count = 0;
  uint32_t allowlist_ids[kCanTxGatewayMaxAllowlistIds] = {};
};

struct CanTxGatewayInputs {
  authority::AuthorityDecision authority_decision = {};
  bool local_tx_inhibit_latched = true;
  bool safety_supervisor_allows = false;
  bool hardware_gate_allows = false;
  can::CanBackendState backend_state = {};
};

struct CanTxGatewayResult {
  bool accepted = false;
  authority::ControlDecisionCode decision = authority::ControlDecisionCode::RejectedFramePolicy;
  uint16_t detail = 0;
};

class CanTxGateway {
 public:
  void begin(uint32_t now_ms);

  bool configure(const CanTxGatewayPolicy& policy);
  void clearPolicy();

  CanTxGatewayResult evaluate(const CanFrameRequest& request,
                              const CanTxGatewayInputs& inputs) const;

  bool configured() const { return policy_.configured; }

 private:
  static bool isValidPolicy(const CanTxGatewayPolicy& policy);
  bool isAllowedFrame(const CanFrameRequest& request) const;
  bool isAllowedId(uint32_t can_id_flags) const;

  CanTxGatewayPolicy policy_ = {};
};

}  // namespace csm::board::control
