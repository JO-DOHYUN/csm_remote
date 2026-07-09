#pragma once

#include <stdint.h>

#include "board/authority/AuthorityTypes.h"
#include "board/control/CanTxGateway.h"
#include "board/control/OperatorCommand.h"

namespace csm::board::control {

static constexpr uint8_t kVehicleCommandMapperMaxFrames = 4;

struct VehicleCommandProfile {
  bool configured = false;
  bool output_enabled = false;
  uint8_t bus = authority::kAuthorityNoBus;
  uint16_t policy_id = 0;
  int16_t throttle_limit_permille = 0;
  int16_t steer_limit_permille = 0;
  int16_t brake_limit_permille = 0;
};

struct VehicleCommandMapResult {
  bool mapped = false;
  authority::ControlDecisionCode decision = authority::ControlDecisionCode::RejectedFramePolicy;
  uint8_t frame_count = 0;
  CanFrameRequest frames[kVehicleCommandMapperMaxFrames] = {};
  uint16_t detail = 0;
};

class VehicleCommandMapper {
 public:
  void begin(uint32_t now_ms);

  bool configure(const VehicleCommandProfile& profile);
  void clearProfile();

  VehicleCommandMapResult map(const OperatorCommand& command) const;

  bool configured() const { return profile_.configured; }

 private:
  static bool isValidProfile(const VehicleCommandProfile& profile);
  bool isWithinProfileLimits(const OperatorCommand& command) const;

  VehicleCommandProfile profile_ = {};
};

}  // namespace csm::board::control
