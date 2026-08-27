#include "board/control_island/ControlIslandSharedMemory.h"

#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
#else
#include <atomic>
#define __DMB() std::atomic_thread_fence(std::memory_order_seq_cst)
#define __DSB() std::atomic_thread_fence(std::memory_order_seq_cst)
#endif

namespace csm::board::control_island {
namespace {

constexpr uint16_t kDetailBadHeader = 1;
constexpr uint16_t kDetailTorn = 2;
constexpr uint16_t kDetailCrc = 3;
constexpr uint16_t kDetailBoot = 4;

#if defined(CORE_CM7)
__attribute__((section(".csm_control_ipc_d3"), aligned(32), used))
ControlIpcRegion g_control_ipc_storage;
__attribute__((section(".csm_can1_raw_ring_d3"), aligned(32), used))
RawCanEntry g_raw_can_ring_storage[kRawCanRingCapacity];
#endif

uint32_t nextEvenSequence(uint32_t current) {
  const uint32_t next = (current & ~1u) + 2u;
  return next == 0u ? 2u : next;
}

bool validHeader(const ControlIpcRegion& region) {
  return region.schema_id == kControlIslandSchemaId &&
         region.wire_contract_id == kHno1WireContractId &&
         region.memory_layout_id == kControlMemoryLayoutId &&
         region.region_size == sizeof(ControlIpcRegion) &&
         region.m7_boot_id != 0u;
}

template <typename Slot>
uint32_t slotCrc(const Slot& source) {
  Slot slot = source;
  slot.sequence_begin = 0;
  slot.crc32 = 0;
  slot.sequence_end = 0;
  return controlObjectCrc32(&slot, sizeof(slot));
}

void cleanM7Cache(void* address, size_t size) {
#if defined(CORE_CM7)
  const uintptr_t start = reinterpret_cast<uintptr_t>(address) & ~uintptr_t(31u);
  const uintptr_t end =
      (reinterpret_cast<uintptr_t>(address) + size + 31u) & ~uintptr_t(31u);
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
  const uintptr_t end =
      (reinterpret_cast<uintptr_t>(address) + size + 31u) & ~uintptr_t(31u);
  SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(start),
                               static_cast<int32_t>(end - start));
  __DSB();
#else
  (void)address;
  (void)size;
  __DMB();
#endif
}

}  // namespace

uint32_t controlObjectCrc32(const void* data, size_t size) {
  const uint8_t* bytes = static_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t index = 0; index < size; ++index) {
    crc ^= bytes[index];
    for (uint8_t bit = 0; bit < 8u; ++bit) {
      const uint32_t mask = 0u - (crc & 1u);
      crc = (crc >> 1u) ^ (0xEDB88320u & mask);
    }
  }
  return ~crc;
}

ControlIpcRegion* controlIpcRegion() {
#if defined(CSM_CONTROL_ISLAND_SHARED_MEMORY_TEST)
  static ControlIpcRegion test_region;
  return &test_region;
#else
  return reinterpret_cast<ControlIpcRegion*>(kControlIpcAddress);
#endif
}

RawCanEntry* rawCanRingEntries() {
#if defined(CSM_CONTROL_ISLAND_SHARED_MEMORY_TEST)
  static RawCanEntry test_ring[kRawCanRingCapacity];
  return test_ring;
#else
  return reinterpret_cast<RawCanEntry*>(kCan1RawRingAddress);
#endif
}

void initializeControlIpcForM7(uint32_t m7_boot_id) {
  ControlIpcRegion* region = controlIpcRegion();
  RawCanEntry* ring = rawCanRingEntries();
  memset(region, 0, sizeof(*region));
  memset(ring, 0, sizeof(RawCanEntry) * kRawCanRingCapacity);
  region->schema_id = kControlIslandSchemaId;
  region->wire_contract_id = kHno1WireContractId;
  region->memory_layout_id = kControlMemoryLayoutId;
  region->region_size = sizeof(ControlIpcRegion);
  region->m7_boot_id = m7_boot_id == 0u ? 1u : m7_boot_id;
  cleanM7Cache(region, sizeof(*region));
  cleanM7Cache(ring, sizeof(RawCanEntry) * kRawCanRingCapacity);
}

uint32_t initializeControlIpcForM4() {
  ControlIpcRegion* region = controlIpcRegion();
  if (!validHeader(*region)) return 0u;
  uint32_t boot_id = region->m4_boot_id + 1u;
  if (boot_id == 0u) boot_id = 1u;
  region->m4_boot_id = boot_id;
  __DMB();
  return boot_id;
}

bool publishFinalControlSnapshot(FinalControlSnapshotPayload payload) {
  ControlIpcRegion* region = controlIpcRegion();
  invalidateM7Cache(region, sizeof(*region));
  if (!validHeader(*region) || payload.m7_boot_id != region->m7_boot_id ||
      payload.schema_id != kControlIslandSchemaId ||
      payload.wire_contract_id != kHno1WireContractId ||
      payload.memory_layout_id != kControlMemoryLayoutId) {
    return false;
  }
  const uint32_t sequence = nextEvenSequence(region->control_sequence);
  const uint32_t slot_index = (region->control_active_slot ^ 1u) & 1u;
  payload.publish_sequence = sequence;
  ControlSnapshotSlot slot;
  slot.sequence_begin = sequence | 1u;
  slot.payload = payload;
  slot.sequence_end = sequence;
  slot.crc32 = slotCrc(slot);
  region->control[slot_index] = slot;
  cleanM7Cache(&region->control[slot_index], sizeof(slot));
  region->control[slot_index].sequence_begin = sequence;
  cleanM7Cache(&region->control[slot_index].sequence_begin,
               sizeof(region->control[slot_index].sequence_begin));
  region->control_active_slot = slot_index;
  region->control_sequence = sequence;
  cleanM7Cache(region, 64u);
  return true;
}

ControlReadResult readFinalControlSnapshot(uint32_t last_sequence) {
  ControlReadResult result;
  ControlIpcRegion* region = controlIpcRegion();
  invalidateM7Cache(region, sizeof(*region));
  if (!validHeader(*region)) {
    result.detail = kDetailBadHeader;
    return result;
  }
  const uint32_t sequence = region->control_sequence;
  const uint32_t slot_index = region->control_active_slot & 1u;
  result.sequence = sequence;
  if (sequence == 0u) return result;
  if (sequence == last_sequence) {
    result.accepted = true;
    return result;
  }
  const ControlSnapshotSlot slot = region->control[slot_index];
  __DMB();
  if ((slot.sequence_begin & 1u) != 0u ||
      slot.sequence_begin != slot.sequence_end ||
      slot.sequence_begin != sequence ||
      region->control_sequence != sequence ||
      (region->control_active_slot & 1u) != slot_index) {
    result.detail = kDetailTorn;
    return result;
  }
  if (slot.crc32 != slotCrc(slot)) {
    result.detail = kDetailCrc;
    return result;
  }
  if (slot.payload.m7_boot_id != region->m7_boot_id) {
    result.detail = kDetailBoot;
    return result;
  }
  result.accepted = true;
  result.new_snapshot = true;
  result.payload = slot.payload;
  return result;
}

bool publishControlHealth(ControlHealthPayload payload) {
  ControlIpcRegion* region = controlIpcRegion();
  if (!validHeader(*region) || payload.schema_id != kControlIslandSchemaId ||
      payload.wire_contract_id != kHno1WireContractId ||
      payload.memory_layout_id != kControlMemoryLayoutId ||
      payload.m4_boot_id == 0u) {
    return false;
  }
  const uint32_t sequence = nextEvenSequence(region->health_sequence);
  const uint32_t slot_index = (region->health_active_slot ^ 1u) & 1u;
  payload.health_sequence = sequence;
  ControlHealthSlot slot;
  slot.sequence_begin = sequence | 1u;
  slot.payload = payload;
  slot.sequence_end = sequence;
  slot.crc32 = slotCrc(slot);
  region->health[slot_index] = slot;
  __DMB();
  region->health[slot_index].sequence_begin = sequence;
  __DMB();
  region->health_active_slot = slot_index;
  region->health_sequence = sequence;
  __DMB();
  return true;
}

HealthReadResult readControlHealth(uint32_t last_sequence) {
  HealthReadResult result;
  ControlIpcRegion* region = controlIpcRegion();
  invalidateM7Cache(region, sizeof(*region));
  if (!validHeader(*region)) {
    result.detail = kDetailBadHeader;
    return result;
  }
  const uint32_t sequence = region->health_sequence;
  const uint32_t slot_index = region->health_active_slot & 1u;
  result.sequence = sequence;
  if (sequence == 0u) return result;
  if (sequence == last_sequence) {
    result.accepted = true;
    return result;
  }
  const ControlHealthSlot slot = region->health[slot_index];
  invalidateM7Cache(region, sizeof(*region));
  if ((slot.sequence_begin & 1u) != 0u ||
      slot.sequence_begin != slot.sequence_end ||
      slot.sequence_begin != sequence ||
      region->health_sequence != sequence ||
      (region->health_active_slot & 1u) != slot_index) {
    result.detail = kDetailTorn;
    return result;
  }
  if (slot.crc32 != slotCrc(slot)) {
    result.detail = kDetailCrc;
    return result;
  }
  result.accepted = true;
  result.new_snapshot = true;
  result.payload = slot.payload;
  return result;
}

bool publishBringupTrace(const BringupTracePayload& payload) {
  ControlIpcRegion* region = controlIpcRegion();
  if (!validHeader(*region)) return false;
  const uint32_t sequence = nextEvenSequence(region->bringup_sequence);
  BringupTraceSlot slot;
  slot.sequence_begin = sequence | 1u;
  slot.payload = payload;
  slot.sequence_end = sequence;
  region->bringup = slot;
  __DMB();
  region->bringup.sequence_begin = sequence;
  __DMB();
  region->bringup_sequence = sequence;
  __DMB();
  return true;
}

BringupReadResult readBringupTrace(uint32_t last_sequence) {
  BringupReadResult result;
  ControlIpcRegion* region = controlIpcRegion();
  invalidateM7Cache(region, sizeof(*region));
  if (!validHeader(*region)) {
    result.detail = kDetailBadHeader;
    return result;
  }
  const uint32_t sequence = region->bringup_sequence;
  result.sequence = sequence;
  if (sequence == 0u) return result;
  if (sequence == last_sequence) {
    result.accepted = true;
    return result;
  }
  const BringupTraceSlot slot = region->bringup;
  invalidateM7Cache(region, sizeof(*region));
  if ((slot.sequence_begin & 1u) != 0u ||
      slot.sequence_begin != slot.sequence_end ||
      slot.sequence_begin != sequence || region->bringup_sequence != sequence) {
    result.detail = kDetailTorn;
    return result;
  }
  result.accepted = true;
  result.new_snapshot = true;
  result.payload = slot.payload;
  return result;
}

uint32_t rawCanRingFill() {
  ControlIpcRegion* region = controlIpcRegion();
  const uint32_t fill = region->raw_write_sequence - region->raw_read_sequence;
  return fill > kRawCanRingCapacity ? static_cast<uint32_t>(kRawCanRingCapacity)
                                    : fill;
}

bool pushRawCanFromM4(const RawCanEntry& source) {
  ControlIpcRegion* region = controlIpcRegion();
  if (!validHeader(*region)) return false;
  const uint32_t write = region->raw_write_sequence;
  const uint32_t read = region->raw_read_sequence;
  const uint32_t fill = write - read;
  if (fill >= kRawCanRingCapacity) {
    ++region->raw_drop_count;
    return false;
  }
  RawCanEntry entry = source;
  entry.capture_sequence = write + 1u;
  rawCanRingEntries()[write % kRawCanRingCapacity] = entry;
  __DMB();
  region->raw_write_sequence = write + 1u;
  const uint32_t new_fill = fill + 1u;
  if (new_fill > region->raw_high_water) region->raw_high_water = new_fill;
  __DMB();
  return true;
}

bool popRawCanForM7(RawCanEntry* entry) {
  if (entry == nullptr) return false;
  ControlIpcRegion* region = controlIpcRegion();
  invalidateM7Cache(region, sizeof(*region));
  if (!validHeader(*region)) return false;
  const uint32_t read = region->raw_read_sequence;
  if (read == region->raw_write_sequence) return false;
  RawCanEntry* source = &rawCanRingEntries()[read % kRawCanRingCapacity];
  invalidateM7Cache(source, sizeof(*source));
  *entry = *source;
  __DMB();
  region->raw_read_sequence = read + 1u;
  cleanM7Cache(&region->raw_read_sequence, sizeof(region->raw_read_sequence));
  return true;
}

}  // namespace csm::board::control_island
