#pragma once

#include <stdint.h>

#include "board/uplink/UplinkPriority.h"
#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

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
#ifndef BOARD_UPLINK_POOL_LARGE_CRITICAL_RESERVE
#define BOARD_UPLINK_POOL_LARGE_CRITICAL_RESERVE 4
#endif
#ifndef BOARD_UPLINK_POOL_MEDIUM_BLOCKS
#define BOARD_UPLINK_POOL_MEDIUM_BLOCKS 16
#endif
#ifndef BOARD_UPLINK_POOL_MEDIUM_PAYLOAD_BYTES
#define BOARD_UPLINK_POOL_MEDIUM_PAYLOAD_BYTES 128
#endif
#ifndef BOARD_UPLINK_POOL_SMALL_BLOCKS
#define BOARD_UPLINK_POOL_SMALL_BLOCKS 24
#endif

namespace csm::board::uplink {

static_assert(BOARD_UPLINK_POOL_LARGE_CAN_RESERVE +
                      BOARD_UPLINK_POOL_LARGE_CRITICAL_RESERVE <=
                  BOARD_UPLINK_POOL_LARGE_BLOCKS,
              "large-payload reserves exceed pool capacity");
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
static_assert(BOARD_UPLINK_POOL_MEDIUM_PAYLOAD_BYTES >=
                  csm::kTransportDiagnosticPayloadLen,
              "Service/HIL transport diagnostic must remain medium-pool admissible");
#endif

struct AdmissionCounters {
  uint32_t record_accept_total = 0;
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
  uint32_t pool_large_critical_reserve_used_high_water = 0;
  uint32_t descriptor_high_water_total = 0;
  uint32_t payload_release_total = 0;
};

class RecordAdmission {
 public:
  enum class PoolClass : uint8_t { None = 0, Small, Medium, Large };

  struct PayloadRef {
    PoolClass pool_class = PoolClass::None;
    uint8_t pool_id = 0;
    uint16_t length = 0;
  };

  struct Record {
    csm::RecordType type = static_cast<csm::RecordType>(0);
    uint16_t length = 0;
    uint8_t flags = 0;
    UplinkPriority priority = UplinkPriority::Normal;
    PayloadRef payload;
  };

  void begin();
  bool enqueue(csm::RecordType type, const uint8_t* payload, uint16_t length,
               UplinkPriority priority, uint8_t flags = 0);
  bool popNext(Record& record);
  const uint8_t* payload(const Record& record) const;
  void release(Record& record);
  void discardAll();

  bool hasQueuedRecords() const;
  bool hasQueueSpace(UplinkPriority priority) const;
  bool pressureActive() const;
  uint32_t queuedRecordCount() const;
  uint32_t queuedPayloadBytes() const { return queued_payload_bytes_; }
  uint32_t queuedPayloadHighWaterBytes() const {
    return counters_.queued_payload_high_water_bytes;
  }
  uint32_t poolUsedBytes() const;
  uint32_t poolLargeUsed() const { return large_used_count_; }
  uint32_t poolLargeCanReserveUsed() const;
  uint32_t poolLargeCriticalReserveUsed() const;
  const AdmissionCounters& counters() const { return counters_; }
  void noteEncodeFailure(UplinkPriority priority);

 private:
  struct DescriptorQueue {
    Record* records = nullptr;
    uint8_t capacity = 0;
    uint8_t head = 0;
    uint8_t tail = 0;
    uint8_t count = 0;
  };

  Record critical_records_[BOARD_UPLINK_CRITICAL_QUEUE_RECORDS] = {};
  Record can_truth_records_[BOARD_UPLINK_CAN_TRUTH_QUEUE_RECORDS] = {};
  Record normal_records_[BOARD_UPLINK_NORMAL_QUEUE_RECORDS] = {};
  Record diagnostic_records_[BOARD_UPLINK_DIAGNOSTIC_QUEUE_RECORDS] = {};
  DescriptorQueue critical_queue_;
  DescriptorQueue can_truth_queue_;
  DescriptorQueue normal_queue_;
  DescriptorQueue diagnostic_queue_;

  uint8_t pool_large_[BOARD_UPLINK_POOL_LARGE_BLOCKS][csm::kMaxPayloadLen] = {};
  uint8_t pool_medium_[BOARD_UPLINK_POOL_MEDIUM_BLOCKS]
                      [BOARD_UPLINK_POOL_MEDIUM_PAYLOAD_BYTES] = {};
  uint8_t pool_small_[BOARD_UPLINK_POOL_SMALL_BLOCKS][64] = {};
  bool pool_large_used_[BOARD_UPLINK_POOL_LARGE_BLOCKS] = {};
  bool pool_medium_used_[BOARD_UPLINK_POOL_MEDIUM_BLOCKS] = {};
  bool pool_small_used_[BOARD_UPLINK_POOL_SMALL_BLOCKS] = {};
  uint8_t large_used_count_ = 0;
  uint8_t medium_used_count_ = 0;
  uint8_t small_used_count_ = 0;
  uint32_t queued_payload_bytes_ = 0;
  AdmissionCounters counters_;

  void resetQueues();
  void resetPools();
  DescriptorQueue& queueFor(UplinkPriority priority);
  const DescriptorQueue& queueFor(UplinkPriority priority) const;
  bool push(DescriptorQueue& queue, const Record& record);
  bool pop(DescriptorQueue& queue, Record& record);
  void noteDrop(UplinkPriority priority, bool encode_failure = false);
  void noteQueueHighWater(UplinkPriority priority, uint32_t count);
  void noteHighWater();
  bool allocate(UplinkPriority priority, uint16_t length, PayloadRef& ref);
  bool allocateSmall(uint16_t length, PayloadRef& ref);
  bool allocateMedium(uint16_t length, PayloadRef& ref);
  bool allocateLarge(uint16_t length, UplinkPriority priority, PayloadRef& ref);
  void releasePayload(PayloadRef& ref);
  uint8_t* payloadPtr(const PayloadRef& ref);
  const uint8_t* payloadPtr(const PayloadRef& ref) const;
};

}  // namespace csm::board::uplink
