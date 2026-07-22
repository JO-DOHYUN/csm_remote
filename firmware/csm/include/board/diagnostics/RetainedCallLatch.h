#pragma once

#include <cstddef>
#include <cstdint>

#include "board/diagnostics/BootRecovery.h"

namespace csm::board::diagnostics {

// One crash-surviving call boundary. The runtime owner writes Enter before a
// potentially blocking vendor API and Leave immediately after it returns.
// Two checksum-last slots ensure a torn update cannot destroy the preceding
// valid boundary. Ownership transfers from setup() to the worker before the
// first call; there is never more than one writer at a time.
struct RetainedCallSnapshot {
  bool valid = false;
  bool in_progress = false;
  bool completed = false;
  bool contract_changed = false;
  uint16_t owner = 0;
  uint16_t operation = 0;
  uint32_t write_sequence = 0;
  uint32_t boot_sequence = 0;
  uint64_t runtime_contract_id = 0;
  uint32_t call_sequence = 0;
  uint32_t started_uptime_ms = 0;
  uint32_t completed_uptime_ms = 0;
  uint32_t duration_us = 0;
  int32_t result = 0;
};

class RetainedCallLatch {
 public:
  static constexpr std::size_t kSlotBytes = 64;
  static constexpr std::size_t kSlotCount = 2;
  static constexpr std::size_t kChecksumOffset = 60;
  static constexpr std::size_t kRequiredStorageBytes =
      kSlotBytes * kSlotCount;

  RetainedCallLatch(void* retained_storage, std::size_t retained_storage_bytes,
                    RetainedStorageAdapter adapter = {});

  // Recovers the last complete slot, then publishes an idle marker for this
  // boot. Call once before the worker starts.
  bool beginSession(uint64_t runtime_contract_id, uint32_t boot_sequence,
                    uint32_t uptime_ms);

  // Synchronous persistence boundary. enter() must complete before invoking
  // the vendor API; leave() must be the first persistence action on return.
  bool enter(uint16_t owner, uint16_t operation, uint32_t call_sequence,
             uint32_t uptime_ms);
  bool leave(int32_t result, uint32_t duration_us, uint32_t uptime_ms);

  bool ready() const { return ready_; }
  RetainedCallSnapshot previousSnapshot() const { return previous_; }

  static constexpr std::size_t slotOffset(std::size_t index) {
    return index * kSlotBytes;
  }

 private:
  bool readSlot(std::size_t index, RetainedCallSnapshot& snapshot) const;
  bool commit();
  void flush(std::size_t offset, std::size_t bytes) const;
  void barrier() const;

  void* storage_address_ = nullptr;
  volatile uint8_t* storage_ = nullptr;
  std::size_t storage_bytes_ = 0;
  RetainedStorageAdapter adapter_{};
  RetainedCallSnapshot current_{};
  RetainedCallSnapshot previous_{};
  int8_t active_slot_ = -1;
  bool ready_ = false;
};

}  // namespace csm::board::diagnostics
