#include "board/uplink/CanRxSegmentBuilder.h"

#include <cstring>

namespace csm::board::uplink {

uint16_t encode_can_rx_segment_payload(const CanRxSegmentItem* items,
                                       uint8_t count,
                                       uint64_t segment_seq,
                                       uint32_t dropped_total,
                                       uint32_t fifo_overflow_total,
                                       uint8_t* payload,
                                       uint16_t capacity) {
  if (items == nullptr || payload == nullptr || count == 0 ||
      count > csm::kCanRxSegmentMaxFrames) {
    return 0;
  }

  const uint16_t payload_len = static_cast<uint16_t>(
      csm::kCanRxSegmentHeaderLen +
      static_cast<uint16_t>(count) * csm::kCanRxSegmentEntryLen);
  if (capacity < payload_len) {
    return 0;
  }

  const uint64_t first_capture_seq = items[0].capture_seq;
  const uint64_t base_mono_us = items[0].mono_us;
  for (uint8_t index = 0; index < count; ++index) {
    if (items[index].capture_seq < first_capture_seq ||
        items[index].capture_seq - first_capture_seq > 0xFFFFULL ||
        items[index].mono_us < base_mono_us ||
        items[index].mono_us - base_mono_us > 0xFFFFFFFFULL) {
      return 0;
    }
  }

  memset(payload, 0, payload_len);
  csm::wr_u64_le(&payload[csm::kCanRxSegmentSequenceOffset], segment_seq);
  csm::wr_u64_le(&payload[csm::kCanRxSegmentFirstCaptureOffset],
                 first_capture_seq);
  csm::wr_u16_le(&payload[csm::kCanRxSegmentFrameCountOffset], count);
  payload[csm::kCanRxSegmentEntrySizeOffset] =
      static_cast<uint8_t>(csm::kCanRxSegmentEntryLen);
  payload[csm::kCanRxSegmentFlagsOffset] =
      csm::kCanRxSegmentFlagCaptureSequenceValid |
      csm::kCanRxSegmentFlagCompactEntries;
  csm::wr_u32_le(&payload[csm::kCanRxSegmentDroppedTotalOffset],
                 dropped_total);
  csm::wr_u32_le(&payload[csm::kCanRxSegmentFifoOverflowTotalOffset],
                 fifo_overflow_total);
  payload[csm::kCanRxSegmentSchemaOffset] = csm::kCanRxSegmentSchema;
  payload[csm::kCanRxSegmentHeaderSizeOffset] =
      static_cast<uint8_t>(csm::kCanRxSegmentHeaderLen);
  csm::wr_u64_le(&payload[csm::kCanRxSegmentBaseMonoUsOffset], base_mono_us);

  for (uint8_t index = 0; index < count; ++index) {
    const CanRxSegmentItem& item = items[index];
    uint8_t* entry =
        &payload[csm::kCanRxSegmentEntriesOffset +
                 static_cast<uint16_t>(index) * csm::kCanRxSegmentEntryLen];
    csm::wr_u16_le(
        &entry[csm::kCanRxSegmentCompactEntryCaptureDeltaOffset],
        static_cast<uint16_t>(item.capture_seq - first_capture_seq));
    csm::wr_u32_le(
        &entry[csm::kCanRxSegmentCompactEntryMonoDeltaUsOffset],
        static_cast<uint32_t>(item.mono_us - base_mono_us));
    csm::wr_u32_le(&entry[csm::kCanRxSegmentCompactEntryIdFlagsOffset],
                   item.can_id_flags);
    entry[csm::kCanRxSegmentCompactEntryDlcFlagsOffset] = item.dlc_flags;
    entry[csm::kCanRxSegmentCompactEntryBusOffset] = item.bus;
    memcpy(&entry[csm::kCanRxSegmentCompactEntryDataOffset], item.data,
           sizeof(item.data));
  }
  return payload_len;
}

void CanRxSegmentBuilder::begin(CanRxSegmentEmitFn emit_fn, void* context,
                                uint32_t flush_us) {
  emit_fn_ = emit_fn;
  context_ = context;
  flush_us_ = flush_us;
  pending_count_ = 0;
  first_pending_us_ = 0;
  segment_seq_next_ = 0;
}

bool CanRxSegmentBuilder::ageDue(uint32_t now_us) const {
  return pending_count_ > 0 && static_cast<uint32_t>(now_us - first_pending_us_) >= flush_us_;
}

bool CanRxSegmentBuilder::compactDeltasFit(
    const CanRxSegmentItem& item) const {
  if (pending_count_ == 0) {
    return true;
  }
  const CanRxSegmentItem& first = pending_[0];
  return item.capture_seq >= first.capture_seq &&
         item.capture_seq - first.capture_seq <= 0xFFFFULL &&
         item.mono_us >= first.mono_us &&
         item.mono_us - first.mono_us <= 0xFFFFFFFFULL;
}

bool CanRxSegmentBuilder::push(const CanRxSegmentItem& item, uint32_t now_us) {
  bool preceding_flush_ok = true;
  if (!compactDeltasFit(item)) {
    preceding_flush_ok = flush();
  }

  if (pending_count_ == 0) {
    first_pending_us_ = now_us;
  }

  if (pending_count_ < csm::kCanRxSegmentMaxFrames) {
    pending_[pending_count_++] = item;
  }

  if (pending_count_ >= csm::kCanRxSegmentMaxFrames) {
    return flush() && preceding_flush_ok;
  }
  return preceding_flush_ok;
}

bool CanRxSegmentBuilder::flush() {
  if (pending_count_ == 0) {
    return true;
  }

  const uint8_t count = pending_count_;
  const uint64_t segment_seq = segment_seq_next_++;
  const bool ok = emit_fn_ != nullptr && emit_fn_(context_, pending_, count, segment_seq);
  pending_count_ = 0;
  first_pending_us_ = 0;
  return ok;
}

bool CanRxSegmentBuilder::flushIfDue(uint32_t now_us) {
  if (!ageDue(now_us)) {
    return true;
  }
  return flush();
}

void CanRxSegmentBuilder::discardPending() {
  pending_count_ = 0;
  first_pending_us_ = 0;
}

void CanRxSegmentBuilder::resetForEpoch(uint64_t next_segment_seq) {
  discardPending();
  segment_seq_next_ = next_segment_seq;
}

}  // namespace csm::board::uplink
