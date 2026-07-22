#include "board/diagnostics/RetainedCallLatch.h"

namespace csm::board::diagnostics {
namespace {

constexpr uint32_t kMagic = 0x52434C54u;  // "RCLT"
constexpr uint16_t kFormatVersion = 1;
constexpr uint32_t kChecksumSeed = 2166136261u;
constexpr uint16_t kFlagInProgress = 1u << 0;
constexpr uint16_t kFlagCompleted = 1u << 1;

uint16_t readU16(const uint8_t* bytes, std::size_t offset) {
  return static_cast<uint16_t>(bytes[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t readU32(const uint8_t* bytes, std::size_t offset) {
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

uint64_t readU64(const uint8_t* bytes, std::size_t offset) {
  return static_cast<uint64_t>(readU32(bytes, offset)) |
         (static_cast<uint64_t>(readU32(bytes, offset + 4)) << 32);
}

void writeU16(uint8_t* bytes, std::size_t offset, uint16_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void writeU32(uint8_t* bytes, std::size_t offset, uint32_t value) {
  bytes[offset] = static_cast<uint8_t>(value);
  bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void writeU64(uint8_t* bytes, std::size_t offset, uint64_t value) {
  writeU32(bytes, offset, static_cast<uint32_t>(value));
  writeU32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
}

uint32_t checksum(const uint8_t* bytes, std::size_t length) {
  uint32_t value = kChecksumSeed;
  for (std::size_t index = 0; index < length; ++index) {
    value ^= bytes[index];
    value *= 16777619u;
  }
  return value == 0 ? 0xFFFFFFFFu : value;
}

uint32_t nextSequence(uint32_t value) {
  ++value;
  return value == 0 ? 1u : value;
}

bool sequenceNewer(uint32_t candidate, uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

}  // namespace

RetainedCallLatch::RetainedCallLatch(void* retained_storage,
                                     std::size_t retained_storage_bytes,
                                     RetainedStorageAdapter adapter)
    : storage_address_(retained_storage),
      storage_(static_cast<volatile uint8_t*>(retained_storage)),
      storage_bytes_(retained_storage_bytes),
      adapter_(adapter) {}

void RetainedCallLatch::barrier() const {
  if (adapter_.barrier != nullptr) adapter_.barrier(adapter_.context);
}

void RetainedCallLatch::flush(std::size_t offset, std::size_t bytes) const {
  if (adapter_.commit == nullptr || storage_address_ == nullptr || bytes == 0) {
    return;
  }
  const auto* base = static_cast<const uint8_t*>(storage_address_);
  adapter_.commit(adapter_.context, base + offset, bytes);
}

bool RetainedCallLatch::readSlot(std::size_t index,
                                 RetainedCallSnapshot& snapshot) const {
  if (index >= kSlotCount) return false;
  uint8_t bytes[kSlotBytes] = {};
  const std::size_t base = slotOffset(index);
  for (std::size_t offset = 0; offset < sizeof(bytes); ++offset) {
    bytes[offset] = storage_[base + offset];
  }
  if (readU32(bytes, 0) != kMagic || readU16(bytes, 4) != kFormatVersion ||
      readU16(bytes, 6) != kSlotBytes || readU32(bytes, 8) == 0 ||
      readU32(bytes, kChecksumOffset) == 0 ||
      readU32(bytes, kChecksumOffset) != checksum(bytes, kChecksumOffset)) {
    return false;
  }

  snapshot = {};
  snapshot.valid = true;
  const uint16_t flags = readU16(bytes, 12);
  snapshot.in_progress = (flags & kFlagInProgress) != 0;
  snapshot.completed = (flags & kFlagCompleted) != 0;
  snapshot.owner = readU16(bytes, 14);
  snapshot.operation = readU16(bytes, 16);
  snapshot.write_sequence = readU32(bytes, 8);
  snapshot.boot_sequence = readU32(bytes, 20);
  snapshot.runtime_contract_id = readU64(bytes, 24);
  snapshot.call_sequence = readU32(bytes, 32);
  snapshot.started_uptime_ms = readU32(bytes, 36);
  snapshot.completed_uptime_ms = readU32(bytes, 40);
  snapshot.duration_us = readU32(bytes, 44);
  snapshot.result = static_cast<int32_t>(readU32(bytes, 48));
  return true;
}

bool RetainedCallLatch::commit() {
  if (!ready_) return false;
  current_.write_sequence = nextSequence(current_.write_sequence);
  const std::size_t target =
      active_slot_ < 0 ? 0u : static_cast<std::size_t>(1 - active_slot_);
  const std::size_t base = slotOffset(target);
  uint8_t bytes[kSlotBytes] = {};
  writeU32(bytes, 0, kMagic);
  writeU16(bytes, 4, kFormatVersion);
  writeU16(bytes, 6, static_cast<uint16_t>(kSlotBytes));
  writeU32(bytes, 8, current_.write_sequence);
  uint16_t flags = 0;
  if (current_.in_progress) flags |= kFlagInProgress;
  if (current_.completed) flags |= kFlagCompleted;
  writeU16(bytes, 12, flags);
  writeU16(bytes, 14, current_.owner);
  writeU16(bytes, 16, current_.operation);
  writeU32(bytes, 20, current_.boot_sequence);
  writeU64(bytes, 24, current_.runtime_contract_id);
  writeU32(bytes, 32, current_.call_sequence);
  writeU32(bytes, 36, current_.started_uptime_ms);
  writeU32(bytes, 40, current_.completed_uptime_ms);
  writeU32(bytes, 44, current_.duration_us);
  writeU32(bytes, 48, static_cast<uint32_t>(current_.result));

  for (std::size_t offset = 0; offset < sizeof(uint32_t); ++offset) {
    storage_[base + kChecksumOffset + offset] = 0;
  }
  barrier();
  flush(base + kChecksumOffset, sizeof(uint32_t));
  for (std::size_t offset = 0; offset < kChecksumOffset; ++offset) {
    storage_[base + offset] = bytes[offset];
  }
  barrier();
  flush(base, kChecksumOffset);
  writeU32(bytes, kChecksumOffset, checksum(bytes, kChecksumOffset));
  for (std::size_t offset = 0; offset < sizeof(uint32_t); ++offset) {
    storage_[base + kChecksumOffset + offset] =
        bytes[kChecksumOffset + offset];
  }
  barrier();
  flush(base + kChecksumOffset, sizeof(uint32_t));
  barrier();
  active_slot_ = static_cast<int8_t>(target);
  return true;
}

bool RetainedCallLatch::beginSession(uint64_t runtime_contract_id,
                                     uint32_t boot_sequence,
                                     uint32_t uptime_ms) {
  if (ready_ || storage_address_ == nullptr ||
      storage_bytes_ < kRequiredStorageBytes) {
    return false;
  }
  if (adapter_.prepare != nullptr &&
      !adapter_.prepare(adapter_.context, storage_address_,
                        kRequiredStorageBytes)) {
    return false;
  }

  RetainedCallSnapshot slot0{};
  RetainedCallSnapshot slot1{};
  const bool valid0 = readSlot(0, slot0);
  const bool valid1 = readSlot(1, slot1);
  if (valid0 || valid1) {
    if (!valid0 ||
        (valid1 && sequenceNewer(slot1.write_sequence, slot0.write_sequence))) {
      previous_ = slot1;
      active_slot_ = 1;
    } else {
      previous_ = slot0;
      active_slot_ = 0;
    }
    previous_.contract_changed =
        previous_.runtime_contract_id != runtime_contract_id;
  }

  current_ = {};
  current_.valid = true;
  current_.write_sequence = previous_.valid ? previous_.write_sequence : 0;
  current_.boot_sequence = boot_sequence;
  current_.runtime_contract_id = runtime_contract_id;
  current_.started_uptime_ms = uptime_ms;
  current_.completed_uptime_ms = uptime_ms;
  ready_ = true;
  return commit();
}

bool RetainedCallLatch::enter(uint16_t owner, uint16_t operation,
                              uint32_t call_sequence, uint32_t uptime_ms) {
  if (!ready_) return false;
  current_.owner = owner;
  current_.operation = operation;
  current_.call_sequence = call_sequence;
  current_.started_uptime_ms = uptime_ms;
  current_.completed_uptime_ms = 0;
  current_.duration_us = 0;
  current_.result = 0;
  current_.in_progress = true;
  current_.completed = false;
  return commit();
}

bool RetainedCallLatch::leave(int32_t result, uint32_t duration_us,
                              uint32_t uptime_ms) {
  if (!ready_ || !current_.in_progress) return false;
  current_.completed_uptime_ms = uptime_ms;
  current_.duration_us = duration_us;
  current_.result = result;
  current_.in_progress = false;
  current_.completed = true;
  return commit();
}

}  // namespace csm::board::diagnostics
