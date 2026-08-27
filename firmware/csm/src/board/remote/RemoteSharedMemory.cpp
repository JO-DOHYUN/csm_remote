#include "board/remote/RemoteSharedMemory.h"

#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <atomic>
#define __DMB() std::atomic_thread_fence(std::memory_order_seq_cst)
#define __DSB() std::atomic_thread_fence(std::memory_order_seq_cst)
#endif

namespace csm::board::remote {
namespace {

constexpr uint16_t kDetailBadHeader = 1;
constexpr uint16_t kDetailTorn = 2;
constexpr uint16_t kDetailChecksum = 3;

uint32_t fnv1a(const uint8_t* data, size_t size) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}

template <typename T>
uint32_t objectChecksum(const T& value) {
  T copy = value;
  copy.sequence_begin = 0;
  copy.checksum = 0;
  copy.sequence_end = 0;
  return fnv1a(reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

void cleanM7Cache(void* address, size_t size) {
#if defined(CORE_CM7)
  const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~uintptr_t(31u);
  const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + 31u) & ~uintptr_t(31u);
  SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(start),
                          static_cast<int32_t>(end - start));
  __DSB();
#else
  (void)address;
  (void)size;
  __DMB();
#endif
}

void invalidateM7Cache(void* address, size_t size) {
#if defined(CORE_CM7)
  const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~uintptr_t(31u);
  const uintptr_t end = (reinterpret_cast<uintptr_t>(address) + size + 31u) & ~uintptr_t(31u);
  SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(start),
                               static_cast<int32_t>(end - start));
  __DSB();
#else
  (void)address;
  (void)size;
  __DMB();
#endif
}

bool hasValidHeader(const RemoteSharedMemoryRegion& region) {
  return region.header.magic == kRemoteSharedMemoryMagic &&
         region.header.version == kRemoteSharedMemoryVersion &&
         region.header.region_size == sizeof(RemoteSharedMemoryRegion);
}

uint32_t nextEvenSequence(uint32_t current) {
  uint32_t next = (current & ~1u) + 2u;
  return next == 0 ? 2u : next;
}

}  // namespace

RemoteSharedMemoryRegion* remoteSharedMemoryRegion() {
#if defined(CSM_REMOTE_SHARED_MEMORY_TEST)
  static RemoteSharedMemoryRegion test_region;
  return &test_region;
#else
  return reinterpret_cast<RemoteSharedMemoryRegion*>(kRemoteSharedMemoryAddress);
#endif
}

void initializeRemoteSharedMemoryForM7(uint32_t m7_boot_id) {
  RemoteSharedMemoryRegion* region = remoteSharedMemoryRegion();
  memset(region, 0, sizeof(*region));
  region->header.magic = kRemoteSharedMemoryMagic;
  region->header.version = kRemoteSharedMemoryVersion;
  region->header.region_size = sizeof(RemoteSharedMemoryRegion);
  region->header.m7_boot_id = m7_boot_id;
  cleanM7Cache(region, sizeof(*region));
}

uint32_t initializeRemoteSharedMemoryForM4() {
  RemoteSharedMemoryRegion* region = remoteSharedMemoryRegion();
  if (!hasValidHeader(*region) || region->header.m7_boot_id == 0u) return 0u;
  uint32_t boot_id = region->m4_to_m7.m4_boot_id + 1u;
  if (boot_id == 0) boot_id = 1;
  region->m4_to_m7.m4_boot_id = boot_id;
  __DMB();
  return boot_id;
}

bool publishRemoteSharedSample(uint32_t m4_boot_id,
                               uint32_t heartbeat_sequence,
                               const M4RemoteMailboxFrame& mailbox,
                               const RemoteFrontendDiagnostics& diagnostics) {
  RemoteSharedMemoryRegion* region = remoteSharedMemoryRegion();
  if (!hasValidHeader(*region)) return false;

  RemoteM4ToM7Channel& channel = region->m4_to_m7;
  const uint32_t sequence = nextEvenSequence(channel.sample_sequence);
  const uint32_t slot_index = (channel.active_sample_slot ^ 1u) & 1u;
  RemoteSharedSampleSlot slot;
  slot.sequence_begin = sequence | 1u;
  slot.m4_boot_id = m4_boot_id;
  slot.heartbeat_sequence = heartbeat_sequence;
  slot.mailbox = mailbox;
  slot.diagnostics = diagnostics;
  slot.sequence_end = sequence;
  slot.checksum = objectChecksum(slot);

  channel.samples[slot_index] = slot;
  __DMB();
  channel.samples[slot_index].sequence_begin = sequence;
  __DMB();
  channel.active_sample_slot = slot_index;
  channel.sample_sequence = sequence;
  channel.m4_boot_id = m4_boot_id;
  __DMB();
  return true;
}

RemoteSharedSampleReadResult readRemoteSharedSample(uint32_t last_sequence) {
  RemoteSharedSampleReadResult result;
  RemoteSharedMemoryRegion* region = remoteSharedMemoryRegion();
  invalidateM7Cache(&region->header, sizeof(region->header));
  if (!hasValidHeader(*region)) {
    result.detail = kDetailBadHeader;
    return result;
  }

  RemoteM4ToM7Channel& channel = region->m4_to_m7;
  invalidateM7Cache(&channel, sizeof(channel));
  const uint32_t sequence = channel.sample_sequence;
  const uint32_t slot_index = channel.active_sample_slot & 1u;
  if (sequence == 0) return result;
  result.shared_sequence = sequence;
  if (sequence == last_sequence) {
    result.accepted = true;
    return result;
  }

  RemoteSharedSampleSlot slot = channel.samples[slot_index];
  invalidateM7Cache(&channel, sizeof(channel));
  const uint32_t sequence_after = channel.sample_sequence;
  const uint32_t slot_after = channel.active_sample_slot & 1u;
  if ((slot.sequence_begin & 1u) != 0 ||
      slot.sequence_begin != slot.sequence_end ||
      slot.sequence_begin != sequence ||
      sequence_after != sequence || slot_after != slot_index) {
    result.detail = kDetailTorn;
    return result;
  }
  if (slot.checksum != objectChecksum(slot)) {
    result.detail = kDetailChecksum;
    return result;
  }

  result.accepted = true;
  result.new_sample = true;
  result.slot = slot;
  return result;
}

bool publishRemoteTelemetry(const RemoteTelemetrySlot& telemetry) {
  RemoteSharedMemoryRegion* region = remoteSharedMemoryRegion();
  invalidateM7Cache(&region->header, sizeof(region->header));
  if (!hasValidHeader(*region)) return false;

  RemoteM7ToM4Channel& channel = region->m7_to_m4;
  const uint32_t sequence = nextEvenSequence(channel.telemetry_sequence);
  const uint32_t slot_index = (channel.active_telemetry_slot ^ 1u) & 1u;
  RemoteTelemetrySlot slot = telemetry;
  slot.sequence_begin = sequence | 1u;
  slot.magic = kRemoteSharedMemoryMagic;
  slot.version = kRemoteSharedMemoryVersion;
  slot.frame_size = sizeof(RemoteTelemetrySlot);
  slot.sequence_end = sequence;
  slot.checksum = objectChecksum(slot);
  channel.telemetry[slot_index] = slot;
  channel.telemetry[slot_index].sequence_begin = sequence;
  channel.active_telemetry_slot = slot_index;
  channel.telemetry_sequence = sequence;
  cleanM7Cache(&channel, sizeof(channel));
  return true;
}

bool readRemoteTelemetry(uint32_t last_sequence,
                         RemoteTelemetrySlot* telemetry,
                         uint32_t* published_sequence) {
  if (telemetry == nullptr || published_sequence == nullptr) return false;
  RemoteSharedMemoryRegion* region = remoteSharedMemoryRegion();
  if (!hasValidHeader(*region)) return false;
  const RemoteM7ToM4Channel& channel = region->m7_to_m4;
  const uint32_t sequence = channel.telemetry_sequence;
  if (sequence == 0 || sequence == last_sequence) return false;
  const uint32_t slot_index = channel.active_telemetry_slot & 1u;
  const RemoteTelemetrySlot slot = channel.telemetry[slot_index];
  __DMB();
  if ((slot.sequence_begin & 1u) != 0 ||
      slot.sequence_begin != slot.sequence_end ||
      slot.sequence_begin != sequence ||
      slot.magic != kRemoteSharedMemoryMagic ||
      slot.version != kRemoteSharedMemoryVersion ||
      slot.frame_size != sizeof(RemoteTelemetrySlot) ||
      slot.checksum != objectChecksum(slot)) {
    return false;
  }
  *telemetry = slot;
  *published_sequence = sequence;
  return true;
}

}  // namespace csm::board::remote
