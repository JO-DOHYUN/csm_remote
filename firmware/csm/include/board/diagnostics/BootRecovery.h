#pragma once

#include <cstddef>
#include <cstdint>

namespace csm::board::diagnostics {

struct FirmwareIdentity {
  // build_id identifies the exact artifact. source_id identifies the source
  // tree. runtime_contract_id identifies the selected, resolved PlatformIO
  // runtime contract without epoch/dirty metadata. RuntimeSupervisor composes
  // the latter two into the source/config identity persisted by BootRecovery.
  uint64_t build_id = 0;
  uint64_t source_id = 0;
  uint64_t runtime_contract_id = 0;
};

struct BootStartInfo {
  FirmwareIdentity firmware{};
  uint32_t reset_cause_bits = 0;
  uint32_t uptime_ms = 0;
};

struct BootRecoveryConfig {
  // markStable() is rejected before this interval has elapsed. A boot which
  // resets without a committed stable marker is classified as an early reset.
  uint32_t stable_after_ms = 30000;
  uint32_t early_reset_limit = 2;
};

struct RetainedStorageAdapter {
  void* context = nullptr;

  // Optional board adapter. It may enable retained RAM and invalidate cache
  // before the first read. Returning false prevents all retained writes.
  bool (*prepare)(void* context, void* storage, std::size_t bytes) = nullptr;

  // Optional cache-clean / persistence callback. BootRecovery invokes it once
  // for the record body and again after writing the checksum last.
  void (*commit)(void* context, const void* address, std::size_t bytes) = nullptr;

  // Optional DMB/DSB adapter used around checksum publication.
  void (*barrier)(void* context) = nullptr;
};

enum class BootRecoveryEventType : uint16_t {
  BootStarted = 1,
  Progress = 2,
  Stable = 3,
  WifiQuarantined = 4,
  WifiRetryGranted = 5,
  WifiRetryConsumed = 6,
  WifiRetrySucceeded = 7,
  WifiRetryFailed = 8,
  FirmwareIdentityChanged = 9,
};

struct BootRecoveryEvent {
  uint32_t sequence = 0;
  uint32_t boot_sequence = 0;
  uint32_t uptime_ms = 0;
  BootRecoveryEventType type = BootRecoveryEventType::BootStarted;
  uint16_t code = 0;
  uint32_t value = 0;
  uint32_t firmware_identity_tag = 0;
};

// Compact, operator-facing state. This deliberately contains only scalars so
// it can be projected into BOARD_HEALTH/CAPABILITY without exposing storage
// layout or making the diagnostic stream the source of truth.
struct ProductRecoverySnapshot {
  bool ready = false;
  bool recovered_metadata = false;
  bool recovered_from_fallback = false;
  bool previous_boot_valid = false;
  bool previous_boot_stable = false;
  bool firmware_build_changed = false;
  bool firmware_source_changed = false;
  bool current_boot_stable = false;
  bool wifi_quarantined = false;
  bool wifi_retry_token_available = false;
  bool wifi_retry_active = false;
  bool wifi_start_allowed = false;

  uint64_t firmware_build_id = 0;
  uint64_t firmware_source_id = 0;
  uint64_t previous_firmware_build_id = 0;
  uint64_t previous_firmware_source_id = 0;

  uint32_t metadata_generation = 0;
  uint32_t boot_sequence = 0;
  uint32_t reset_cause_bits = 0;
  uint32_t boot_started_uptime_ms = 0;
  uint32_t last_progress_id = 0;
  uint32_t last_progress_detail = 0;
  uint32_t last_progress_uptime_ms = 0;
  uint32_t stable_uptime_ms = 0;
  uint32_t consecutive_early_resets = 0;
  uint32_t early_reset_total = 0;
  uint32_t wifi_quarantine_total = 0;
  uint32_t wifi_retry_grants = 0;
  uint32_t wifi_retry_attempts = 0;
  uint32_t wifi_retry_failures = 0;
  uint32_t wifi_retry_successes = 0;
  uint32_t retained_event_sequence = 0;
  uint32_t valid_retained_events = 0;
  uint32_t corrupt_metadata_slots = 0;
  uint32_t corrupt_event_slots = 0;

  uint32_t previous_boot_sequence = 0;
  uint32_t previous_reset_cause_bits = 0;
  uint32_t previous_last_progress_id = 0;
  uint32_t previous_last_progress_detail = 0;
  uint32_t previous_last_progress_uptime_ms = 0;
};

class BootRecovery {
 public:
  static constexpr std::size_t kMetadataSlotBytes = 128;
  static constexpr std::size_t kMetadataSlotCount = 2;
  static constexpr std::size_t kEventSlotBytes = 32;
  static constexpr std::size_t kEventSlotCount = 64;
  static constexpr std::size_t kMetadataChecksumOffset = 124;
  static constexpr std::size_t kEventChecksumOffset = 28;
  static constexpr std::size_t kRequiredStorageBytes =
      kMetadataSlotBytes * kMetadataSlotCount +
      kEventSlotBytes * kEventSlotCount;

  static_assert(kRequiredStorageBytes < 4096,
                "boot recovery retained storage must remain below 4 KiB");

  BootRecovery(void* retained_storage, std::size_t retained_storage_bytes,
               BootRecoveryConfig config = {},
               RetainedStorageAdapter adapter = {});

  // Recovers the newest valid metadata slot, classifies the previous boot, and
  // commits a new BootStarted marker. Call exactly once before risky drivers.
  bool beginBoot(const BootStartInfo& info);

  bool recordProgress(uint32_t progress_id, uint32_t detail,
                      uint32_t uptime_ms);
  bool markStable(uint32_t uptime_ms);

  // Retry is deliberately two-step. An explicit operator/service action grants
  // one token; consuming it is committed before Wi-Fi is touched. If the board
  // resets with retry_active set, the next boot returns to quarantine and the
  // token is not restored.
  bool grantWifiRetryToken(uint32_t uptime_ms, uint32_t reason = 0);
  bool consumeWifiRetryToken(uint32_t uptime_ms);
  bool completeWifiRetry(bool succeeded, uint32_t uptime_ms,
                         uint32_t detail = 0);

  bool shouldStartWifi() const;
  bool wifiQuarantined() const;
  ProductRecoverySnapshot snapshot() const;

  // Returns the newest available events in chronological order. Invalid/torn
  // ring entries are skipped; the ring never allocates or grows.
  std::size_t readRecentEvents(BootRecoveryEvent* output,
                               std::size_t capacity) const;

  static constexpr std::size_t metadataSlotOffset(std::size_t index) {
    return index * kMetadataSlotBytes;
  }
  static constexpr std::size_t eventSlotOffset(std::size_t index) {
    return kMetadataSlotBytes * kMetadataSlotCount + index * kEventSlotBytes;
  }

 private:
  struct State {
    uint32_t generation = 0;
    uint64_t build_id = 0;
    uint64_t source_id = 0;
    uint32_t boot_sequence = 0;
    uint32_t reset_cause_bits = 0;
    uint32_t flags = 0;
    uint32_t consecutive_early_resets = 0;
    uint32_t early_reset_total = 0;
    uint32_t last_progress_id = 0;
    uint32_t last_progress_detail = 0;
    uint32_t boot_started_uptime_ms = 0;
    uint32_t last_progress_uptime_ms = 0;
    uint32_t stable_uptime_ms = 0;
    uint32_t event_sequence = 0;
    uint32_t wifi_retry_grants = 0;
    uint32_t wifi_retry_attempts = 0;
    uint32_t wifi_retry_failures = 0;
    uint32_t wifi_retry_successes = 0;
    uint32_t wifi_quarantine_total = 0;
  };

  bool commitMetadata();
  bool appendEvent(BootRecoveryEventType type, uint16_t code, uint32_t value,
                   uint32_t uptime_ms);
  bool readMetadataSlot(std::size_t index, State& state) const;
  bool readEventSlot(std::size_t index, BootRecoveryEvent& event) const;
  void scanEventRing();
  void flush(std::size_t offset, std::size_t bytes) const;
  void barrier() const;

  void* storage_address_ = nullptr;
  volatile uint8_t* storage_ = nullptr;
  std::size_t storage_bytes_ = 0;
  BootRecoveryConfig config_{};
  RetainedStorageAdapter adapter_{};
  State state_{};
  State previous_state_{};
  int8_t active_metadata_slot_ = -1;
  bool ready_ = false;
  bool recovered_metadata_ = false;
  bool recovered_from_fallback_ = false;
  bool previous_boot_valid_ = false;
  bool build_changed_ = false;
  bool source_changed_ = false;
  uint32_t corrupt_metadata_slots_ = 0;
  uint32_t corrupt_event_slots_ = 0;
  uint32_t valid_event_count_ = 0;
};

}  // namespace csm::board::diagnostics
