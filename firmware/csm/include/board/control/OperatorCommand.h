#pragma once

#include <stdint.h>

#include "board/authority/AuthorityTypes.h"

namespace csm::board::control {

static constexpr int16_t kControlPermilleMin = -1000;
static constexpr int16_t kControlPermilleMax = 1000;
static constexpr int16_t kBrakePermilleMin = 0;
static constexpr int16_t kBrakePermilleMax = 1000;

struct OperatorCommand {
  authority::ControlSourceId source = authority::ControlSourceId::None;
  uint32_t command_seq = 0;
  uint32_t source_time_ms = 0;
  bool takeover_request = false;
  bool release_request = false;
  bool enable_request = false;
  int16_t throttle_permille = 0;
  int16_t steer_permille = 0;
  int16_t brake_permille = 0;
  uint8_t drive_mode = 0;
  uint16_t validity_flags = 0;
};

constexpr bool isWithinOperatorCommandRange(const OperatorCommand& command) {
  return command.throttle_permille >= kControlPermilleMin &&
         command.throttle_permille <= kControlPermilleMax &&
         command.steer_permille >= kControlPermilleMin &&
         command.steer_permille <= kControlPermilleMax &&
         command.brake_permille >= kBrakePermilleMin &&
         command.brake_permille <= kBrakePermilleMax;
}

constexpr bool hasLocalControlSource(const OperatorCommand& command) {
  return command.source != authority::ControlSourceId::None;
}

}  // namespace csm::board::control

