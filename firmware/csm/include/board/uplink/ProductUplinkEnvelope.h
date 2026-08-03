#pragma once

#include <stdint.h>

#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

// Enabled Service/HIL product profile. These rates are the steady-state
// contract used by both the compile-time queue guards and the host envelope
// calculation; encoded byte sizes come only from the canonical wire schema.
static constexpr uint32_t kProductCanBusCount = 2;
static constexpr uint32_t kProductCanRxFramesPerSecondPerBus = 2000;
static constexpr uint32_t kProductControlCommandsPerSecond = 250;
static constexpr uint32_t kProductRemoteStateRecordsPerSecond = 10;
static constexpr uint32_t kProductBoardHealthRecordsPerSecond = 1;
static constexpr uint32_t kProductTransportDiagnosticRecordsPerSecond = 1;

// Qualification and design limits deliberately bound the calculated enabled
// profile instead of replacing it with a hand-written observed rate.
static constexpr uint32_t kProductUplinkMinimumBytesPerSecond = 120000;
static constexpr uint32_t kProductUplinkDesignBytesPerSecond = 135000;

constexpr uint32_t productSegmentRecordRate(uint32_t can_frames_per_second) {
  return (can_frames_per_second + csm::kCanRxSegmentMaxFrames - 1u) /
      csm::kCanRxSegmentMaxFrames;
}

constexpr uint32_t productSegmentWireBytesPerSecond(
    uint32_t can_frames_per_second) {
  const uint32_t complete_segments =
      can_frames_per_second / csm::kCanRxSegmentMaxFrames;
  const uint32_t remaining_frames =
      can_frames_per_second % csm::kCanRxSegmentMaxFrames;
  const uint32_t complete_segment_bytes = static_cast<uint32_t>(
      csm::encoded_typed_frame_len(
          csm::kCanRxSegmentHeaderLen +
          csm::kCanRxSegmentEntryLen * csm::kCanRxSegmentMaxFrames));
  const uint32_t remaining_segment_bytes = remaining_frames == 0
      ? 0u
      : static_cast<uint32_t>(csm::encoded_typed_frame_len(
            static_cast<uint16_t>(csm::kCanRxSegmentHeaderLen +
                                  csm::kCanRxSegmentEntryLen *
                                      remaining_frames)));
  return complete_segments * complete_segment_bytes +
      remaining_segment_bytes;
}

constexpr uint32_t productTypedRecordWireBytes(uint16_t payload_bytes,
                                               uint32_t records_per_second) {
  return static_cast<uint32_t>(csm::encoded_typed_frame_len(payload_bytes)) *
      records_per_second;
}

static constexpr uint32_t kProductCanRxFramesPerSecond =
    kProductCanBusCount * kProductCanRxFramesPerSecondPerBus;
static constexpr uint32_t kProductCanRxSegmentRecordsPerSecond =
    productSegmentRecordRate(kProductCanRxFramesPerSecond);
static constexpr uint32_t kProductCanRxWireBytesPerSecond =
    productSegmentWireBytesPerSecond(kProductCanRxFramesPerSecond);
static constexpr uint32_t kProductEnabledRecordsPerSecond =
    kProductCanRxSegmentRecordsPerSecond +
    kProductControlCommandsPerSecond +  // CAN_TX_RAW
    kProductControlCommandsPerSecond +  // CONTROL_ACK
    kProductRemoteStateRecordsPerSecond +
    kProductBoardHealthRecordsPerSecond +
    kProductTransportDiagnosticRecordsPerSecond;
static constexpr uint32_t kProductEnabledWireBytesPerSecond =
    kProductCanRxWireBytesPerSecond +
    productTypedRecordWireBytes(csm::kCanRawPayloadLen,
                                kProductControlCommandsPerSecond) +
    productTypedRecordWireBytes(csm::kControlAckPayloadLen,
                                kProductControlCommandsPerSecond) +
    productTypedRecordWireBytes(csm::kRemoteControlStatePayloadLen,
                                kProductRemoteStateRecordsPerSecond) +
    productTypedRecordWireBytes(csm::kBoardHealthV13PayloadLen,
                                kProductBoardHealthRecordsPerSecond) +
    productTypedRecordWireBytes(csm::kTransportDiagnosticPayloadLen,
                                kProductTransportDiagnosticRecordsPerSecond);

static_assert(kProductCanRxSegmentRecordsPerSecond == 174,
              "enabled product CAN segment record-rate regression");
static_assert(kProductCanRxWireBytesPerSecond == 88874,
              "enabled product CAN wire-rate regression");
static_assert(kProductEnabledRecordsPerSecond == 686,
              "enabled product record-rate regression");
static_assert(kProductEnabledWireBytesPerSecond == 111922,
              "enabled product wire-rate regression");
static_assert(kProductEnabledWireBytesPerSecond <=
                  kProductUplinkMinimumBytesPerSecond,
              "enabled product exceeds the minimum qualification envelope");
static_assert(kProductUplinkMinimumBytesPerSecond <
                  kProductUplinkDesignBytesPerSecond,
              "product uplink design envelope must retain qualification headroom");

}  // namespace csm::board::uplink
