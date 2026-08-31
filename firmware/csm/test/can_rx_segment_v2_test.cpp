#include <cstdio>
#include <cstring>

#include "board/CapabilityPublisher.h"
#include "board/uplink/CanRxQueueOrder.h"
#include "board/uplink/CanRxSegmentBuilder.h"
#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                         \
  do {                                                                      \
    if (!(expr)) {                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);          \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

uint64_t read_u64_le(const uint8_t* bytes) {
  uint64_t value = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    value |= static_cast<uint64_t>(bytes[index]) << (8u * index);
  }
  return value;
}

struct SegmentCapture {
  uint8_t emits = 0;
  uint8_t counts[4] = {};
  uint64_t sequences[4] = {};
};

bool capture_segment(void* context,
                     const csm::board::uplink::CanRxSegmentItem*,
                     uint8_t count,
                     uint64_t sequence) {
  auto* capture = static_cast<SegmentCapture*>(context);
  if (capture->emits < 4) {
    capture->counts[capture->emits] = count;
    capture->sequences[capture->emits] = sequence;
  }
  ++capture->emits;
  return true;
}

void compact_payload_is_exact_and_bounded() {
  using csm::board::uplink::CanRxSegmentItem;
  using csm::board::uplink::encode_can_rx_segment_payload;

  static_assert(csm::kCanRxSegmentSchema == 2);
  static_assert(csm::kCanRxSegmentHeaderLen == 40);
  static_assert(csm::kCanRxSegmentEntryLen == 20);
  static_assert(csm::kCanRxSegmentMaxFrames == 23);
  static_assert(csm::kCanRxSegmentLegacyHeaderLen == 32);
  static_assert(csm::kCanRxSegmentLegacyEntryLen == 30);
  static_assert(csm::kCanRxSegmentLegacyEntryCaptureOffset == 0);
  static_assert(csm::kCanRxSegmentLegacyEntryMonoUsOffset == 8);
  static_assert(csm::kCanRxSegmentLegacyEntryIdFlagsOffset == 16);
  static_assert(csm::kCanRxSegmentLegacyEntryDlcFlagsOffset == 20);
  static_assert(csm::kCanRxSegmentLegacyEntryBusOffset == 21);
  static_assert(csm::kCanRxSegmentLegacyEntryDataOffset == 22);
  static_assert(csm::encoded_typed_frame_len(
                    csm::kCanRxSegmentHeaderLen +
                    csm::kCanRxSegmentEntryLen *
                        csm::kCanRxSegmentMaxFrames) == 511);

  CanRxSegmentItem items[2] = {};
  items[0].capture_seq = 1000;
  items[0].mono_us = 5000000;
  items[0].can_id_flags = 0x123;
  items[0].dlc_flags = 8;
  items[0].bus = 0;
  for (uint8_t index = 0; index < 8; ++index) {
    items[0].data[index] = index;
  }
  items[1].capture_seq = 1002;
  items[1].mono_us = 5000250;
  items[1].can_id_flags = 0x20000123;
  items[1].dlc_flags = 4;
  items[1].bus = 1;
  for (uint8_t index = 0; index < 8; ++index) {
    items[1].data[index] = static_cast<uint8_t>(0xA0 + index);
  }

  uint8_t payload[csm::kMaxPayloadLen] = {};
  const uint16_t length = encode_can_rx_segment_payload(
      items, 2, 77, 3, 4, payload, sizeof(payload));
  CHECK(length == 80);
  CHECK(read_u64_le(&payload[csm::kCanRxSegmentSequenceOffset]) == 77);
  CHECK(read_u64_le(&payload[csm::kCanRxSegmentFirstCaptureOffset]) == 1000);
  CHECK(csm::rd_u16_le(&payload[csm::kCanRxSegmentFrameCountOffset]) == 2);
  CHECK(payload[csm::kCanRxSegmentEntrySizeOffset] ==
        csm::kCanRxSegmentEntryLen);
  CHECK(payload[csm::kCanRxSegmentFlagsOffset] ==
        (csm::kCanRxSegmentFlagCaptureSequenceValid |
         csm::kCanRxSegmentFlagCompactEntries));
  CHECK(csm::rd_u32_le(
            &payload[csm::kCanRxSegmentDroppedTotalOffset]) == 3);
  CHECK(csm::rd_u32_le(
            &payload[csm::kCanRxSegmentFifoOverflowTotalOffset]) == 4);
  CHECK(payload[csm::kCanRxSegmentSchemaOffset] ==
        csm::kCanRxSegmentSchema);
  CHECK(payload[csm::kCanRxSegmentHeaderSizeOffset] ==
        csm::kCanRxSegmentHeaderLen);
  CHECK(read_u64_le(&payload[csm::kCanRxSegmentBaseMonoUsOffset]) ==
        5000000);

  const uint8_t* first = &payload[csm::kCanRxSegmentEntriesOffset];
  const uint8_t* second =
      first + csm::kCanRxSegmentEntryLen;
  CHECK(csm::rd_u16_le(
            &first[csm::kCanRxSegmentCompactEntryCaptureDeltaOffset]) == 0);
  CHECK(csm::rd_u32_le(
            &first[csm::kCanRxSegmentCompactEntryMonoDeltaUsOffset]) == 0);
  CHECK(csm::rd_u16_le(
            &second[csm::kCanRxSegmentCompactEntryCaptureDeltaOffset]) == 2);
  CHECK(csm::rd_u32_le(
            &second[csm::kCanRxSegmentCompactEntryMonoDeltaUsOffset]) == 250);
  CHECK(csm::rd_u32_le(
            &second[csm::kCanRxSegmentCompactEntryIdFlagsOffset]) ==
        items[1].can_id_flags);
  CHECK(second[csm::kCanRxSegmentCompactEntryDlcFlagsOffset] == 4);
  CHECK(second[csm::kCanRxSegmentCompactEntryBusOffset] == 1);
  CHECK(std::memcmp(
            &second[csm::kCanRxSegmentCompactEntryDataOffset],
            items[1].data, sizeof(items[1].data)) == 0);

  CHECK(encode_can_rx_segment_payload(
            items, 2, 77, 3, 4, payload,
            static_cast<uint16_t>(length - 1)) == 0);
  items[1].capture_seq = items[0].capture_seq + 0x10000ULL;
  CHECK(encode_can_rx_segment_payload(
            items, 2, 77, 3, 4, payload, sizeof(payload)) == 0);
}

void builder_splits_before_compact_delta_overflow() {
  using namespace csm::board::uplink;
  SegmentCapture capture;
  CanRxSegmentBuilder builder;
  builder.begin(capture_segment, &capture, 20000);

  CanRxSegmentItem first = {};
  first.capture_seq = 10;
  first.mono_us = 100;
  CHECK(builder.push(first, 1000));

  CanRxSegmentItem outside_delta = first;
  outside_delta.capture_seq += 0x10000ULL;
  outside_delta.mono_us += 10;
  CHECK(builder.push(outside_delta, 1010));
  CHECK(capture.emits == 1);
  CHECK(capture.counts[0] == 1);
  CHECK(capture.sequences[0] == 0);
  CHECK(builder.pendingCount() == 1);
  CHECK(builder.flush());
  CHECK(capture.emits == 2);
  CHECK(capture.counts[1] == 1);
  CHECK(capture.sequences[1] == 1);

  capture = {};
  builder.begin(capture_segment, &capture, 20000);
  for (uint8_t index = 0; index < csm::kCanRxSegmentMaxFrames; ++index) {
    CanRxSegmentItem item = {};
    item.capture_seq = index;
    item.mono_us = 1000 - index;
    CHECK(builder.push(item, 1000 + index));
  }
  CHECK(capture.emits == 1);
  CHECK(capture.counts[0] == csm::kCanRxSegmentMaxFrames);
  CHECK(builder.pendingCount() == 0);
}

void regressing_timestamps_use_the_segment_minimum_base() {
  using csm::board::uplink::CanRxSegmentItem;
  using csm::board::uplink::encode_can_rx_segment_payload;
  CanRxSegmentItem items[2] = {};
  items[0].capture_seq = 20;
  items[0].mono_us = 2000;
  items[0].dlc_flags = 8;
  items[1].capture_seq = 21;
  items[1].mono_us = 1750;
  items[1].dlc_flags = 8;

  uint8_t payload[csm::kMaxPayloadLen] = {};
  CHECK(encode_can_rx_segment_payload(
            items, 2, 3, 0, 0, payload, sizeof(payload)) == 80);
  CHECK(read_u64_le(&payload[csm::kCanRxSegmentBaseMonoUsOffset]) == 1750);
  const uint8_t* first = &payload[csm::kCanRxSegmentEntriesOffset];
  const uint8_t* second = first + csm::kCanRxSegmentEntryLen;
  CHECK(csm::rd_u32_le(
            &first[csm::kCanRxSegmentCompactEntryMonoDeltaUsOffset]) == 250);
  CHECK(csm::rd_u32_le(
            &second[csm::kCanRxSegmentCompactEntryMonoDeltaUsOffset]) == 0);
}

void capability_advertises_the_emitted_segment_schema() {
  csm::board::CapabilityPayloadConfig config;
  config.profile_major = 4;
  config.profile_minor = 1;
  config.include_v2 = true;
  config.include_v3 = true;
  config.can_rx_segment_schema = csm::kCanRxSegmentSchema;
  config.can_rx_segment_header_len =
      static_cast<uint8_t>(csm::kCanRxSegmentHeaderLen);
  config.can_rx_segment_entry_len =
      static_cast<uint8_t>(csm::kCanRxSegmentEntryLen);
  config.can_rx_segment_max_frames = csm::kCanRxSegmentMaxFrames;

  uint8_t payload[csm::kCapabilityV3PayloadLen] = {};
  CHECK(csm::board::build_capability_payload(
            config, payload, sizeof(payload)) ==
        csm::kCapabilityV3PayloadLen);
  CHECK(payload[csm::kCapabilityProfileMinorOffset] == 1);
  CHECK(payload[csm::kCapabilityCanRxSegmentSchemaOffset] ==
        csm::kCanRxSegmentSchema);
  CHECK(payload[csm::kCapabilityCanRxSegmentHeaderLenOffset] ==
        csm::kCanRxSegmentHeaderLen);
  CHECK(payload[csm::kCapabilityCanRxSegmentEntryLenOffset] ==
        csm::kCanRxSegmentEntryLen);
  CHECK(payload[csm::kCapabilityCanRxSegmentMaxFramesOffset] ==
        csm::kCanRxSegmentMaxFrames);
}

void capability_v7_advertises_control_and_qualification_truth() {
  csm::board::CapabilityPayloadConfig config;
  config.include_v2 = true;
  config.include_v3 = true;
  config.include_v4 = true;
  config.include_v5 = true;
  config.include_v6 = true;
  config.include_v7 = true;
  config.control_schema = csm::kHostControlSchema;
  config.terminal_evidence_schema = csm::kControlTxEvidenceSchema;
  config.threshold_qualification = 0;
  config.hardware_tx_slots = 3;
  config.host_software_retention = 0;
  config.host_proof_max_gap_ms = 17;
  config.intentional_cancel_total = 2;
  config.hardware_failure_total = 3;
  config.tracking_failure_total = 4;

  uint8_t payload[csm::kCapabilityV7PayloadLen] = {};
  CHECK(csm::board::build_capability_payload(
            config, payload, sizeof(payload)) ==
        csm::kCapabilityV7PayloadLen);
  CHECK(payload[csm::kCapabilityControlSchemaOffset] ==
        csm::kHostControlSchema);
  CHECK(payload[csm::kCapabilityTerminalEvidenceSchemaOffset] ==
        csm::kControlTxEvidenceSchema);
  CHECK(payload[csm::kCapabilityThresholdQualificationOffset] == 0);
  CHECK(payload[csm::kCapabilityHardwareTxSlotsOffset] == 3);
  CHECK(csm::rd_u16_le(&payload[csm::kCapabilityHostSoftwareRetentionOffset]) == 0);
  CHECK(csm::rd_u32_le(&payload[csm::kCapabilityHostProofMaxGapMsOffset]) == 17);
  CHECK(csm::rd_u32_le(&payload[csm::kCapabilityIntentionalCancelTotalOffset]) == 2);
  CHECK(csm::rd_u32_le(&payload[csm::kCapabilityHardwareFailureTotalOffset]) == 3);
  CHECK(csm::rd_u32_le(&payload[csm::kCapabilityTrackingFailureTotalOffset]) == 4);
}

void two_bus_queue_selection_preserves_global_capture_order() {
  using namespace csm::board::uplink;
  CHECK(select_can_rx_queue_index(false, 0, false, 0) ==
        kNoReadyCanRxQueue);
  CHECK(select_can_rx_queue_index(true, 20, false, 0) == 0);
  CHECK(select_can_rx_queue_index(false, 0, true, 19) == 1);
  CHECK(select_can_rx_queue_index(true, 20, true, 19) == 1);
  CHECK(select_can_rx_queue_index(true, 20, true, 21) == 0);
  CHECK(select_can_rx_queue_index(true, 20, true, 20) == 0);
}

}  // namespace

int main() {
  compact_payload_is_exact_and_bounded();
  builder_splits_before_compact_delta_overflow();
  regressing_timestamps_use_the_segment_minimum_base();
  capability_advertises_the_emitted_segment_schema();
  capability_v7_advertises_control_and_qualification_truth();
  two_bus_queue_selection_preserves_global_capture_order();
  if (failures != 0) {
    return 1;
  }
  std::puts("PASS: CAN_RX_SEGMENT compact schema v2 contract");
  return 0;
}
