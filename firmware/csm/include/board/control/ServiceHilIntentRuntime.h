#pragma once

#include <stdint.h>

#include "board/control/CommandLimiter.h"
#include "board/control/VehicleCommandMapper.h"

namespace csm::board::control {

struct ServiceHilIntentResult {
  bool accepted = false;
  authority::ControlDecisionCode decision =
      authority::ControlDecisionCode::RejectedFramePolicy;
  CanFrameRequest frame = {};
  uint16_t detail = 0;
};

// Compatibility ingress for the current Android Service/HIL record. The wire
// carries a legacy 0x005/0x007-shaped intent, but these bytes are never
// submitted directly: they are decoded, rate/step limited and rebuilt by the
// same vehicle mapper used by RC.
class ServiceHilIntentRuntime {
 public:
  bool begin(uint32_t now_ms, uint8_t bus, uint16_t policy_id);
  void reset(uint32_t now_ms);
  ServiceHilIntentResult accept(uint32_t now_ms, uint32_t command_id,
                                uint32_t can_id, uint8_t dlc,
                                const uint8_t data[8]);

 private:
  bool decode(uint32_t can_id, uint8_t dlc, const uint8_t data[8],
              OperatorCommand* command) const;

  uint8_t bus_ = authority::kAuthorityNoBus;
  uint16_t policy_id_ = 0;
  OperatorCommand requested_ = {};
  CommandLimiter limiter_ = {};
  VehicleCommandMapper mapper_ = {};
  bool configured_ = false;
};

}  // namespace csm::board::control
