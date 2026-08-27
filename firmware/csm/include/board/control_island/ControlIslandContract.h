#pragma once

#include <stddef.h>
#include <stdint.h>

namespace csm::board::control_island {

// Frozen REV.B cross-image identity and fixed SRAM4 ownership.
static constexpr uint32_t kControlIslandSchemaId = 0x43495343u;   // "CISC"
static constexpr uint32_t kHno1WireContractId = 0x484E4F31u;     // "HNO1"
static constexpr uint32_t kControlMemoryLayoutId = 0xD3A8B800u;
static constexpr uintptr_t kControlIpcAddress = 0x3800A800u;
static constexpr size_t kControlIpcReservedBytes = 0x1000u;
static constexpr uintptr_t kCan1RawRingAddress = 0x3800B800u;
static constexpr size_t kCan1RawRingReservedBytes = 0x4000u;

static constexpr uint8_t kLaneCount = 3;
static constexpr uint8_t kLane005 = 0;
static constexpr uint8_t kLane007 = 1;
static constexpr uint8_t kLane364 = 2;
static constexpr uint32_t kLaneIds[kLaneCount] = {0x005u, 0x007u, 0x364u};
static constexpr uint32_t kLanePeriodsUs[kLaneCount] = {5000u, 20000u, 20000u};
static constexpr uint8_t kLaneDedicatedBuffers[kLaneCount] = {0u, 1u, 2u};
static constexpr uint8_t kAllLanePermitMask = 0x07u;

enum class ControlSource : uint32_t {
  None = 0,
  Remote = 1,
  Host = 2,
};

enum class LaneState : uint8_t {
  Free = 0,
  PendingSafe = 1,
  PendingActive = 2,
};

enum class TxRequestResult : uint8_t {
  Accepted = 0,
  TransportUnavailable = 1,
  AlreadyPending = 2,
  AddFailed = 3,
  EnableFailedNoPending = 4,
  EnableFailedAbortPending = 5,
  EnableFailedAbortFailed = 6,
};

enum class BringupStage : uint16_t {
  None = 0,
  M4Entered = 1,
  ControlIpcValidated = 2,
  RemoteIpcValidated = 3,
  ExecutorInitialized = 4,
  Tim4Configured = 5,
  FdcanBeginEntered = 6,
  MbedBootstrapReturned = 7,
  Fdcan1InstanceValidated = 8,
  HalInitialized = 9,
  FilterConfigured = 10,
  GlobalFilterConfigured = 11,
  HalStarted = 12,
  InterruptLinesConfigured = 13,
  NotificationsActivated = 14,
  NvicConfigured = 15,
  FdcanOperational = 16,
  FirstTim4Tick = 17,
  ForegroundLoopEntered = 18,
};

enum class BringupFailure : uint16_t {
  None = 0,
  ControlIpc = 1,
  RemoteIpc = 2,
  Executor = 3,
  Tim4 = 4,
  MbedBootstrap = 5,
  FdcanInstance = 6,
  HalInit = 7,
  Filter = 8,
  GlobalFilter = 9,
  HalStart = 10,
  InterruptLine = 11,
  Notification = 12,
  ClockContract = 13,
};

enum class TransactionState : uint8_t {
  None = 0,
  Active = 1,
  Complete = 2,
  Cancelled = 3,
  Faulted = 4,
};

enum class SafeWireAction : uint8_t {
  SuppressTx = 0,
  FixedSafeFrame = 1,
};

struct SafeWireFrame {
  SafeWireAction action = SafeWireAction::SuppressTx;
  uint8_t data[8] = {};
};

struct LaneSafeWirePolicy {
  SafeWireFrame idle_safe = {};
};

// Frozen only where existing HNO1 contract evidence defines a wire-safe frame.
// No M4 path derives vehicle semantics or fills an unspecified lane with zero.
static constexpr LaneSafeWirePolicy kLaneSafeWirePolicies[kLaneCount] = {
    {SafeWireAction::FixedSafeFrame, {0xAA, 0x02, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00}},
    {SafeWireAction::FixedSafeFrame, {0x82, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00}},
    {SafeWireAction::SuppressTx, {}},
};

struct LaneExecutionImage {
  uint32_t value_generation = 0;
  uint8_t valid = 0;
  uint8_t flags = 0;
  uint16_t reserved = 0;
  uint8_t data[8] = {};
};

// Optional strict-N primitive. It is deliberately source- and vehicle-agnostic.
struct BoundedTxTransaction {
  uint32_t transaction_id = 0;
  uint32_t payload_generation = 0;
  uint16_t requested_success_count = 0;
  uint8_t lane_index = 0;
  uint8_t active = 0;
  uint8_t data[8] = {};
};

struct FinalControlSnapshotPayload {
  uint32_t schema_id = kControlIslandSchemaId;
  uint32_t wire_contract_id = kHno1WireContractId;
  uint32_t memory_layout_id = kControlMemoryLayoutId;
  uint32_t m7_boot_id = 0;
  uint32_t publish_sequence = 0;
  uint32_t source_image_generation = 0;
  uint32_t source_lease_sequence = 0;
  uint32_t source_epoch = 0;
  uint32_t activation_epoch = 0;
  uint32_t active_source = static_cast<uint32_t>(ControlSource::None);
  uint32_t permit_mask = 0;
  LaneExecutionImage lanes[kLaneCount] = {};
  BoundedTxTransaction transaction = {};
};

struct alignas(32) ControlSnapshotSlot {
  uint32_t sequence_begin = 0;
  FinalControlSnapshotPayload payload = {};
  uint32_t crc32 = 0;
  uint32_t sequence_end = 0;
};

struct FdcanRawSnapshot {
  uint32_t cccr = 0;
  uint32_t psr = 0;
  uint32_t ecr = 0;
  uint32_t txfqs = 0;
  uint32_t txbrp = 0;
  uint32_t txbto = 0;
  uint32_t txbcf = 0;
  uint32_t ir = 0;
  uint32_t hal_state = 0;
  uint32_t hal_error = 0;
};

struct TxBufferReconciliation {
  uint32_t pending = 0;
  uint32_t transmitted = 0;
  uint32_t cancelled = 0;
  uint32_t failed = 0;
};

constexpr TxBufferReconciliation reconcileAcceptedTxBuffers(
    uint32_t accepted, uint32_t txbrp, uint32_t txbto, uint32_t txbcf) {
  TxBufferReconciliation result;
  result.transmitted = accepted & txbto;
  result.cancelled = accepted & txbcf;
  const uint32_t terminal = result.transmitted | result.cancelled;
  result.pending = accepted & txbrp & ~terminal;
  result.failed = accepted & ~(txbrp | txbto | txbcf);
  return result;
}

struct LaneHealth {
  uint32_t schedule_due = 0;
  uint32_t policy_suppressed = 0;
  uint32_t transport_blocked = 0;
  uint32_t pending_blocked = 0;
  uint32_t request_attempt = 0;
  uint32_t request_accepted = 0;
  uint32_t request_failed = 0;
  uint32_t tx_success = 0;
  uint32_t cancel_count = 0;
  uint32_t cancel_race_count = 0;
  uint32_t tracking_fault = 0;
  uint32_t last_value_generation = 0;
  uint8_t state = static_cast<uint8_t>(LaneState::Free);
  uint8_t reserved[3] = {};
};

struct ControlHealthPayload {
  uint32_t schema_id = kControlIslandSchemaId;
  uint32_t wire_contract_id = kHno1WireContractId;
  uint32_t memory_layout_id = kControlMemoryLayoutId;
  uint32_t m4_boot_id = 0;
  uint32_t health_sequence = 0;
  uint32_t flags = 0;
  uint32_t m7_publish_sequence_seen = 0;
  uint32_t m7_publish_age_local_ms = UINT32_MAX;
  uint32_t m7_publish_max_gap_local_ms = 0;
  uint32_t source_epoch_seen = 0;
  uint32_t activation_epoch_seen = 0;
  uint32_t active_source_seen = 0;
  uint32_t ipc_integrity_miss = 0;
  uint32_t m7_stale_count = 0;
  uint32_t error_warning_count = 0;
  uint32_t error_passive_count = 0;
  uint32_t bus_off_count = 0;
  uint32_t raw_ring_fill = 0;
  uint32_t raw_ring_high_water = 0;
  uint32_t raw_ring_drop = 0;
  uint32_t tim4_tick_total = 0;
  uint32_t tim4_first_tick_us = 0;
  uint32_t tim4_last_tick_us = 0;
  uint32_t tim4_max_gap_us = 0;
  uint32_t fdcan_kernel_clock_hz = 0;
  uint32_t nominal_prescaler = 0;
  uint32_t nominal_sjw = 0;
  uint32_t nominal_time_seg1 = 0;
  uint32_t nominal_time_seg2 = 0;
  uint32_t nominal_bitrate = 0;
  uint32_t fdcan_irq_total = 0;
  uint32_t tx_complete_callback_total = 0;
  uint32_t tx_abort_callback_total = 0;
  uint32_t error_callback_total = 0;
  uint32_t last_error_callback_status = 0;
  uint32_t add_failure_total = 0;
  uint32_t enable_failure_total = 0;
  uint32_t abort_failure_total = 0;
  uint32_t health_snapshot_reject_total = 0;
  uint32_t transaction_id = 0;
  uint16_t transaction_requested = 0;
  uint16_t transaction_completed = 0;
  uint8_t transaction_state = static_cast<uint8_t>(TransactionState::None);
  uint8_t fdcan_state = 0;
  uint8_t reserved_state[2] = {};
  LaneHealth lanes[kLaneCount] = {};
  FdcanRawSnapshot current = {};
  FdcanRawSnapshot first_fault = {};
  FdcanRawSnapshot last_fault = {};
};

static constexpr uint32_t kHealthFlagReady = 1u << 0;
static constexpr uint32_t kHealthFlagClockContractOk = 1u << 1;
static constexpr uint32_t kHealthFlagBusOff = 1u << 3;
static constexpr uint32_t kHealthFlagErrorPassive = 1u << 4;
static constexpr uint32_t kHealthFlagM7Fresh = 1u << 5;
static constexpr uint32_t kHealthFlagControlActive = 1u << 6;
static constexpr uint32_t kHealthFlagTrackingFault = 1u << 7;
static constexpr uint32_t kHealthFlagTransportReady = 1u << 8;
static constexpr uint32_t kHealthFlagActiveMotion = 1u << 9;
static constexpr uint32_t kHealthFlagTim4Configured = 1u << 10;
static constexpr uint32_t kHealthFlagTim4Ticking = 1u << 11;

struct BringupTracePayload {
  uint64_t source_id = 0;
  uint64_t runtime_contract_id = 0;
  uint32_t build_id = 0;
  uint32_t m4_boot_id = 0;
  uint16_t stage = static_cast<uint16_t>(BringupStage::None);
  uint16_t failure = static_cast<uint16_t>(BringupFailure::None);
  uint32_t failure_detail = 0;
  uint32_t flags = 0;
};

struct alignas(32) BringupTraceSlot {
  uint32_t sequence_begin = 0;
  BringupTracePayload payload = {};
  uint32_t sequence_end = 0;
};

struct alignas(32) ControlHealthSlot {
  uint32_t sequence_begin = 0;
  ControlHealthPayload payload = {};
  uint32_t crc32 = 0;
  uint32_t sequence_end = 0;
};

struct alignas(32) ControlIpcRegion {
  uint32_t schema_id = 0;
  uint32_t wire_contract_id = 0;
  uint32_t memory_layout_id = 0;
  uint32_t region_size = 0;
  uint32_t m7_boot_id = 0;
  uint32_t control_active_slot = 0;
  uint32_t control_sequence = 0;
  uint32_t health_active_slot = 0;
  uint32_t health_sequence = 0;
  uint32_t raw_write_sequence = 0;
  uint32_t raw_read_sequence = 0;
  uint32_t raw_drop_count = 0;
  uint32_t raw_high_water = 0;
  uint32_t m4_boot_id = 0;
  uint32_t bringup_sequence = 0;
  uint32_t reserved_header = 0;
  BringupTraceSlot bringup = {};
  ControlSnapshotSlot control[2] = {};
  ControlHealthSlot health[2] = {};
};

struct alignas(32) RawCanEntry {
  uint32_t capture_sequence = 0;
  uint32_t mono_us = 0;
  uint32_t can_id_flags = 0;
  uint32_t fdcan_timestamp = 0;
  uint8_t dlc_flags = 0;
  uint8_t bus = 1;
  uint16_t reserved0 = 0;
  uint8_t data[8] = {};
  uint32_t reserved1 = 0;
};

static constexpr size_t kRawCanRingCapacity =
    kCan1RawRingReservedBytes / sizeof(RawCanEntry);

static_assert(sizeof(LaneExecutionImage) == 16u, "lane image layout drift");
static_assert(sizeof(RawCanEntry) == 32u, "raw CAN entry must remain 32 bytes");
static_assert(kRawCanRingCapacity == 512u, "raw CAN ring capacity drift");
static_assert(sizeof(ControlIpcRegion) <= kControlIpcReservedBytes,
              "control IPC exceeds fixed D3 region");

constexpr bool elapsedAtLeast(uint32_t now, uint32_t then, uint32_t interval) {
  return static_cast<uint32_t>(now - then) >= interval;
}

}  // namespace csm::board::control_island
