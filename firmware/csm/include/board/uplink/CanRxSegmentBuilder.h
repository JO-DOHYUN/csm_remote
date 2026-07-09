#pragma once

#include <stdint.h>

#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

struct CanRxSegmentItem {
  uint64_t capture_seq = 0;
  uint64_t mono_us = 0;
  uint32_t can_id_flags = 0;
  uint8_t dlc_flags = 0;
  uint8_t bus = 0;
  uint8_t data[8] = {};
};

using CanRxSegmentEmitFn = bool (*)(void* context, const CanRxSegmentItem* items,
                                    uint8_t count, uint64_t segment_seq);

class CanRxSegmentBuilder {
 public:
  void begin(CanRxSegmentEmitFn emit_fn, void* context, uint32_t flush_us);

  bool push(const CanRxSegmentItem& item, uint32_t now_us);
  bool flush();
  bool flushIfDue(uint32_t now_us);
  void discardPending();
  void resetForEpoch(uint64_t next_segment_seq = 0);

  uint8_t pendingCount() const { return pending_count_; }
  uint64_t nextSegmentSeq() const { return segment_seq_next_; }

 private:
  CanRxSegmentItem pending_[csm::kCanRxSegmentMaxFrames] = {};
  uint8_t pending_count_ = 0;
  uint32_t first_pending_us_ = 0;
  uint32_t flush_us_ = 1000;
  uint64_t segment_seq_next_ = 0;
  CanRxSegmentEmitFn emit_fn_ = nullptr;
  void* context_ = nullptr;

  bool ageDue(uint32_t now_us) const;
};

}  // namespace csm::board::uplink
