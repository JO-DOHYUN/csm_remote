#pragma once

#include <stdint.h>

namespace csm {

enum ControlAckStatus : uint8_t {
  ControlAckRejected = 0,
  ControlAckAccepted = 1,
};

enum ControlAckReason : uint8_t {
  ControlReasonOk = 0,
  ControlReasonBadLength = 1,
  ControlReasonBadBus = 2,
  ControlReasonUnsupportedFrame = 3,
  ControlReasonDlcOutOfRange = 4,
  ControlReasonIdNotAllowed = 5,
  ControlReasonCanNotReady = 6,
  ControlReasonCanWriteFailed = 7,
  ControlReasonBadProtocol = 8,
  ControlReasonNotArmed = 9,
  ControlReasonHostTimeout = 10,
  ControlReasonControlLeaseExpired = 11,
  ControlReasonSafetyLockout = 12,
  ControlReasonEstopAsserted = 13,
  ControlReasonFieldPowerLost = 14,
  ControlReasonEncoderFault = 15,
  ControlReasonQueueFull = 16,
  ControlReasonTxBusy = 17,
  ControlReasonBusOff = 18,
  ControlReasonErrorPassive = 19,
  ControlReasonRoleUnresolved = 20,
  ControlReasonPolicyHashMismatch = 21,
  ControlReasonNeutralProfileMissing = 22,
  ControlReasonRateLimited = 23,
  ControlReasonUnsupportedCommand = 24,
  ControlReasonAuthorityDenied = 25,
  ControlReasonStaleCommand = 26,
};

}  // namespace csm
