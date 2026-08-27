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
static constexpr uint8_t kHostHeartbeatCommandIdOffset = 0;
static constexpr uint8_t kHostHeartbeatMonoMsOffset = 4;
static constexpr uint8_t kHostControlSessionCommandIdOffset = 0;
static constexpr uint8_t kHostControlSessionActionOffset = 4;
static constexpr uint8_t kHostControlSessionBusOffset = 5;
static constexpr uint8_t kHostControlSessionLeaseMsOffset = 8;
static constexpr uint8_t kHostControlSessionMonoMsOffset = 12;
static constexpr uint8_t kHostControlSessionSchemaOffset = 16;
static constexpr uint16_t kHostSetControlPolicyMinPayloadLen = 8;
static constexpr uint16_t kAppRxCommitAckPayloadLen = 16;
static constexpr uint8_t kAppRxCommitAckBootSessionOffset = 0;
static constexpr uint8_t kAppRxCommitAckPublishSeqOffset = 8;

}  // namespace csm
