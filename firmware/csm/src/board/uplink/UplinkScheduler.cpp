#include "board/uplink/UplinkScheduler.h"

#include <Arduino.h>
#include <string.h>

namespace csm::board::uplink {

namespace {

static constexpr uint16_t kSmallPayloadBytes = 64;
static constexpr uint16_t kMediumPayloadBytes = 128;

static constexpr uint8_t clamp_can_reserve() {
  return BOARD_UPLINK_POOL_LARGE_CAN_RESERVE > BOARD_UPLINK_POOL_LARGE_BLOCKS
             ? BOARD_UPLINK_POOL_LARGE_BLOCKS
             : BOARD_UPLINK_POOL_LARGE_CAN_RESERVE;
}

}  // namespace

void UplinkScheduler::begin(SerialTxScheduler* serial_tx) {
  serial_tx_ = serial_tx;
  typed_seq_ = 0;
  counters_ = {};
  active_descriptor_valid_ = false;
  active_payload_ = {};
  resetQueues();
  resetPools();
}

bool UplinkScheduler::enqueueRecord(csm::RecordType type, const uint8_t* payload,
                                    uint16_t len, UplinkPriority priority,
                                    uint8_t flags) {
  if (len > csm::kMaxPayloadLen || (len > 0 && payload == nullptr)) {
    noteDrop(priority, true);
    return false;
  }

  PoolRef payload_ref;
  if (!allocatePayload(type, priority, len, payload_ref)) {
    counters_.pool_alloc_fail_total++;
    if (priority == UplinkPriority::CanTruth) {
      counters_.can_truth_pool_alloc_fail_total++;
    }
    noteDrop(priority);
    return false;
  }

  if (len > 0) {
    memcpy(payloadPtr(payload_ref), payload, len);
  }

  PendingDescriptor record;
  record.type = type;
  record.len = len;
  record.flags = flags;
  record.priority = priority;
  record.payload = payload_ref;

  DescriptorQueue& queue = queueFor(priority);
  if (!push(queue, record)) {
    releasePayload(payload_ref);
    noteDrop(priority);
    return false;
  }

  queued_payload_bytes_ += len;
  counters_.record_accept_total++;
  noteQueueHighWater(priority, queue.count);
  noteDescriptorHighWater();
  noteQueuedPayloadHighWater();
  return true;
}

SerialTxServiceResult UplinkScheduler::service(uint32_t byte_budget, uint32_t now_ms,
                                               uint32_t now_us) {
  SerialTxServiceResult result;
  if (serial_tx_ == nullptr || byte_budget == 0) {
    return result;
  }

  accumulate(result, serial_tx_->service(byte_budget, now_ms, now_us));
  releaseCompletedActiveFrame();
  if (result.actual_bytes >= byte_budget || result.writes_attempted > 0 ||
      serial_tx_->hasActiveFrame() || serial_tx_->backpressureActive()) {
    return result;
  }

  PendingDescriptor record;
  if (!popNext(record)) {
    return result;
  }

  if (!encodeAndStage(record)) {
    return result;
  }

  const uint32_t remaining = byte_budget - result.actual_bytes;
  if (remaining > 0) {
    accumulate(result, serial_tx_->service(remaining, now_ms, micros()));
    releaseCompletedActiveFrame();
  }
  return result;
}

bool UplinkScheduler::hasQueuedRecords() const {
  return queuedRecordCount() > 0;
}

bool UplinkScheduler::hasQueueSpace(UplinkPriority priority) const {
  const DescriptorQueue& queue = queueFor(priority);
  return queue.records != nullptr && queue.count < queue.capacity;
}

bool UplinkScheduler::pressureActive() const {
  if (serial_tx_ != nullptr &&
      (serial_tx_->backpressureActive() || serial_tx_->hasActiveFrame())) {
    return true;
  }
  return can_truth_queue_.count >= ((can_truth_queue_.capacity * 3U) / 4U) ||
         normal_queue_.count >= normal_queue_.capacity ||
         diagnostic_queue_.count >= diagnostic_queue_.capacity ||
         critical_queue_.count >= ((critical_queue_.capacity * 3U) / 4U);
}

void UplinkScheduler::discardQueuedRecords() {
  if (serial_tx_ != nullptr) {
    serial_tx_->discardActiveFrame();
  }
  active_descriptor_valid_ = false;
  active_payload_ = {};
  resetQueues();
  resetPools();
}

uint32_t UplinkScheduler::queuedRecordCount() const {
  return static_cast<uint32_t>(critical_queue_.count) + can_truth_queue_.count +
         normal_queue_.count + diagnostic_queue_.count;
}

uint32_t UplinkScheduler::poolUsedBytes() const {
  return static_cast<uint32_t>(large_used_count_) * csm::kMaxPayloadLen +
         static_cast<uint32_t>(medium_used_count_) * kMediumPayloadBytes +
         static_cast<uint32_t>(small_used_count_) * kSmallPayloadBytes;
}

uint32_t UplinkScheduler::poolLargeCanReserveUsed() const {
  const uint8_t reserve = clamp_can_reserve();
  const uint8_t free_large = BOARD_UPLINK_POOL_LARGE_BLOCKS - large_used_count_;
  if (free_large >= reserve) {
    return 0;
  }
  return reserve - free_large;
}

void UplinkScheduler::resetQueues() {
  critical_queue_ =
      DescriptorQueue{critical_records_, BOARD_UPLINK_CRITICAL_QUEUE_RECORDS, 0, 0, 0};
  can_truth_queue_ =
      DescriptorQueue{can_truth_records_, BOARD_UPLINK_CAN_TRUTH_QUEUE_RECORDS, 0, 0, 0};
  normal_queue_ =
      DescriptorQueue{normal_records_, BOARD_UPLINK_NORMAL_QUEUE_RECORDS, 0, 0, 0};
  diagnostic_queue_ =
      DescriptorQueue{diagnostic_records_, BOARD_UPLINK_DIAGNOSTIC_QUEUE_RECORDS, 0, 0, 0};
  queued_payload_bytes_ = 0;
}

void UplinkScheduler::resetPools() {
  memset(pool_large_used_, 0, sizeof(pool_large_used_));
  memset(pool_medium_used_, 0, sizeof(pool_medium_used_));
  memset(pool_small_used_, 0, sizeof(pool_small_used_));
  large_used_count_ = 0;
  medium_used_count_ = 0;
  small_used_count_ = 0;
}

UplinkScheduler::DescriptorQueue& UplinkScheduler::queueFor(UplinkPriority priority) {
  if (priority == UplinkPriority::Critical) {
    return critical_queue_;
  }
  if (priority == UplinkPriority::CanTruth) {
    return can_truth_queue_;
  }
  if (priority == UplinkPriority::Diagnostic) {
    return diagnostic_queue_;
  }
  return normal_queue_;
}

const UplinkScheduler::DescriptorQueue& UplinkScheduler::queueFor(
    UplinkPriority priority) const {
  if (priority == UplinkPriority::Critical) {
    return critical_queue_;
  }
  if (priority == UplinkPriority::CanTruth) {
    return can_truth_queue_;
  }
  if (priority == UplinkPriority::Diagnostic) {
    return diagnostic_queue_;
  }
  return normal_queue_;
}

bool UplinkScheduler::push(DescriptorQueue& queue, const PendingDescriptor& record) {
  if (queue.records == nullptr || queue.capacity == 0 || queue.count >= queue.capacity) {
    return false;
  }
  queue.records[queue.tail] = record;
  queue.tail = static_cast<uint8_t>((queue.tail + 1U) % queue.capacity);
  queue.count++;
  return true;
}

bool UplinkScheduler::pop(DescriptorQueue& queue, PendingDescriptor& record) {
  if (queue.records == nullptr || queue.count == 0) {
    return false;
  }
  record = queue.records[queue.head];
  queue.records[queue.head] = {};
  queue.head = static_cast<uint8_t>((queue.head + 1U) % queue.capacity);
  queue.count--;
  if (queued_payload_bytes_ >= record.len) {
    queued_payload_bytes_ -= record.len;
  } else {
    queued_payload_bytes_ = 0;
  }
  return true;
}

bool UplinkScheduler::popNext(PendingDescriptor& record) {
  if (pop(critical_queue_, record)) {
    return true;
  }
  if (pop(can_truth_queue_, record)) {
    return true;
  }
  if (pop(normal_queue_, record)) {
    return true;
  }
  return pop(diagnostic_queue_, record);
}

void UplinkScheduler::noteDrop(UplinkPriority priority, bool encode_failure) {
  counters_.record_drop_total++;
  if (encode_failure) {
    counters_.record_encode_fail_total++;
  }
  switch (priority) {
    case UplinkPriority::Critical:
      counters_.critical_drop_total++;
      break;
    case UplinkPriority::CanTruth:
      counters_.can_truth_drop_total++;
      break;
    case UplinkPriority::Diagnostic:
      counters_.diagnostic_drop_total++;
      counters_.diagnostic_suppressed_total++;
      break;
    case UplinkPriority::Normal:
    default:
      counters_.normal_drop_total++;
      break;
  }
}

void UplinkScheduler::noteQueueHighWater(UplinkPriority priority, uint32_t count) {
  if (priority == UplinkPriority::Critical) {
    if (count > counters_.critical_queue_high_water) {
      counters_.critical_queue_high_water = count;
    }
    return;
  }
  if (priority == UplinkPriority::CanTruth) {
    if (count > counters_.can_truth_queue_high_water) {
      counters_.can_truth_queue_high_water = count;
    }
    return;
  }
  if (priority == UplinkPriority::Diagnostic) {
    if (count > counters_.diagnostic_queue_high_water) {
      counters_.diagnostic_queue_high_water = count;
    }
    return;
  }
  if (count > counters_.normal_queue_high_water) {
    counters_.normal_queue_high_water = count;
  }
}

void UplinkScheduler::noteQueuedPayloadHighWater() {
  if (queued_payload_bytes_ > counters_.queued_payload_high_water_bytes) {
    counters_.queued_payload_high_water_bytes = queued_payload_bytes_;
  }
}

void UplinkScheduler::noteDescriptorHighWater() {
  const uint32_t total = queuedRecordCount();
  if (total > counters_.descriptor_high_water_total) {
    counters_.descriptor_high_water_total = total;
  }
}

bool UplinkScheduler::allocatePayload(csm::RecordType /*type*/,
                                      UplinkPriority priority,
                                      uint16_t len,
                                      PoolRef& ref) {
  ref = {};
  if (len == 0) {
    ref.length = 0;
    return true;
  }

  if (priority == UplinkPriority::CanTruth) {
    return allocateLarge(ref, len, true);
  }

  if (len <= kSmallPayloadBytes) {
    if (allocateSmall(ref, len)) {
      return true;
    }
    if (priority != UplinkPriority::Diagnostic && allocateMedium(ref, len)) {
      return true;
    }
    return priority == UplinkPriority::Critical && allocateLarge(ref, len, false);
  }

  if (len <= kMediumPayloadBytes) {
    if (allocateMedium(ref, len)) {
      return true;
    }
    return priority == UplinkPriority::Critical && allocateLarge(ref, len, false);
  }

  if (priority == UplinkPriority::Diagnostic) {
    return false;
  }
  return allocateLarge(ref, len, false);
}

bool UplinkScheduler::allocateSmall(PoolRef& ref, uint16_t len) {
  if (len > kSmallPayloadBytes) {
    return false;
  }
  for (uint8_t i = 0; i < BOARD_UPLINK_POOL_SMALL_BLOCKS; ++i) {
    if (!pool_small_used_[i]) {
      pool_small_used_[i] = true;
      small_used_count_++;
      ref = PoolRef{PoolClass::Small, i, 0, len};
      if (small_used_count_ > counters_.pool_small_used_high_water) {
        counters_.pool_small_used_high_water = small_used_count_;
      }
      return true;
    }
  }
  return false;
}

bool UplinkScheduler::allocateMedium(PoolRef& ref, uint16_t len) {
  if (len > kMediumPayloadBytes) {
    return false;
  }
  for (uint8_t i = 0; i < BOARD_UPLINK_POOL_MEDIUM_BLOCKS; ++i) {
    if (!pool_medium_used_[i]) {
      pool_medium_used_[i] = true;
      medium_used_count_++;
      ref = PoolRef{PoolClass::Medium, i, 0, len};
      if (medium_used_count_ > counters_.pool_medium_used_high_water) {
        counters_.pool_medium_used_high_water = medium_used_count_;
      }
      return true;
    }
  }
  return false;
}

bool UplinkScheduler::allocateLarge(PoolRef& ref, uint16_t len, bool can_truth) {
  if (len > csm::kMaxPayloadLen) {
    return false;
  }
  const uint8_t reserve = clamp_can_reserve();
  const uint8_t free_large = BOARD_UPLINK_POOL_LARGE_BLOCKS - large_used_count_;
  if (!can_truth && free_large <= reserve) {
    return false;
  }

  for (uint8_t i = 0; i < BOARD_UPLINK_POOL_LARGE_BLOCKS; ++i) {
    if (!pool_large_used_[i]) {
      pool_large_used_[i] = true;
      large_used_count_++;
      ref = PoolRef{PoolClass::Large, i, 0, len};
      if (large_used_count_ > counters_.pool_large_used_high_water) {
        counters_.pool_large_used_high_water = large_used_count_;
      }
      const uint32_t reserve_used = poolLargeCanReserveUsed();
      if (reserve_used > counters_.pool_large_can_reserve_used_high_water) {
        counters_.pool_large_can_reserve_used_high_water = reserve_used;
      }
      return true;
    }
  }
  return false;
}

void UplinkScheduler::releasePayload(PoolRef& ref) {
  switch (ref.pool_class) {
    case PoolClass::Small:
      if (ref.pool_id < BOARD_UPLINK_POOL_SMALL_BLOCKS && pool_small_used_[ref.pool_id]) {
        pool_small_used_[ref.pool_id] = false;
        if (small_used_count_ > 0) {
          small_used_count_--;
        }
      }
      break;
    case PoolClass::Medium:
      if (ref.pool_id < BOARD_UPLINK_POOL_MEDIUM_BLOCKS && pool_medium_used_[ref.pool_id]) {
        pool_medium_used_[ref.pool_id] = false;
        if (medium_used_count_ > 0) {
          medium_used_count_--;
        }
      }
      break;
    case PoolClass::Large:
      if (ref.pool_id < BOARD_UPLINK_POOL_LARGE_BLOCKS && pool_large_used_[ref.pool_id]) {
        pool_large_used_[ref.pool_id] = false;
        if (large_used_count_ > 0) {
          large_used_count_--;
        }
      }
      break;
    case PoolClass::None:
    default:
      break;
  }
  ref = {};
}

uint8_t* UplinkScheduler::payloadPtr(const PoolRef& ref) {
  switch (ref.pool_class) {
    case PoolClass::Small:
      return ref.pool_id < BOARD_UPLINK_POOL_SMALL_BLOCKS ? pool_small_[ref.pool_id] : nullptr;
    case PoolClass::Medium:
      return ref.pool_id < BOARD_UPLINK_POOL_MEDIUM_BLOCKS ? pool_medium_[ref.pool_id] : nullptr;
    case PoolClass::Large:
      return ref.pool_id < BOARD_UPLINK_POOL_LARGE_BLOCKS ? pool_large_[ref.pool_id] : nullptr;
    case PoolClass::None:
    default:
      return nullptr;
  }
}

const uint8_t* UplinkScheduler::payloadPtr(const PoolRef& ref) const {
  return const_cast<UplinkScheduler*>(this)->payloadPtr(ref);
}

bool UplinkScheduler::encodeAndStage(const PendingDescriptor& record) {
  if (serial_tx_ == nullptr || serial_tx_->hasActiveFrame()) {
    PoolRef release_ref = record.payload;
    releasePayload(release_ref);
    noteDrop(record.priority);
    return false;
  }

  const uint32_t frame_len =
      static_cast<uint32_t>(csm::encoded_typed_frame_len(record.len));
  if (!serial_tx_->canEnqueue(frame_len, record.priority)) {
    serial_tx_->noteAdmissionFailure(frame_len, record.priority);
    PoolRef release_ref = record.payload;
    releasePayload(release_ref);
    noteDrop(record.priority);
    return false;
  }

  const uint8_t* payload = payloadPtr(record.payload);
  if (record.len > 0 && payload == nullptr) {
    PoolRef release_ref = record.payload;
    releasePayload(release_ref);
    noteDrop(record.priority, true);
    return false;
  }

  uint8_t frame[csm::encoded_typed_frame_len(csm::kMaxPayloadLen)];
  size_t written = 0;
  if (!csm::encode_typed_frame(frame, sizeof(frame), record.type, payload,
                               record.len, typed_seq_, record.flags, &written)) {
    PoolRef release_ref = record.payload;
    releasePayload(release_ref);
    noteDrop(record.priority, true);
    return false;
  }

  if (written != frame_len ||
      !serial_tx_->enqueueAtomic(frame, static_cast<uint32_t>(written), record.priority)) {
    PoolRef release_ref = record.payload;
    releasePayload(release_ref);
    noteDrop(record.priority);
    return false;
  }

  active_payload_ = record.payload;
  active_descriptor_valid_ = true;
  typed_seq_++;
  counters_.record_tx_total++;
  return true;
}

void UplinkScheduler::releaseCompletedActiveFrame() {
  if (!active_descriptor_valid_ || serial_tx_ == nullptr || serial_tx_->hasActiveFrame()) {
    return;
  }
  releasePayload(active_payload_);
  active_payload_ = {};
  active_descriptor_valid_ = false;
  counters_.active_frame_release_total++;
}

void UplinkScheduler::accumulate(SerialTxServiceResult& total,
                                 const SerialTxServiceResult& next) {
  total.requested_bytes += next.requested_bytes;
  total.actual_bytes += next.actual_bytes;
  total.writes_attempted += next.writes_attempted;
  total.writes_completed += next.writes_completed;
  total.partial_writes += next.partial_writes;
  total.backpressure_event = total.backpressure_event || next.backpressure_event;
  if (next.backpressure_duration_ms > total.backpressure_duration_ms) {
    total.backpressure_duration_ms = next.backpressure_duration_ms;
  }
}

}  // namespace csm::board::uplink
