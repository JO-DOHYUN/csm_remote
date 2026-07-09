#pragma once

#include <stdint.h>

#include "board/uplink/SerialTxScheduler.h"
#include "protocol/TypedFrame.h"

#ifndef BOARD_UPLINK_CRITICAL_QUEUE_RECORDS
#define BOARD_UPLINK_CRITICAL_QUEUE_RECORDS 16
#endif

#ifndef BOARD_UPLINK_CAN_TRUTH_QUEUE_RECORDS
#define BOARD_UPLINK_CAN_TRUTH_QUEUE_RECORDS 32
#endif

#ifndef BOARD_UPLINK_NORMAL_QUEUE_RECORDS
#define BOARD_UPLINK_NORMAL_QUEUE_RECORDS 8
#endif

#ifndef BOARD_UPLINK_DIAGNOSTIC_QUEUE_RECORDS
#define BOARD_UPLINK_DIAGNOSTIC_QUEUE_RECORDS 4
#endif

#ifndef BOARD_UPLINK_POOL_LARGE_BLOCKS
#define BOARD_UPLINK_POOL_LARGE_BLOCKS 40
#endif

#ifndef BOARD_UPLINK_POOL_LARGE_CAN_RESERVE
#define BOARD_UPLINK_POOL_LARGE_CAN_RESERVE 28
#endif

#ifndef BOARD_UPLINK_POOL_MEDIUM_BLOCKS
#define BOARD_UPLINK_POOL_MEDIUM_BLOCKS 16
#endif

#ifndef BOARD_UPLINK_POOL_SMALL_BLOCKS
#define BOARD_UPLINK_POOL_SMALL_BLOCKS 24
#endif

namespace csm::board::uplink {

struct UplinkCounters {
  uint32_t record_accept_total = 0;
  uint32_t record_tx_total = 0;
  uint32_t record_drop_total = 0;
  uint32_t record_encode_fail_total = 0;
  uint32_t critical_drop_total = 0;
  uint32_t can_truth_drop_total = 0;
  uint32_t normal_drop_total = 0;
  uint32_t diagnostic_drop_total = 0;
  uint32_t critical_queue_high_water = 0;
  uint32_t can_truth_queue_high_water = 0;
  uint32_t normal_queue_high_water = 0;
  uint32_t diagnostic_queue_high_water = 0;
  uint32_t queued_payload_high_water_bytes = 0;
  uint32_t pool_alloc_fail_total = 0;
  uint32_t can_truth_pool_alloc_fail_total = 0;
  uint32_t diagnostic_suppressed_total = 0;
  uint32_t pool_large_used_high_water = 0;
  uint32_t pool_medium_used_high_water = 0;
  uint32_t pool_small_used_high_water = 0;
  uint32_t pool_large_can_reserve_used_high_water = 0;
  uint32_t descriptor_high_water_total = 0;
  uint32_t active_frame_release_total = 0;
};

class UplinkScheduler {
 public:
  void begin(SerialTxScheduler* serial_tx);

  bool enqueueRecord(csm::RecordType type, const uint8_t* payload, uint16_t len,
                     UplinkPriority priority, uint8_t flags = 0);
  SerialTxServiceResult service(uint32_t byte_budget, uint32_t now_ms, uint32_t now_us);

  uint16_t nextTypedSeq() const { return typed_seq_; }
  const UplinkCounters& counters() const { return counters_; }
  bool hasQueuedRecords() const;
  bool hasQueueSpace(UplinkPriority priority) const;
  bool pressureActive() const;
  void discardQueuedRecords();
  uint32_t queuedRecordCount() const;
  uint32_t queuedPayloadBytes() const { return queued_payload_bytes_; }
  uint32_t queuedPayloadHighWaterBytes() const {
    return counters_.queued_payload_high_water_bytes;
  }
  uint32_t poolUsedBytes() const;
  uint32_t poolLargeUsed() const { return large_used_count_; }
  uint32_t poolMediumUsed() const { return medium_used_count_; }
  uint32_t poolSmallUsed() const { return small_used_count_; }
  uint32_t poolLargeCanReserveUsed() const;

 private:
  enum class PoolClass : uint8_t {
    None = 0,
    Small,
    Medium,
    Large,
  };

  struct PoolRef {
    PoolClass pool_class = PoolClass::None;
    uint8_t pool_id = 0;
    uint16_t offset = 0;
    uint16_t length = 0;
  };

  struct PendingDescriptor {
    csm::RecordType type = static_cast<csm::RecordType>(0);
    uint16_t len = 0;
    uint8_t flags = 0;
    UplinkPriority priority = UplinkPriority::Normal;
    PoolRef payload;
  };

  struct DescriptorQueue {
    PendingDescriptor* records = nullptr;
    uint8_t capacity = 0;
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;
  };

  SerialTxScheduler* serial_tx_ = nullptr;
  uint16_t typed_seq_ = 0;
  UplinkCounters counters_;
  uint32_t queued_payload_bytes_ = 0;
  bool active_descriptor_valid_ = false;
  PoolRef active_payload_;

  PendingDescriptor critical_records_[BOARD_UPLINK_CRITICAL_QUEUE_RECORDS] = {};
  PendingDescriptor can_truth_records_[BOARD_UPLINK_CAN_TRUTH_QUEUE_RECORDS] = {};
  PendingDescriptor normal_records_[BOARD_UPLINK_NORMAL_QUEUE_RECORDS] = {};
  PendingDescriptor diagnostic_records_[BOARD_UPLINK_DIAGNOSTIC_QUEUE_RECORDS] = {};
  DescriptorQueue critical_queue_;
  DescriptorQueue can_truth_queue_;
  DescriptorQueue normal_queue_;
  DescriptorQueue diagnostic_queue_;

  uint8_t pool_large_[BOARD_UPLINK_POOL_LARGE_BLOCKS][csm::kMaxPayloadLen] = {};
  uint8_t pool_medium_[BOARD_UPLINK_POOL_MEDIUM_BLOCKS][128] = {};
  uint8_t pool_small_[BOARD_UPLINK_POOL_SMALL_BLOCKS][64] = {};
  bool pool_large_used_[BOARD_UPLINK_POOL_LARGE_BLOCKS] = {};
  bool pool_medium_used_[BOARD_UPLINK_POOL_MEDIUM_BLOCKS] = {};
  bool pool_small_used_[BOARD_UPLINK_POOL_SMALL_BLOCKS] = {};
  uint8_t large_used_count_ = 0;
  uint8_t medium_used_count_ = 0;
  uint8_t small_used_count_ = 0;

  void resetQueues();
  void resetPools();
  DescriptorQueue& queueFor(UplinkPriority priority);
  const DescriptorQueue& queueFor(UplinkPriority priority) const;
  bool push(DescriptorQueue& queue, const PendingDescriptor& record);
  bool pop(DescriptorQueue& queue, PendingDescriptor& record);
  bool popNext(PendingDescriptor& record);
  void noteDrop(UplinkPriority priority, bool encode_failure = false);
  void noteQueueHighWater(UplinkPriority priority, uint32_t count);
  void noteQueuedPayloadHighWater();
  void noteDescriptorHighWater();
  bool allocatePayload(csm::RecordType type, UplinkPriority priority, uint16_t len,
                       PoolRef& ref);
  bool allocateSmall(PoolRef& ref, uint16_t len);
  bool allocateMedium(PoolRef& ref, uint16_t len);
  bool allocateLarge(PoolRef& ref, uint16_t len, bool can_truth);
  void releasePayload(PoolRef& ref);
  uint8_t* payloadPtr(const PoolRef& ref);
  const uint8_t* payloadPtr(const PoolRef& ref) const;
  bool encodeAndStage(const PendingDescriptor& record);
  void releaseCompletedActiveFrame();
  static void accumulate(SerialTxServiceResult& total,
                         const SerialTxServiceResult& next);
};

}  // namespace csm::board::uplink
