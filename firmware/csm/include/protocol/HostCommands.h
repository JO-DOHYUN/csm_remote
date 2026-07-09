#pragma once

#include <stdint.h>

namespace csm {

enum HostControlSessionAction : uint8_t {
  HostControlDisarm = 0,
  HostControlArm = 1,
  HostControlRenewLease = 2,
  HostControlInstallNeutralProfile = 3,
};

static constexpr uint16_t kHostHeartbeatPayloadLen = 12;
static constexpr uint16_t kHostControlSessionPayloadLen = 24;
static constexpr uint16_t kHostSetControlPolicyMinPayloadLen = 8;
static constexpr uint16_t kHostClearFaultLockoutPayloadLen = 4;
static constexpr uint16_t kHostStreamAckPayloadLen = 12;
static constexpr uint16_t kHostReplayRequestPayloadLen = 24;

}  // namespace csm
