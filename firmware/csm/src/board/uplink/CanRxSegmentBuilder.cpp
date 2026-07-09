#include "board/uplink/CanRxSegmentBuilder.h"

namespace csm::board::uplink {

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

bool CanRxSegmentBuilder::push(const CanRxSegmentItem& item, uint32_t now_us) {
  if (pending_count_ == 0) {
    first_pending_us_ = now_us;
  }

  if (pending_count_ < csm::kCanRxSegmentMaxFrames) {
    pending_[pending_count_++] = item;
  }

  if (pending_count_ >= csm::kCanRxSegmentMaxFrames) {
    return flush();
  }
  return true;
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
