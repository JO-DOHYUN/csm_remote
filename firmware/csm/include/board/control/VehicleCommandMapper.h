#pragma once

#include <stdint.h>

#include "board/authority/AuthorityTypes.h"
#include "board/control/CanTxGateway.h"
#include "board/control/OperatorCommand.h"

namespace csm::board::control {

static constexpr uint8_t kVehicleCommandMapperMaxFrames = 2;
static constexpr uint32_t kRemoteDriveCanId = 0x005;
static constexpr uint32_t kRemoteSteeringCanId = 0x007;
static constexpr uint8_t kRemoteDriveHeader = 0xAA;
static constexpr uint8_t kRemoteDriveMode = 0x52;
static constexpr uint8_t kRemoteDriveStopMode = 0x02;
static constexpr uint8_t kRemoteDriveForward = 0x50;
static constexpr uint8_t kRemoteDriveReverse = 0x60;
static constexpr uint8_t kRemoteSteeringMinimum = 10;
static constexpr uint8_t kRemoteSteeringCenter = 130;
static constexpr uint8_t kRemoteSteeringMaximum = 250;
static constexpr uint8_t kRemoteAuxiliaryNegative = 0x01;
static constexpr uint8_t kRemoteAuxiliaryPositive = 0x80;

enum class VehicleCommandMapping : uint8_t {
  None = 0,
  VehicleBench0x005And0x007 = 1,
};

struct VehicleCommandProfile {
  bool configured = false;
  bool output_enabled = false;
  VehicleCommandMapping mapping = VehicleCommandMapping::None;
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
  VehicleCommandMapResult mapSafetyStop(uint32_t command_seq) const;

  bool configured() const { return profile_.configured; }

 private:
  static bool isValidProfile(const VehicleCommandProfile& profile);
  bool isWithinProfileLimits(const OperatorCommand& command) const;

  VehicleCommandProfile profile_ = {};
};

}  // namespace csm::board::control
