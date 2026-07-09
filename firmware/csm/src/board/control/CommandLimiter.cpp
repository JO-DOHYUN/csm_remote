#include "board/control/CommandLimiter.h"

namespace csm::board::control {
namespace {

constexpr uint16_t kDetailNotConfigured = 1;
constexpr uint16_t kDetailOutOfRange = 2;
constexpr uint16_t kDetailRateLimited = 3;

}  // namespace

void CommandLimiter::begin(uint32_t now_ms) {
  last_accepted_ms_ = now_ms;
  has_last_accepted_command_ = false;
  last_accepted_command_ = {};
}

bool CommandLimiter::configure(const CommandLimiterConfig& config) {
  if (!isValidConfig(config)) {
    config_ = {};
    return false;
  }
  config_ = config;
  config_.configured = true;
  return true;
}

void CommandLimiter::clearConfig() {
  config_ = {};
  has_last_accepted_command_ = false;
  last_accepted_command_ = {};
}

CommandLimitResult CommandLimiter::evaluate(uint32_t now_ms,
                                            const OperatorCommand& command) const {
  CommandLimitResult result;
  result.command = command;

  if (!config_.configured) {
    result.decision = authority::ControlDecisionCode::RejectedBuildProfile;
    result.detail = kDetailNotConfigured;
    return result;
  }
  if (!isWithinOperatorCommandRange(command) || !isWithinConfiguredRange(command)) {
    result.decision = authority::ControlDecisionCode::RejectedRateLimit;
    result.detail = kDetailOutOfRange;
    return result;
  }
  if (has_last_accepted_command_ &&
      config_.min_command_interval_ms != 0 &&
      (now_ms - last_accepted_ms_) < config_.min_command_interval_ms) {
    result.decision = authority::ControlDecisionCode::RejectedRateLimit;
    result.detail = kDetailRateLimited;
    return result;
  }

  result.accepted = true;
  result.decision = authority::ControlDecisionCode::Accepted;
  result.detail = 0;
  return result;
}

void CommandLimiter::noteAccepted(uint32_t now_ms, const OperatorCommand& command) {
  last_accepted_ms_ = now_ms;
  last_accepted_command_ = command;
  has_last_accepted_command_ = true;
}

bool CommandLimiter::isValidConfig(const CommandLimiterConfig& config) {
  if (!config.configured) {
    return false;
  }
  return config.throttle_min_permille >= kControlPermilleMin &&
         config.throttle_max_permille <= kControlPermilleMax &&
         config.throttle_min_permille <= config.throttle_max_permille &&
         config.steer_min_permille >= kControlPermilleMin &&
         config.steer_max_permille <= kControlPermilleMax &&
         config.steer_min_permille <= config.steer_max_permille &&
         config.brake_min_permille >= kBrakePermilleMin &&
         config.brake_max_permille <= kBrakePermilleMax &&
         config.brake_min_permille <= config.brake_max_permille;
}

bool CommandLimiter::isWithinConfiguredRange(const OperatorCommand& command) const {
  return command.throttle_permille >= config_.throttle_min_permille &&
         command.throttle_permille <= config_.throttle_max_permille &&
         command.steer_permille >= config_.steer_min_permille &&
         command.steer_permille <= config_.steer_max_permille &&
         command.brake_permille >= config_.brake_min_permille &&
         command.brake_permille <= config_.brake_max_permille;
}

}  // namespace csm::board::control
