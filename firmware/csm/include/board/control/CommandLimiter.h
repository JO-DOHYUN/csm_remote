#pragma once

#include <stdint.h>

#include "board/authority/AuthorityTypes.h"
#include "board/control/OperatorCommand.h"

namespace csm::board::control {

struct CommandLimiterConfig {
  bool configured = false;
  int16_t throttle_min_permille = 0;
  int16_t throttle_max_permille = 0;
  int16_t steer_min_permille = 0;
  int16_t steer_max_permille = 0;
  int16_t brake_min_permille = 0;
  int16_t brake_max_permille = 0;
  uint16_t min_command_interval_ms = 0;
  uint16_t throttle_rise_step_permille = 0;
  uint16_t throttle_fall_step_permille = 0;
  uint16_t steer_step_permille = 0;
  uint16_t steer_return_step_permille = 0;
};

struct CommandLimitResult {
  bool accepted = false;
  authority::ControlDecisionCode decision = authority::ControlDecisionCode::RejectedBuildProfile;
  OperatorCommand command = {};
  uint16_t detail = 0;
};

class CommandLimiter {
 public:
  void begin(uint32_t now_ms);

  bool configure(const CommandLimiterConfig& config);
  void clearConfig();

  CommandLimitResult evaluate(uint32_t now_ms, const OperatorCommand& command) const;
  void noteAccepted(uint32_t now_ms, const OperatorCommand& command);

  bool configured() const { return config_.configured; }
  uint32_t lastAcceptedMs() const { return last_accepted_ms_; }
  bool hasLastAcceptedCommand() const { return has_last_accepted_command_; }

 private:
  static bool isValidConfig(const CommandLimiterConfig& config);
  bool isWithinConfiguredRange(const OperatorCommand& command) const;

  CommandLimiterConfig config_ = {};
  OperatorCommand last_accepted_command_ = {};
  bool has_last_accepted_command_ = false;
  uint32_t last_accepted_ms_ = 0;
};

}  // namespace csm::board::control
