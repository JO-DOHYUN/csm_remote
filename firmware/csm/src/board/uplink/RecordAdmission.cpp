#include "board/uplink/RecordAdmission.h"

#include <string.h>

namespace csm::board::uplink {

namespace {
constexpr uint16_t kSmallPayloadBytes = 64;
constexpr uint16_t kMediumPayloadBytes = 128;
constexpr uint8_t can_reserve() {
  return BOARD_UPLINK_POOL_LARGE_CAN_RESERVE > BOARD_UPLINK_POOL_LARGE_BLOCKS
             ? BOARD_UPLINK_POOL_LARGE_BLOCKS
             : BOARD_UPLINK_POOL_LARGE_CAN_RESERVE;
}
constexpr uint8_t critical_reserve() {
  return BOARD_UPLINK_POOL_LARGE_CRITICAL_RESERVE >
          BOARD_UPLINK_POOL_LARGE_BLOCKS
      ? BOARD_UPLINK_POOL_LARGE_BLOCKS
      : BOARD_UPLINK_POOL_LARGE_CRITICAL_RESERVE;
}
}  // namespace

void RecordAdmission::begin() {
  counters_ = {};
  resetQueues();
  resetPools();
}

bool RecordAdmission::enqueue(csm::RecordType type, const uint8_t* payload_bytes,
                              uint16_t length, UplinkPriority priority, uint8_t flags) {
  if (length > csm::kMaxPayloadLen || (length > 0 && payload_bytes == nullptr)) {
    noteDrop(priority, true);
    return false;
  }
  PayloadRef ref;
  if (!allocate(priority, length, ref)) {
    counters_.pool_alloc_fail_total++;
    if (priority == UplinkPriority::CanTruth) {
      counters_.can_truth_pool_alloc_fail_total++;
    }
    noteDrop(priority);
    return false;
  }
  if (length > 0) {
    memcpy(payloadPtr(ref), payload_bytes, length);
  }
  Record record;
  record.type = type;
  record.length = length;
  record.flags = flags;
  record.priority = priority;
  record.payload = ref;
  DescriptorQueue& queue = queueFor(priority);
  if (!push(queue, record)) {
    releasePayload(ref);
    noteDrop(priority);
    return false;
  }
  queued_payload_bytes_ += length;
  counters_.record_accept_total++;
  noteQueueHighWater(priority, queue.count);
  noteHighWater();
  return true;
}

bool RecordAdmission::popNext(Record& record) {
  if (pop(critical_queue_, record) || pop(can_truth_queue_, record) ||
      pop(normal_queue_, record) || pop(diagnostic_queue_, record)) {
    return true;
  }
  return false;
}

const uint8_t* RecordAdmission::payload(const Record& record) const {
  return payloadPtr(record.payload);
}

void RecordAdmission::release(Record& record) {
  releasePayload(record.payload);
  record = {};
  counters_.payload_release_total++;
}

void RecordAdmission::discardAll() {
  Record record;
  while (popNext(record)) {
    release(record);
  }
  resetQueues();
  resetPools();
}

bool RecordAdmission::hasQueuedRecords() const { return queuedRecordCount() > 0; }

bool RecordAdmission::hasQueueSpace(UplinkPriority priority) const {
  const DescriptorQueue& queue = queueFor(priority);
  return queue.records != nullptr && queue.count < queue.capacity;
}

bool RecordAdmission::pressureActive() const {
  return can_truth_queue_.count >= ((can_truth_queue_.capacity * 3U) / 4U) ||
         critical_queue_.count >= ((critical_queue_.capacity * 3U) / 4U) ||
         normal_queue_.count >= normal_queue_.capacity ||
         diagnostic_queue_.count >= diagnostic_queue_.capacity;
}

uint32_t RecordAdmission::queuedRecordCount() const {
  return static_cast<uint32_t>(critical_queue_.count) + can_truth_queue_.count +
         normal_queue_.count + diagnostic_queue_.count;
}

uint32_t RecordAdmission::poolUsedBytes() const {
  return static_cast<uint32_t>(large_used_count_) * csm::kMaxPayloadLen +
         static_cast<uint32_t>(medium_used_count_) * kMediumPayloadBytes +
         static_cast<uint32_t>(small_used_count_) * kSmallPayloadBytes;
}

uint32_t RecordAdmission::poolLargeCanReserveUsed() const {
  const uint8_t reserve = can_reserve();
  const uint8_t free_large = BOARD_UPLINK_POOL_LARGE_BLOCKS - large_used_count_;
  return free_large >= reserve ? 0 : reserve - free_large;
}

uint32_t RecordAdmission::poolLargeCriticalReserveUsed() const {
  const uint8_t reserve = critical_reserve();
  const uint8_t free_large = BOARD_UPLINK_POOL_LARGE_BLOCKS - large_used_count_;
  return free_large >= reserve ? 0 : reserve - free_large;
}

void RecordAdmission::noteEncodeFailure(UplinkPriority priority) {
  noteDrop(priority, true);
}

void RecordAdmission::resetQueues() {
  critical_queue_ = {critical_records_, BOARD_UPLINK_CRITICAL_QUEUE_RECORDS, 0, 0, 0};
  can_truth_queue_ = {can_truth_records_, BOARD_UPLINK_CAN_TRUTH_QUEUE_RECORDS, 0, 0, 0};
  normal_queue_ = {normal_records_, BOARD_UPLINK_NORMAL_QUEUE_RECORDS, 0, 0, 0};
  diagnostic_queue_ = {diagnostic_records_, BOARD_UPLINK_DIAGNOSTIC_QUEUE_RECORDS, 0, 0, 0};
  queued_payload_bytes_ = 0;
}

void RecordAdmission::resetPools() {
  memset(pool_large_used_, 0, sizeof(pool_large_used_));
  memset(pool_medium_used_, 0, sizeof(pool_medium_used_));
  memset(pool_small_used_, 0, sizeof(pool_small_used_));
  large_used_count_ = 0;
  medium_used_count_ = 0;
  small_used_count_ = 0;
}

RecordAdmission::DescriptorQueue& RecordAdmission::queueFor(UplinkPriority priority) {
  if (priority == UplinkPriority::Critical) return critical_queue_;
  if (priority == UplinkPriority::CanTruth) return can_truth_queue_;
  if (priority == UplinkPriority::Diagnostic) return diagnostic_queue_;
  return normal_queue_;
}

const RecordAdmission::DescriptorQueue& RecordAdmission::queueFor(
    UplinkPriority priority) const {
  if (priority == UplinkPriority::Critical) return critical_queue_;
  if (priority == UplinkPriority::CanTruth) return can_truth_queue_;
  if (priority == UplinkPriority::Diagnostic) return diagnostic_queue_;
  return normal_queue_;
}

bool RecordAdmission::push(DescriptorQueue& queue, const Record& record) {
  if (queue.records == nullptr || queue.count >= queue.capacity) return false;
  queue.records[queue.tail] = record;
  queue.tail = static_cast<uint8_t>((queue.tail + 1U) % queue.capacity);
  queue.count++;
  return true;
}

bool RecordAdmission::pop(DescriptorQueue& queue, Record& record) {
  if (queue.records == nullptr || queue.count == 0) return false;
  record = queue.records[queue.head];
  queue.records[queue.head] = {};
  queue.head = static_cast<uint8_t>((queue.head + 1U) % queue.capacity);
  queue.count--;
  queued_payload_bytes_ =
      queued_payload_bytes_ >= record.length ? queued_payload_bytes_ - record.length : 0;
  return true;
}

void RecordAdmission::noteDrop(UplinkPriority priority, bool encode_failure) {
  counters_.record_drop_total++;
  if (encode_failure) counters_.record_encode_fail_total++;
  switch (priority) {
    case UplinkPriority::Critical: counters_.critical_drop_total++; break;
    case UplinkPriority::CanTruth: counters_.can_truth_drop_total++; break;
    case UplinkPriority::Diagnostic:
      counters_.diagnostic_drop_total++;
      counters_.diagnostic_suppressed_total++;
      break;
    case UplinkPriority::Normal:
    default: counters_.normal_drop_total++; break;
  }
}

void RecordAdmission::noteQueueHighWater(UplinkPriority priority, uint32_t count) {
  uint32_t* high_water = &counters_.normal_queue_high_water;
  if (priority == UplinkPriority::Critical) high_water = &counters_.critical_queue_high_water;
  if (priority == UplinkPriority::CanTruth) high_water = &counters_.can_truth_queue_high_water;
  if (priority == UplinkPriority::Diagnostic) high_water = &counters_.diagnostic_queue_high_water;
  if (count > *high_water) *high_water = count;
}

void RecordAdmission::noteHighWater() {
  const uint32_t descriptors = queuedRecordCount();
  if (descriptors > counters_.descriptor_high_water_total) {
    counters_.descriptor_high_water_total = descriptors;
  }
  if (queued_payload_bytes_ > counters_.queued_payload_high_water_bytes) {
    counters_.queued_payload_high_water_bytes = queued_payload_bytes_;
  }
}

bool RecordAdmission::allocate(UplinkPriority priority, uint16_t length, PayloadRef& ref) {
  ref = {};
  if (length == 0) return true;
  if (priority == UplinkPriority::CanTruth) {
    return allocateLarge(length, priority, ref);
  }
  if (length <= kSmallPayloadBytes) {
    if (allocateSmall(length, ref)) return true;
    if (priority != UplinkPriority::Diagnostic && allocateMedium(length, ref)) return true;
    return priority == UplinkPriority::Critical &&
        allocateLarge(length, priority, ref);
  }
  if (length <= kMediumPayloadBytes) {
    if (allocateMedium(length, ref)) return true;
    return priority == UplinkPriority::Critical &&
        allocateLarge(length, priority, ref);
  }
  return priority != UplinkPriority::Diagnostic &&
      allocateLarge(length, priority, ref);
}

bool RecordAdmission::allocateSmall(uint16_t length, PayloadRef& ref) {
  if (length > kSmallPayloadBytes) return false;
  for (uint8_t i = 0; i < BOARD_UPLINK_POOL_SMALL_BLOCKS; ++i) {
    if (!pool_small_used_[i]) {
      pool_small_used_[i] = true;
      small_used_count_++;
      ref = {PoolClass::Small, i, length};
      if (small_used_count_ > counters_.pool_small_used_high_water)
        counters_.pool_small_used_high_water = small_used_count_;
      return true;
    }
  }
  return false;
}

bool RecordAdmission::allocateMedium(uint16_t length, PayloadRef& ref) {
  if (length > kMediumPayloadBytes) return false;
  for (uint8_t i = 0; i < BOARD_UPLINK_POOL_MEDIUM_BLOCKS; ++i) {
    if (!pool_medium_used_[i]) {
      pool_medium_used_[i] = true;
      medium_used_count_++;
      ref = {PoolClass::Medium, i, length};
      if (medium_used_count_ > counters_.pool_medium_used_high_water)
        counters_.pool_medium_used_high_water = medium_used_count_;
      return true;
    }
  }
  return false;
}

bool RecordAdmission::allocateLarge(uint16_t length, UplinkPriority priority,
                                    PayloadRef& ref) {
  if (length > csm::kMaxPayloadLen) return false;
  const uint8_t free_large = BOARD_UPLINK_POOL_LARGE_BLOCKS - large_used_count_;
  if (priority == UplinkPriority::CanTruth) {
    // CAN truth has a large quota, but it may never consume the blocks needed
    // by current Critical records (REMOTE_CONTROL_STATE, BOARD_HEALTH and
    // CAPABILITY). This is the inverse boundary missing from the old CAN-only
    // reserve.
    if (free_large <= critical_reserve()) return false;
  } else if (priority != UplinkPriority::Critical) {
    // Normal traffic may consume neither protected class. Critical is allowed
    // to borrow the CAN reserve because it is the higher-priority evidence.
    if (free_large <= can_reserve() + critical_reserve()) return false;
  }
  for (uint8_t i = 0; i < BOARD_UPLINK_POOL_LARGE_BLOCKS; ++i) {
    if (!pool_large_used_[i]) {
      pool_large_used_[i] = true;
      large_used_count_++;
      ref = {PoolClass::Large, i, length};
      if (large_used_count_ > counters_.pool_large_used_high_water)
        counters_.pool_large_used_high_water = large_used_count_;
      const uint32_t reserve_used = poolLargeCanReserveUsed();
      if (reserve_used > counters_.pool_large_can_reserve_used_high_water)
        counters_.pool_large_can_reserve_used_high_water = reserve_used;
      const uint32_t critical_reserve_used = poolLargeCriticalReserveUsed();
      if (critical_reserve_used >
          counters_.pool_large_critical_reserve_used_high_water) {
        counters_.pool_large_critical_reserve_used_high_water =
            critical_reserve_used;
      }
      return true;
    }
  }
  return false;
}

void RecordAdmission::releasePayload(PayloadRef& ref) {
  if (ref.pool_class == PoolClass::Small && ref.pool_id < BOARD_UPLINK_POOL_SMALL_BLOCKS &&
      pool_small_used_[ref.pool_id]) {
    pool_small_used_[ref.pool_id] = false;
    if (small_used_count_ > 0) small_used_count_--;
  } else if (ref.pool_class == PoolClass::Medium &&
             ref.pool_id < BOARD_UPLINK_POOL_MEDIUM_BLOCKS && pool_medium_used_[ref.pool_id]) {
    pool_medium_used_[ref.pool_id] = false;
    if (medium_used_count_ > 0) medium_used_count_--;
  } else if (ref.pool_class == PoolClass::Large &&
             ref.pool_id < BOARD_UPLINK_POOL_LARGE_BLOCKS && pool_large_used_[ref.pool_id]) {
    pool_large_used_[ref.pool_id] = false;
    if (large_used_count_ > 0) large_used_count_--;
  }
  ref = {};
}

uint8_t* RecordAdmission::payloadPtr(const PayloadRef& ref) {
  if (ref.pool_class == PoolClass::Small && ref.pool_id < BOARD_UPLINK_POOL_SMALL_BLOCKS)
    return pool_small_[ref.pool_id];
  if (ref.pool_class == PoolClass::Medium && ref.pool_id < BOARD_UPLINK_POOL_MEDIUM_BLOCKS)
    return pool_medium_[ref.pool_id];
  if (ref.pool_class == PoolClass::Large && ref.pool_id < BOARD_UPLINK_POOL_LARGE_BLOCKS)
    return pool_large_[ref.pool_id];
  return nullptr;
}

const uint8_t* RecordAdmission::payloadPtr(const PayloadRef& ref) const {
  return const_cast<RecordAdmission*>(this)->payloadPtr(ref);
}

}  // namespace csm::board::uplink
