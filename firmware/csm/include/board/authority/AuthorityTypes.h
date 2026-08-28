#pragma once

#include <stdint.h>

namespace csm::board::authority {

static constexpr uint8_t kAuthorityNoBus = 0xFF;

enum class AuthorityState : uint8_t {
  LocalReady = 7,
  RemoteActive = 8,
  HostServiceActive = 9,
};

enum class ControlSourceId : uint8_t {
  None = 0,
  Remote = 1,
  HostService = 2,
  TestOnly = 3,
  SafetyNeutral = 4,
};

enum class ControlDecisionCode : uint8_t {
  Accepted = 0,
  RejectedBuildProfile = 1,
  RejectedSourceStale = 9,
  RejectedSourceFailsafe = 10,
  RejectedNotNeutral = 11,
  RejectedNoTakeover = 12,
  RejectedRateLimit = 13,
  RejectedFramePolicy = 14,
};

}  // namespace csm::board::authority
