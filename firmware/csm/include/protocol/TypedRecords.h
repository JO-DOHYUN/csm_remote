#pragma once

#include "protocol/TypedFrame.h"

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
};

static constexpr uint8_t kNoFixedControlBus = 0xFF;
static constexpr uint32_t kHostCanTxAllowedPrimaryId = 0x503;
static constexpr uint32_t kHostCanTxAllowedRangeStart = 0x510;
static constexpr uint32_t kHostCanTxAllowedRangeEnd = 0x513;
static constexpr uint16_t kHostCanTxRequestPayloadLen = 19;
static constexpr uint16_t kCanRawPayloadLen = 30;
static constexpr uint16_t kCanRxSegmentHeaderLen = 32;
static constexpr uint16_t kCanRxSegmentEntryLen = 30;
static constexpr uint8_t kCanRxSegmentMaxFrames = 15;
static constexpr uint16_t kControlAckPayloadLen = 28;
static constexpr uint16_t kBoardEventPayloadLen = 16;
static constexpr uint16_t kBoardHealthPayloadLen = 52;
static constexpr uint16_t kBoardHealthV1PayloadLen = 52;
static constexpr uint16_t kBoardHealthV2PayloadLen = 128;
static constexpr uint16_t kBoardHealthV4PayloadLen = 192;
static constexpr uint16_t kBoardHealthV5PayloadLen = 224;
static constexpr uint16_t kBoardHealthV6PayloadLen = 260;
static constexpr uint16_t kBoardHealthV7PayloadLen = 296;
static constexpr uint16_t kCapabilityPayloadLen = 36;
static constexpr uint16_t kCapabilityV1PayloadLen = 36;
static constexpr uint16_t kCapabilityV2PayloadLen = 80;
static constexpr uint16_t kCapabilityV3PayloadLen = 112;
static constexpr uint16_t kCapabilityV4PayloadLen = 192;
static constexpr uint16_t kCapabilityV5PayloadLen = 224;
static constexpr uint16_t kCapabilityV6PayloadLen = 272;
static constexpr uint8_t kCapabilityV2BusDescriptorLen = 20;
static constexpr uint16_t kAdcSamplePayloadLen = 44;
static constexpr uint8_t kFirmwareGitShaFieldLen = 12;
static constexpr uint8_t kFirmwareEnvNameFieldLen = 48;

}  // namespace csm
