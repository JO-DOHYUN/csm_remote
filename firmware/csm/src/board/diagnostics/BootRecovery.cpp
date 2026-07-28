#include "board/diagnostics/BootRecovery.h"

namespace csm::board::diagnostics {
namespace {

constexpr uint32_t kMetadataMagic = 0x4252434Du;  // "BRCM"
constexpr uint32_t kEventMagic = 0x42524345u;     // "BRCE"
constexpr uint16_t kFormatVersion = 1;
constexpr uint32_t kChecksumSeed = 2166136261u;

constexpr uint32_t kFlagBootStarted = 1u << 0;
constexpr uint32_t kFlagBootStable = 1u << 1;
constexpr uint32_t kFlagWifiQuarantined = 1u << 2;
constexpr uint32_t kFlagWifiRetryToken = 1u << 3;
constexpr uint32_t kFlagWifiRetryActive = 1u << 4;

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
  // Zero is the deliberately invalid in-progress marker.
  return value == 0 ? 0xFFFFFFFFu : value;
}

bool sequenceNewer(uint32_t candidate, uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

bool anyNonZero(const volatile uint8_t* bytes, std::size_t length) {
  for (std::size_t index = 0; index < length; ++index) {
    if (bytes[index] != 0) return true;
  }
  return false;
}

uint32_t nextSequence(uint32_t sequence) {
  ++sequence;
  return sequence == 0 ? 1u : sequence;
}

uint32_t identityTag(uint64_t build_id, uint64_t source_id) {
  uint8_t bytes[16] = {};
  writeU64(bytes, 0, build_id);
  writeU64(bytes, 8, source_id);
  return checksum(bytes, sizeof(bytes));
}

}  // namespace

BootRecovery::BootRecovery(void* retained_storage,
                           std::size_t retained_storage_bytes,
                           BootRecoveryConfig config,
                           RetainedStorageAdapter adapter)
    : storage_address_(retained_storage),
      storage_(static_cast<volatile uint8_t*>(retained_storage)),
      storage_bytes_(retained_storage_bytes),
      config_(config),
      adapter_(adapter) {
  if (config_.early_reset_limit == 0) config_.early_reset_limit = 1;
}

void BootRecovery::barrier() const {
  if (adapter_.barrier != nullptr) adapter_.barrier(adapter_.context);
}

void BootRecovery::flush(std::size_t offset, std::size_t bytes) const {
  if (adapter_.commit == nullptr || storage_address_ == nullptr || bytes == 0) {
    return;
  }
  const auto* base = static_cast<const uint8_t*>(storage_address_);
  adapter_.commit(adapter_.context, base + offset, bytes);
}

bool BootRecovery::readMetadataSlot(std::size_t index, State& state) const {
  if (index >= kMetadataSlotCount) return false;
  uint8_t bytes[kMetadataSlotBytes] = {};
  const std::size_t base = metadataSlotOffset(index);
  for (std::size_t offset = 0; offset < sizeof(bytes); ++offset) {
    bytes[offset] = storage_[base + offset];
  }
  if (readU32(bytes, 0) != kMetadataMagic ||
      readU16(bytes, 4) != kFormatVersion ||
      readU16(bytes, 6) != kMetadataSlotBytes ||
      readU32(bytes, 12) != kRequiredStorageBytes ||
      readU32(bytes, kMetadataChecksumOffset) == 0 ||
      readU32(bytes, kMetadataChecksumOffset) !=
          checksum(bytes, kMetadataChecksumOffset)) {
    return false;
  }

  state.generation = readU32(bytes, 8);
  state.build_id = readU64(bytes, 16);
  state.source_id = readU64(bytes, 24);
  state.boot_sequence = readU32(bytes, 32);
  state.reset_cause_bits = readU32(bytes, 36);
  state.flags = readU32(bytes, 40);
  state.consecutive_early_resets = readU32(bytes, 44);
  state.early_reset_total = readU32(bytes, 48);
  state.last_progress_id = readU32(bytes, 52);
  state.last_progress_detail = readU32(bytes, 56);
  state.boot_started_uptime_ms = readU32(bytes, 60);
  state.last_progress_uptime_ms = readU32(bytes, 64);
  state.stable_uptime_ms = readU32(bytes, 68);
  state.event_sequence = readU32(bytes, 72);
  state.wifi_retry_grants = readU32(bytes, 76);
  state.wifi_retry_attempts = readU32(bytes, 80);
  state.wifi_retry_failures = readU32(bytes, 84);
  state.wifi_retry_successes = readU32(bytes, 88);
  state.wifi_quarantine_total = readU32(bytes, 92);
  return true;
}

bool BootRecovery::commitMetadata() {
  if (!ready_ || active_metadata_slot_ < -1) return false;
  state_.generation = nextSequence(state_.generation);
  const std::size_t target =
      active_metadata_slot_ < 0 ? 0u : static_cast<std::size_t>(1 - active_metadata_slot_);
  const std::size_t base = metadataSlotOffset(target);
  uint8_t bytes[kMetadataSlotBytes] = {};
  writeU32(bytes, 0, kMetadataMagic);
  writeU16(bytes, 4, kFormatVersion);
  writeU16(bytes, 6, static_cast<uint16_t>(kMetadataSlotBytes));
  writeU32(bytes, 8, state_.generation);
  writeU32(bytes, 12, static_cast<uint32_t>(kRequiredStorageBytes));
  writeU64(bytes, 16, state_.build_id);
  writeU64(bytes, 24, state_.source_id);
  writeU32(bytes, 32, state_.boot_sequence);
  writeU32(bytes, 36, state_.reset_cause_bits);
  writeU32(bytes, 40, state_.flags);
  writeU32(bytes, 44, state_.consecutive_early_resets);
  writeU32(bytes, 48, state_.early_reset_total);
  writeU32(bytes, 52, state_.last_progress_id);
  writeU32(bytes, 56, state_.last_progress_detail);
  writeU32(bytes, 60, state_.boot_started_uptime_ms);
  writeU32(bytes, 64, state_.last_progress_uptime_ms);
  writeU32(bytes, 68, state_.stable_uptime_ms);
  writeU32(bytes, 72, state_.event_sequence);
  writeU32(bytes, 76, state_.wifi_retry_grants);
  writeU32(bytes, 80, state_.wifi_retry_attempts);
  writeU32(bytes, 84, state_.wifi_retry_failures);
  writeU32(bytes, 88, state_.wifi_retry_successes);
  writeU32(bytes, 92, state_.wifi_quarantine_total);

  // Invalidate the target first. The other metadata slot remains the recovery
  // point until this slot's new checksum is published and cleaned.
  for (std::size_t offset = 0; offset < sizeof(uint32_t); ++offset) {
    storage_[base + kMetadataChecksumOffset + offset] = 0;
  }
  barrier();
  flush(base + kMetadataChecksumOffset, sizeof(uint32_t));

  for (std::size_t offset = 0; offset < kMetadataChecksumOffset; ++offset) {
    storage_[base + offset] = bytes[offset];
  }
  barrier();
  flush(base, kMetadataChecksumOffset);

  const uint32_t final_checksum = checksum(bytes, kMetadataChecksumOffset);
  writeU32(bytes, kMetadataChecksumOffset, final_checksum);
  for (std::size_t offset = 0; offset < sizeof(uint32_t); ++offset) {
    storage_[base + kMetadataChecksumOffset + offset] =
        bytes[kMetadataChecksumOffset + offset];
  }
  barrier();
  flush(base + kMetadataChecksumOffset, sizeof(uint32_t));
  barrier();
  active_metadata_slot_ = static_cast<int8_t>(target);
  return true;
}

bool BootRecovery::readEventSlot(std::size_t index,
                                 BootRecoveryEvent& event) const {
  if (index >= kEventSlotCount) return false;
  uint8_t bytes[kEventSlotBytes] = {};
  const std::size_t base = eventSlotOffset(index);
  for (std::size_t offset = 0; offset < sizeof(bytes); ++offset) {
    bytes[offset] = storage_[base + offset];
  }
  if (readU32(bytes, 0) != kEventMagic || readU32(bytes, 4) == 0 ||
      readU32(bytes, kEventChecksumOffset) == 0 ||
      readU32(bytes, kEventChecksumOffset) !=
          checksum(bytes, kEventChecksumOffset)) {
    return false;
  }
  event.sequence = readU32(bytes, 4);
  event.boot_sequence = readU32(bytes, 8);
  event.uptime_ms = readU32(bytes, 12);
  event.type = static_cast<BootRecoveryEventType>(readU16(bytes, 16));
  event.code = readU16(bytes, 18);
  event.value = readU32(bytes, 20);
  event.firmware_identity_tag = readU32(bytes, 24);
  return true;
}

void BootRecovery::scanEventRing() {
  corrupt_event_slots_ = 0;
  valid_event_count_ = 0;
  uint32_t newest_sequence = state_.event_sequence;
  bool have_valid = false;
  for (std::size_t index = 0; index < kEventSlotCount; ++index) {
    BootRecoveryEvent event{};
    if (readEventSlot(index, event)) {
      ++valid_event_count_;
      if (!have_valid || sequenceNewer(event.sequence, newest_sequence)) {
        newest_sequence = event.sequence;
      }
      have_valid = true;
    } else {
      const std::size_t base = eventSlotOffset(index);
      if (anyNonZero(storage_ + base, kEventSlotBytes)) {
        ++corrupt_event_slots_;
      }
    }
  }
  if (have_valid &&
      (state_.event_sequence == 0 ||
       sequenceNewer(newest_sequence, state_.event_sequence))) {
    state_.event_sequence = newest_sequence;
  }
}

bool BootRecovery::appendEvent(BootRecoveryEventType type, uint16_t code,
                               uint32_t value, uint32_t uptime_ms) {
  if (!ready_) return false;
  const uint32_t sequence = nextSequence(state_.event_sequence);
  const std::size_t slot = (sequence - 1u) % kEventSlotCount;
  const std::size_t base = eventSlotOffset(slot);
  BootRecoveryEvent replaced_event{};
  const bool replaced_valid = readEventSlot(slot, replaced_event);
  const bool replaced_corrupt =
      !replaced_valid && anyNonZero(storage_ + base, kEventSlotBytes);
  uint8_t bytes[kEventSlotBytes] = {};
  writeU32(bytes, 0, kEventMagic);
  writeU32(bytes, 4, sequence);
  writeU32(bytes, 8, state_.boot_sequence);
  writeU32(bytes, 12, uptime_ms);
  writeU16(bytes, 16, static_cast<uint16_t>(type));
  writeU16(bytes, 18, code);
  writeU32(bytes, 20, value);
  writeU32(bytes, 24, identityTag(state_.build_id, state_.source_id));

  for (std::size_t offset = 0; offset < sizeof(uint32_t); ++offset) {
    storage_[base + kEventChecksumOffset + offset] = 0;
  }
  barrier();
  flush(base + kEventChecksumOffset, sizeof(uint32_t));
  for (std::size_t offset = 0; offset < kEventChecksumOffset; ++offset) {
    storage_[base + offset] = bytes[offset];
  }
  barrier();
  flush(base, kEventChecksumOffset);
  writeU32(bytes, kEventChecksumOffset,
           checksum(bytes, kEventChecksumOffset));
  for (std::size_t offset = 0; offset < sizeof(uint32_t); ++offset) {
    storage_[base + kEventChecksumOffset + offset] =
        bytes[kEventChecksumOffset + offset];
  }
  barrier();
  flush(base + kEventChecksumOffset, sizeof(uint32_t));
  barrier();

  state_.event_sequence = sequence;
  if (!replaced_valid && valid_event_count_ < kEventSlotCount) {
    ++valid_event_count_;
  }
  if (replaced_corrupt && corrupt_event_slots_ != 0) {
    --corrupt_event_slots_;
  }
  return commitMetadata();
}

bool BootRecovery::beginBoot(const BootStartInfo& info) {
  if (ready_ || storage_address_ == nullptr ||
      storage_bytes_ < kRequiredStorageBytes) {
    return false;
  }
  if (adapter_.prepare != nullptr &&
      !adapter_.prepare(adapter_.context, storage_address_,
                        kRequiredStorageBytes)) {
    return false;
  }

  State slot0{};
  State slot1{};
  const bool valid0 = readMetadataSlot(0, slot0);
  const bool valid1 = readMetadataSlot(1, slot1);
  if (!valid0 && anyNonZero(storage_, kMetadataSlotBytes)) {
    ++corrupt_metadata_slots_;
  }
  if (!valid1 &&
      anyNonZero(storage_ + metadataSlotOffset(1), kMetadataSlotBytes)) {
    ++corrupt_metadata_slots_;
  }

  if (valid0 || valid1) {
    std::size_t selected = 0;
    if (!valid0 ||
        (valid1 && sequenceNewer(slot1.generation, slot0.generation))) {
      selected = 1;
    }
    state_ = selected == 0 ? slot0 : slot1;
    previous_state_ = state_;
    active_metadata_slot_ = static_cast<int8_t>(selected);
    recovered_metadata_ = true;
    previous_boot_valid_ = true;
    recovered_from_fallback_ = (valid0 != valid1) &&
                               corrupt_metadata_slots_ != 0;
  }

  ready_ = true;
  scanEventRing();

  if (previous_boot_valid_) {
    build_changed_ = previous_state_.build_id != info.firmware.build_id;
    source_changed_ = previous_state_.source_id != info.firmware.source_id;
  }

  // Retain reset/call evidence, but never turn an inferred association into a
  // product transport shutdown. Older firmware may have persisted quarantine
  // or retry bits; retire them on the first boot of this policy.
  state_.flags &= ~(kFlagWifiQuarantined | kFlagWifiRetryToken |
                    kFlagWifiRetryActive);
  if (source_changed_) {
    // A new source/config contract gets one clean trial. An artifact-only rebuild
    // does not erase reset evidence.
    state_.consecutive_early_resets = 0;
  } else if (previous_boot_valid_) {
    if ((state_.flags & kFlagBootStarted) != 0 &&
        (state_.flags & kFlagBootStable) == 0) {
      state_.consecutive_early_resets =
          nextSequence(state_.consecutive_early_resets);
      state_.early_reset_total = nextSequence(state_.early_reset_total);
    } else if ((state_.flags & kFlagBootStable) != 0) {
      state_.consecutive_early_resets = 0;
    }

  }

  state_.build_id = info.firmware.build_id;
  state_.source_id = info.firmware.source_id;
  state_.boot_sequence = nextSequence(state_.boot_sequence);
  state_.reset_cause_bits = info.reset_cause_bits;
  state_.flags &= ~(kFlagBootStable | kFlagWifiRetryActive);
  state_.flags |= kFlagBootStarted;
  state_.boot_started_uptime_ms = info.uptime_ms;
  state_.last_progress_id = 0;
  state_.last_progress_detail = 0;
  state_.last_progress_uptime_ms = info.uptime_ms;
  state_.stable_uptime_ms = 0;

  const uint16_t identity_flags =
      static_cast<uint16_t>((build_changed_ ? 1u : 0u) |
                            (source_changed_ ? 2u : 0u));
  if (!appendEvent(BootRecoveryEventType::BootStarted, identity_flags,
                   info.reset_cause_bits, info.uptime_ms)) {
    ready_ = false;
    return false;
  }
  if (source_changed_) {
    appendEvent(BootRecoveryEventType::FirmwareIdentityChanged,
                identity_flags,
                static_cast<uint32_t>(previous_state_.source_id),
                info.uptime_ms);
  }
  return true;
}

bool BootRecovery::recordProgress(uint32_t progress_id, uint32_t detail,
                                  uint32_t uptime_ms) {
  if (!ready_) return false;
  state_.last_progress_id = progress_id;
  state_.last_progress_detail = detail;
  state_.last_progress_uptime_ms = uptime_ms;
  return appendEvent(BootRecoveryEventType::Progress,
                     static_cast<uint16_t>(progress_id), detail, uptime_ms);
}

bool BootRecovery::markStable(uint32_t uptime_ms) {
  if (!ready_ ||
      static_cast<uint32_t>(uptime_ms - state_.boot_started_uptime_ms) <
          config_.stable_after_ms) {
    return false;
  }
  if ((state_.flags & kFlagBootStable) != 0) return true;
  state_.flags |= kFlagBootStable;
  state_.stable_uptime_ms = uptime_ms;
  state_.consecutive_early_resets = 0;
  return appendEvent(BootRecoveryEventType::Stable, 0,
                     state_.last_progress_id, uptime_ms);
}

ProductRecoverySnapshot BootRecovery::snapshot() const {
  ProductRecoverySnapshot result{};
  result.ready = ready_;
  result.recovered_metadata = recovered_metadata_;
  result.recovered_from_fallback = recovered_from_fallback_;
  result.previous_boot_valid = previous_boot_valid_;
  result.previous_boot_stable =
      previous_boot_valid_ && (previous_state_.flags & kFlagBootStable) != 0;
  result.firmware_build_changed = build_changed_;
  result.firmware_source_changed = source_changed_;
  result.current_boot_stable = (state_.flags & kFlagBootStable) != 0;
  result.wifi_quarantined = false;
  result.wifi_retry_token_available = false;
  result.wifi_retry_active = false;
  result.wifi_start_allowed = ready_;
  result.firmware_build_id = state_.build_id;
  result.firmware_source_id = state_.source_id;
  result.previous_firmware_build_id = previous_state_.build_id;
  result.previous_firmware_source_id = previous_state_.source_id;
  result.metadata_generation = state_.generation;
  result.boot_sequence = state_.boot_sequence;
  result.reset_cause_bits = state_.reset_cause_bits;
  result.boot_started_uptime_ms = state_.boot_started_uptime_ms;
  result.last_progress_id = state_.last_progress_id;
  result.last_progress_detail = state_.last_progress_detail;
  result.last_progress_uptime_ms = state_.last_progress_uptime_ms;
  result.stable_uptime_ms = state_.stable_uptime_ms;
  result.consecutive_early_resets = state_.consecutive_early_resets;
  result.early_reset_total = state_.early_reset_total;
  result.wifi_quarantine_total = state_.wifi_quarantine_total;
  result.wifi_retry_grants = state_.wifi_retry_grants;
  result.wifi_retry_attempts = state_.wifi_retry_attempts;
  result.wifi_retry_failures = state_.wifi_retry_failures;
  result.wifi_retry_successes = state_.wifi_retry_successes;
  result.retained_event_sequence = state_.event_sequence;
  result.valid_retained_events = valid_event_count_;
  result.corrupt_metadata_slots = corrupt_metadata_slots_;
  result.corrupt_event_slots = corrupt_event_slots_;
  result.previous_boot_sequence = previous_state_.boot_sequence;
  result.previous_reset_cause_bits = previous_state_.reset_cause_bits;
  result.previous_last_progress_id = previous_state_.last_progress_id;
  result.previous_last_progress_detail = previous_state_.last_progress_detail;
  result.previous_last_progress_uptime_ms =
      previous_state_.last_progress_uptime_ms;
  return result;
}

std::size_t BootRecovery::readRecentEvents(BootRecoveryEvent* output,
                                           std::size_t capacity) const {
  if (!ready_ || output == nullptr || capacity == 0 ||
      state_.event_sequence == 0) {
    return 0;
  }
  std::size_t found = 0;
  uint32_t sequence = state_.event_sequence;
  for (std::size_t step = 0;
       step < kEventSlotCount && sequence != 0 && found < capacity; ++step) {
    const std::size_t slot = (sequence - 1u) % kEventSlotCount;
    BootRecoveryEvent event{};
    if (readEventSlot(slot, event) && event.sequence == sequence) {
      output[found++] = event;
    }
    --sequence;
  }
  // The scan is newest-first; reverse in place for chronological consumption.
  for (std::size_t index = 0; index < found / 2u; ++index) {
    const std::size_t opposite = found - index - 1u;
    const BootRecoveryEvent temporary = output[index];
    output[index] = output[opposite];
    output[opposite] = temporary;
  }
  return found;
}

}  // namespace csm::board::diagnostics
