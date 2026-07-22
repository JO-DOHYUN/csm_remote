#include <array>
#include <cstdint>
#include <iostream>

#include "board/diagnostics/BootRecovery.h"

namespace {

using csm::board::diagnostics::BootRecovery;
using csm::board::diagnostics::BootRecoveryConfig;
using csm::board::diagnostics::BootRecoveryEvent;
using csm::board::diagnostics::BootStartInfo;
using csm::board::diagnostics::FirmwareIdentity;
using csm::board::diagnostics::RetainedStorageAdapter;

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition    \
                << '\n';                                                        \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

using Storage = std::array<uint8_t, BootRecovery::kRequiredStorageBytes>;

BootStartInfo boot(uint64_t build, uint64_t source, uint32_t cause = 0,
                   uint32_t uptime_ms = 0) {
  BootStartInfo info;
  info.firmware = FirmwareIdentity{build, source};
  info.reset_cause_bits = cause;
  info.uptime_ms = uptime_ms;
  return info;
}

uint32_t readU32(const Storage& storage, std::size_t offset) {
  return static_cast<uint32_t>(storage[offset]) |
         (static_cast<uint32_t>(storage[offset + 1]) << 8) |
         (static_cast<uint32_t>(storage[offset + 2]) << 16) |
         (static_cast<uint32_t>(storage[offset + 3]) << 24);
}

bool newer(uint32_t candidate, uint32_t reference) {
  return static_cast<int32_t>(candidate - reference) > 0;
}

std::size_t newestMetadataSlot(const Storage& storage) {
  const uint32_t generation0 =
      readU32(storage, BootRecovery::metadataSlotOffset(0) + 8);
  const uint32_t generation1 =
      readU32(storage, BootRecovery::metadataSlotOffset(1) + 8);
  return newer(generation1, generation0) ? 1u : 0u;
}

void corruptEventChecksum(Storage& storage, uint32_t sequence) {
  const std::size_t slot = (sequence - 1u) % BootRecovery::kEventSlotCount;
  storage[BootRecovery::eventSlotOffset(slot) +
          BootRecovery::kEventChecksumOffset] ^= 0xA5u;
}

struct AdapterProbe {
  uint32_t prepare_calls = 0;
  uint32_t commit_calls = 0;
  uint32_t barrier_calls = 0;
  bool prepare_result = true;
};

bool prepareStorage(void* context, void*, std::size_t bytes) {
  auto& probe = *static_cast<AdapterProbe*>(context);
  ++probe.prepare_calls;
  CHECK(bytes == BootRecovery::kRequiredStorageBytes);
  return probe.prepare_result;
}

void commitStorage(void* context, const void*, std::size_t bytes) {
  auto& probe = *static_cast<AdapterProbe*>(context);
  ++probe.commit_calls;
  CHECK(bytes == sizeof(uint32_t) ||
        bytes == BootRecovery::kMetadataChecksumOffset ||
        bytes == BootRecovery::kEventChecksumOffset);
}

void storageBarrier(void* context) {
  ++static_cast<AdapterProbe*>(context)->barrier_calls;
}

RetainedStorageAdapter adapterFor(AdapterProbe& probe) {
  RetainedStorageAdapter adapter;
  adapter.context = &probe;
  adapter.prepare = prepareStorage;
  adapter.commit = commitStorage;
  adapter.barrier = storageBarrier;
  return adapter;
}

void testStorageContractAndStableBoot() {
  static_assert(BootRecovery::kRequiredStorageBytes < 4096,
                "retained contract exceeds one backup SRAM bank");
  constexpr std::size_t required_storage = BootRecovery::kRequiredStorageBytes;
  static_assert(required_storage == 2304,
                "unexpected retained storage footprint");

  Storage storage{};
  AdapterProbe probe;
  BootRecoveryConfig config;
  config.stable_after_ms = 1000;
  config.early_reset_limit = 2;
  {
    BootRecovery recovery(storage.data(), storage.size(), config,
                          adapterFor(probe));
    CHECK(recovery.beginBoot(boot(0x101, 0xA0, 0x12)));
    auto snap = recovery.snapshot();
    CHECK(snap.ready);
    CHECK(!snap.previous_boot_valid);
    CHECK(snap.boot_sequence == 1);
    CHECK(snap.reset_cause_bits == 0x12);
    CHECK(snap.wifi_start_allowed);
    CHECK(!recovery.markStable(999));
    CHECK(recovery.recordProgress(7, 0x55, 900));
    CHECK(recovery.markStable(1000));
    CHECK(recovery.snapshot().current_boot_stable);
  }
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(0x101, 0xA0, 0x13)));
    const auto snap = recovery.snapshot();
    CHECK(snap.recovered_metadata);
    CHECK(snap.previous_boot_stable);
    CHECK(snap.previous_last_progress_id == 7);
    CHECK(snap.previous_last_progress_detail == 0x55);
    CHECK(snap.consecutive_early_resets == 0);
  }
  CHECK(probe.prepare_calls == 1);
  CHECK(probe.commit_calls > 0);
  CHECK(probe.barrier_calls > 0);

  AdapterProbe rejected_probe;
  rejected_probe.prepare_result = false;
  Storage rejected_storage{};
  BootRecovery rejected(rejected_storage.data(), rejected_storage.size(),
                        config, adapterFor(rejected_probe));
  CHECK(!rejected.beginBoot(boot(1, 1)));
  CHECK(!rejected.snapshot().ready);

  std::array<uint8_t, 128> too_small{};
  BootRecovery undersized(too_small.data(), too_small.size(), config);
  CHECK(!undersized.beginBoot(boot(1, 1)));
}

void testEarlyResetQuarantineAndOneShotRetry() {
  Storage storage{};
  BootRecoveryConfig config;
  config.stable_after_ms = 1000;
  config.early_reset_limit = 2;
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(1, 10)));
  }
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(1, 10)));
    CHECK(recovery.snapshot().consecutive_early_resets == 1);
    CHECK(recovery.shouldStartWifi());
  }
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(1, 10)));
    auto snap = recovery.snapshot();
    CHECK(snap.consecutive_early_resets == 2);
    CHECK(snap.wifi_quarantined);
    CHECK(!snap.wifi_start_allowed);
    CHECK(recovery.grantWifiRetryToken(100, 0x44));
    CHECK(!recovery.grantWifiRetryToken(101));
    CHECK(recovery.consumeWifiRetryToken(102));
    CHECK(!recovery.consumeWifiRetryToken(103));
    snap = recovery.snapshot();
    CHECK(snap.wifi_retry_active);
    CHECK(!snap.wifi_retry_token_available);
    CHECK(snap.wifi_start_allowed);
    // Deliberately reset without completing the attempt.
  }
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(1, 10)));
    auto snap = recovery.snapshot();
    CHECK(snap.wifi_quarantined);
    CHECK(!snap.wifi_retry_active);
    CHECK(!snap.wifi_retry_token_available);
    CHECK(!snap.wifi_start_allowed);
    CHECK(snap.wifi_retry_attempts == 1);
    CHECK(snap.wifi_retry_failures == 1);

    CHECK(recovery.grantWifiRetryToken(200));
    CHECK(recovery.consumeWifiRetryToken(201));
    CHECK(recovery.completeWifiRetry(false, 202, 9));
    CHECK(recovery.wifiQuarantined());
    CHECK(!recovery.shouldStartWifi());
    CHECK(recovery.snapshot().wifi_retry_failures == 2);

    CHECK(recovery.grantWifiRetryToken(300));
    CHECK(recovery.consumeWifiRetryToken(301));
    CHECK(recovery.completeWifiRetry(true, 302));
    snap = recovery.snapshot();
    CHECK(!snap.wifi_quarantined);
    CHECK(snap.wifi_start_allowed);
    CHECK(snap.wifi_retry_successes == 1);
    CHECK(snap.consecutive_early_resets == 0);
  }
}

void testFirmwareBuildAndSourceIdentityPolicy() {
  Storage storage{};
  BootRecoveryConfig config;
  config.stable_after_ms = 10;
  config.early_reset_limit = 2;
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(100, 500)));
    CHECK(recovery.markStable(10));
  }
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(101, 500)));
    const auto snap = recovery.snapshot();
    CHECK(snap.firmware_build_changed);
    CHECK(!snap.firmware_source_changed);
    CHECK(snap.previous_firmware_build_id == 100);
    CHECK(snap.previous_firmware_source_id == 500);
  }
  {
    // Artifact-only rebuild does not hide the preceding unstable boot.
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(102, 500)));
    const auto snap = recovery.snapshot();
    CHECK(snap.firmware_build_changed);
    CHECK(!snap.firmware_source_changed);
    CHECK(snap.consecutive_early_resets == 1);
  }
  {
    // A genuinely new source/config contract receives one clean trial.
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(200, 600)));
    const auto snap = recovery.snapshot();
    CHECK(snap.firmware_build_changed);
    CHECK(snap.firmware_source_changed);
    CHECK(snap.consecutive_early_resets == 0);
    CHECK(!snap.wifi_quarantined);
  }
}

void testTornMetadataFallsBack() {
  Storage committed_storage{};
  BootRecoveryConfig config;
  config.stable_after_ms = 1000;
  {
    BootRecovery recovery(committed_storage.data(), committed_storage.size(),
                          config);
    CHECK(recovery.beginBoot(boot(1, 1)));
    CHECK(recovery.recordProgress(11, 1, 100));
    CHECK(recovery.recordProgress(22, 2, 200));
  }
  const std::size_t newest = newestMetadataSlot(committed_storage);
  const std::size_t corrupt_offsets[] = {
      0, 8, 40, 52, 95, BootRecovery::kMetadataChecksumOffset,
      BootRecovery::kMetadataSlotBytes - 1u};
  for (const std::size_t corrupt_offset : corrupt_offsets) {
    Storage storage = committed_storage;
    storage[BootRecovery::metadataSlotOffset(newest) + corrupt_offset] ^=
        0x5Au;
    BootRecovery recovered(storage.data(), storage.size(), config);
    CHECK(recovered.beginBoot(boot(1, 1)));
    const auto snap = recovered.snapshot();
    CHECK(snap.recovered_metadata);
    CHECK(snap.recovered_from_fallback);
    CHECK(snap.corrupt_metadata_slots == 1);
    CHECK(snap.previous_last_progress_id == 11);
    CHECK(snap.previous_last_progress_detail == 1);
  }
}

void testTornRingEntryIsSkipped() {
  Storage storage{};
  BootRecoveryConfig config;
  config.stable_after_ms = 1000;
  uint32_t torn_sequence = 0;
  {
    BootRecovery recovery(storage.data(), storage.size(), config);
    CHECK(recovery.beginBoot(boot(1, 1)));
    CHECK(recovery.recordProgress(20, 30, 100));
    torn_sequence = recovery.snapshot().retained_event_sequence;
  }
  corruptEventChecksum(storage, torn_sequence);
  {
    BootRecovery recovered(storage.data(), storage.size(), config);
    CHECK(recovered.beginBoot(boot(1, 1)));
    const auto snap = recovered.snapshot();
    CHECK(snap.corrupt_event_slots == 1);
    BootRecoveryEvent events[BootRecovery::kEventSlotCount] = {};
    const std::size_t count =
        recovered.readRecentEvents(events, BootRecovery::kEventSlotCount);
    CHECK(count >= 2);
    for (std::size_t index = 0; index < count; ++index) {
      CHECK(events[index].sequence != torn_sequence);
      if (index != 0) {
        CHECK(newer(events[index].sequence, events[index - 1].sequence));
      }
    }
  }
}

void testEventRingIsBoundedAndOrdered() {
  Storage storage{};
  BootRecoveryConfig config;
  config.stable_after_ms = 100000;
  BootRecovery recovery(storage.data(), storage.size(), config);
  CHECK(recovery.beginBoot(boot(1, 1)));
  for (uint32_t index = 1; index <= 100; ++index) {
    CHECK(recovery.recordProgress(index, index * 2u, index));
  }
  BootRecoveryEvent events[BootRecovery::kEventSlotCount] = {};
  const std::size_t count =
      recovery.readRecentEvents(events, BootRecovery::kEventSlotCount);
  CHECK(count == BootRecovery::kEventSlotCount);
  CHECK(events[count - 1].sequence ==
        recovery.snapshot().retained_event_sequence);
  for (std::size_t index = 1; index < count; ++index) {
    CHECK(events[index].sequence == events[index - 1].sequence + 1u);
  }
  CHECK(recovery.snapshot().valid_retained_events ==
        BootRecovery::kEventSlotCount);
}

}  // namespace

int main() {
  testStorageContractAndStableBoot();
  testEarlyResetQuarantineAndOneShotRetry();
  testFirmwareBuildAndSourceIdentityPolicy();
  testTornMetadataFallsBack();
  testTornRingEntryIsSkipped();
  testEventRingIsBoundedAndOrdered();
  if (failures != 0) {
    std::cerr << failures << " boot recovery contract checks failed\n";
    return 1;
  }
  std::cout << "Boot recovery contract PASS\n";
  return 0;
}
