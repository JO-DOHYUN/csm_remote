#include <Arduino.h>
#include <cstring>
#include <mbed.h>
#if defined(SERIAL_CDC)
#include "USB/PluggableUSBSerial.h"
#endif

#include "BoardPins.h"
#include "board/CapabilityPublisher.h"
#include "board/HostDownlinkParser.h"
#include "board/SafetySupervisor.h"
#include "board/StatusLed.h"
#include "board/uplink/CanRxSegmentBuilder.h"
#include "board/uplink/SerialTxScheduler.h"
#include "board/uplink/UplinkPriorityPolicy.h"
#include "board/uplink/UplinkScheduler.h"
#include "protocol/HostCommands.h"
#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

#ifndef CSM_FW_GIT_SHA
#define CSM_FW_GIT_SHA "unknown"
#endif

#ifndef CSM_FW_GIT_DIRTY
#define CSM_FW_GIT_DIRTY 1
#endif

#ifndef CSM_FW_ENV_NAME
#define CSM_FW_ENV_NAME "unknown"
#endif

#ifndef CSM_FW_BUILD_EPOCH
#define CSM_FW_BUILD_EPOCH 0
#endif

#ifndef CSM_FW_BUILD_ID
#define CSM_FW_BUILD_ID 0
#endif

#ifndef BOARD_HW_PROFILE_MID_TJA1051_DUAL
#define BOARD_HW_PROFILE_MID_TJA1051_DUAL 0
#endif

#ifndef BOARD_HW_PROFILE_MID_MCP2515
#define BOARD_HW_PROFILE_MID_MCP2515 0
#endif

#ifndef BOARD_TARGET_INTERNAL_CAN_LANE0
#define BOARD_TARGET_INTERNAL_CAN_LANE0 0
#endif

#ifndef BOARD_TARGET_INTERNAL_CAN_LANE1
#define BOARD_TARGET_INTERNAL_CAN_LANE1 0
#endif

#ifndef BOARD_ENABLE_MCP2515
#define BOARD_ENABLE_MCP2515 1
#endif

#ifndef BOARD_ENABLE_MCP2515_INIT
#define BOARD_ENABLE_MCP2515_INIT BOARD_ENABLE_MCP2515
#endif

#ifndef BOARD_CSM_PROFILE_PASSIVE_PRODUCT
#define BOARD_CSM_PROFILE_PASSIVE_PRODUCT 0
#endif

#ifndef BOARD_CSM_PROFILE_FULL_INSTRUMENTED
#define BOARD_CSM_PROFILE_FULL_INSTRUMENTED (!BOARD_CSM_PROFILE_PASSIVE_PRODUCT)
#endif

#ifndef BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE
#define BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE 0
#endif

#ifndef BOARD_PASSIVE_HARDWARE_SAFETY_CASE_ID
#define BOARD_PASSIVE_HARDWARE_SAFETY_CASE_ID 0
#endif

#ifndef BOARD_PASSIVE_BENCH_VERIFICATION_ID
#define BOARD_PASSIVE_BENCH_VERIFICATION_ID 0
#endif

#ifndef BOARD_PASSIVE_FIELD_SKU_ID
#define BOARD_PASSIVE_FIELD_SKU_ID 0
#endif

#ifndef BOARD_PASSIVE_EXTERNAL_ANALYZER_ARTIFACT_ID
#define BOARD_PASSIVE_EXTERNAL_ANALYZER_ARTIFACT_ID 0
#endif

#ifndef BOARD_PASSIVE_HOTPLUG_PASS_COUNT
#define BOARD_PASSIVE_HOTPLUG_PASS_COUNT 0
#endif

#ifndef BOARD_PASSIVE_HARDWARE_SILENT_STRAPPED
#define BOARD_PASSIVE_HARDWARE_SILENT_STRAPPED 0
#endif

#ifndef BOARD_PASSIVE_GALVANIC_ISOLATED
#define BOARD_PASSIVE_GALVANIC_ISOLATED 0
#endif

#ifndef BOARD_PASSIVE_POWER_OFF_PASSIVE
#define BOARD_PASSIVE_POWER_OFF_PASSIVE 0
#endif

#ifndef BOARD_PASSIVE_TXD_GATED
#define BOARD_PASSIVE_TXD_GATED 0
#endif

#ifndef BOARD_PASSIVE_NORMAL_ENABLE_PATH_POPULATED
#define BOARD_PASSIVE_NORMAL_ENABLE_PATH_POPULATED 1
#endif

#ifndef BOARD_ENABLE_TIM3_ENCODER
#define BOARD_ENABLE_TIM3_ENCODER 0
#endif

#ifndef BOARD_ENABLE_SAFETY_IO
#define BOARD_ENABLE_SAFETY_IO 1
#endif

#ifndef BOARD_ENABLE_ENCODER_IO
#define BOARD_ENABLE_ENCODER_IO 1
#endif

#ifndef BOARD_CAN_USE_INT
#define BOARD_CAN_USE_INT 0
#endif

// 0: periodic polling only
// 1: MCP2515 INT_N active-low level hint + bounded polling fallback
// 2: EXTI falling-edge hint + INT_N active-low level + bounded polling fallback
#ifndef BOARD_CAN_IRQ_MODE
#if BOARD_CAN_USE_INT
#define BOARD_CAN_IRQ_MODE 1
#else
#define BOARD_CAN_IRQ_MODE 0
#endif
#endif

#ifndef BOARD_CAN_POLL_FALLBACK_US
#define BOARD_CAN_POLL_FALLBACK_US 2000
#endif

#ifndef BOARD_CAN_SERIAL_DRAIN_BUDGET
#define BOARD_CAN_SERIAL_DRAIN_BUDGET 32
#endif

#ifndef BOARD_CAN_QUEUE_SIZE
#define BOARD_CAN_QUEUE_SIZE 4096
#endif

#ifndef BOARD_SERIAL_TX_RING_SIZE
#define BOARD_SERIAL_TX_RING_SIZE 65536
#endif

#ifndef BOARD_SERIAL_TX_CAN_INTERLEAVE_BYTES
#define BOARD_SERIAL_TX_CAN_INTERLEAVE_BYTES 64
#endif

#ifndef BOARD_SERIAL_TX_CAN_INTERLEAVE_MCP_BUDGET
#define BOARD_SERIAL_TX_CAN_INTERLEAVE_MCP_BUDGET 128
#endif

#ifndef BOARD_SERIAL_TX_CHUNK_BYTES
#define BOARD_SERIAL_TX_CHUNK_BYTES 512
#endif

#ifndef BOARD_SERIAL_TX_CRITICAL_RESERVE_BYTES
#define BOARD_SERIAL_TX_CRITICAL_RESERVE_BYTES 8192
#endif

#ifndef BOARD_SERIAL_TX_MAX_WRITES_PER_PUMP
#define BOARD_SERIAL_TX_MAX_WRITES_PER_PUMP 2
#endif

#ifndef BOARD_SERIAL_TX_MAX_BYTES_PER_PUMP
#define BOARD_SERIAL_TX_MAX_BYTES_PER_PUMP 1024
#endif

#ifndef BOARD_SERIAL_TX_DRAIN_TIME_BUDGET_US
#define BOARD_SERIAL_TX_DRAIN_TIME_BUDGET_US 1000
#endif

#ifndef BOARD_SERIAL_TX_NORMAL_HIGH_WATER_BYTES
#define BOARD_SERIAL_TX_NORMAL_HIGH_WATER_BYTES 49152
#endif

#ifndef BOARD_SERIAL_TX_NORMAL_LOW_WATER_BYTES
#define BOARD_SERIAL_TX_NORMAL_LOW_WATER_BYTES 32768
#endif

#ifndef BOARD_CAN_RECORDS_BEFORE_MCP_SERVICE
#define BOARD_CAN_RECORDS_BEFORE_MCP_SERVICE 2
#endif

#ifndef BOARD_CAN_RECORD_DRAIN_TIME_BUDGET_US
#define BOARD_CAN_RECORD_DRAIN_TIME_BUDGET_US 1200
#endif

#ifndef BOARD_MCP2515_RX_DRAIN_TIME_BUDGET_US
#define BOARD_MCP2515_RX_DRAIN_TIME_BUDGET_US 1200
#endif

#ifndef BOARD_CAN_RX_SEGMENT_FLUSH_US
#define BOARD_CAN_RX_SEGMENT_FLUSH_US 1000
#endif

#ifndef BOARD_MCP2515_LOOP_ENTRY_DRAIN_BUDGET
#define BOARD_MCP2515_LOOP_ENTRY_DRAIN_BUDGET 512
#endif

#ifndef BOARD_CAN_ERROR_EVENT_PERIOD_MS
#define BOARD_CAN_ERROR_EVENT_PERIOD_MS 500
#endif

#ifndef BOARD_CAPABILITY_PERIOD_MS
#define BOARD_CAPABILITY_PERIOD_MS 2000
#endif

#ifndef BOARD_UPLINK_BOOT_QUIET_MS
#define BOARD_UPLINK_BOOT_QUIET_MS 2000
#endif

#ifndef BOARD_ENCODER_DERIVED_PERIOD_MS
#define BOARD_ENCODER_DERIVED_PERIOD_MS 50
#endif

#ifndef BOARD_ENCODER_DERIVED_PRESSURE_PERIOD_MS
#define BOARD_ENCODER_DERIVED_PRESSURE_PERIOD_MS 250
#endif

#ifndef BOARD_ENCODER_DERIVED_BOOT_QUIET_PERIOD_MS
#define BOARD_ENCODER_DERIVED_BOOT_QUIET_PERIOD_MS 500
#endif

#ifndef BOARD_ENABLE_STATUS_LED
#define BOARD_ENABLE_STATUS_LED 1
#endif

#ifndef BOARD_ENABLE_RUNTIME_WATCHDOG
#define BOARD_ENABLE_RUNTIME_WATCHDOG 1
#endif

#ifndef BOARD_RUNTIME_WATCHDOG_TIMEOUT_MS
#define BOARD_RUNTIME_WATCHDOG_TIMEOUT_MS 3000
#endif

#ifndef BOARD_USB_CDC_RECONNECT_RESET_MS
#define BOARD_USB_CDC_RECONNECT_RESET_MS 1500
#endif

#ifndef BOARD_USB_CDC_DTR_SESSION_REQUIRED
#define BOARD_USB_CDC_DTR_SESSION_REQUIRED 1
#endif

#ifndef BOARD_USB_CDC_DTR_SESSION_ONLY
#define BOARD_USB_CDC_DTR_SESSION_ONLY BOARD_CSM_PROFILE_PASSIVE_PRODUCT
#endif

#ifndef BOARD_ENABLE_MCP2515_TX_TEST
#define BOARD_ENABLE_MCP2515_TX_TEST 0
#endif

#ifndef BOARD_MCP2515_TX_PERIOD_MS
#define BOARD_MCP2515_TX_PERIOD_MS 500
#endif

#ifndef BOARD_MCP2515_TX_TEST_ID
#define BOARD_MCP2515_TX_TEST_ID 0x321
#endif

#ifndef BOARD_MCP2515_TX_AUDIT_TIMEOUT_MS
#define BOARD_MCP2515_TX_AUDIT_TIMEOUT_MS 100
#endif

#ifndef BOARD_MCP2515_TX_USE_ONESHOT
#define BOARD_MCP2515_TX_USE_ONESHOT 0
#endif

#ifndef BOARD_ENABLE_BUILTIN_CAN_TX_TEST
#define BOARD_ENABLE_BUILTIN_CAN_TX_TEST 0
#endif

#ifndef BOARD_ENABLE_BUILTIN_CAN_RX
#define BOARD_ENABLE_BUILTIN_CAN_RX BOARD_ENABLE_BUILTIN_CAN_TX_TEST
#endif

#ifndef BOARD_ENABLE_HOST_CAN_TX
#define BOARD_ENABLE_HOST_CAN_TX BOARD_ENABLE_BUILTIN_CAN_TX_TEST
#endif

#ifndef BOARD_ENABLE_HOST_CAN_TX_BUILTIN
#define BOARD_ENABLE_HOST_CAN_TX_BUILTIN BOARD_ENABLE_HOST_CAN_TX
#endif

#ifndef BOARD_ENABLE_HOST_CAN_TX_MCP2515
#define BOARD_ENABLE_HOST_CAN_TX_MCP2515 0
#endif

#define BOARD_ENABLE_HOST_CAN_TX_ANY (BOARD_ENABLE_HOST_CAN_TX || BOARD_ENABLE_HOST_CAN_TX_BUILTIN || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
#define BOARD_ENABLE_BUILTIN_CAN_LANE (BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_BUILTIN_CAN_RX || BOARD_ENABLE_HOST_CAN_TX_BUILTIN)

#ifndef BOARD_ENABLE_HOST_DOWNLINK
#define BOARD_ENABLE_HOST_DOWNLINK BOARD_ENABLE_HOST_CAN_TX_ANY
#endif

#ifndef BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND
#define BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND 0
#endif

#ifndef BOARD_BUILTIN_CAN_RX_BUS
#define BOARD_BUILTIN_CAN_RX_BUS 1
#endif

#ifndef BOARD_MCP2515_BUS_ID
#define BOARD_MCP2515_BUS_ID 0
#endif

#ifndef BOARD_MCP2515_BUS_ROLE
#define BOARD_MCP2515_BUS_ROLE 2
#endif

#ifndef BOARD_BUILTIN_CAN_BUS_ID
#define BOARD_BUILTIN_CAN_BUS_ID BOARD_BUILTIN_CAN_RX_BUS
#endif

#ifndef BOARD_BUILTIN_CAN_BUS_ROLE
#define BOARD_BUILTIN_CAN_BUS_ROLE 1
#endif

#ifndef BOARD_MCP2515_CONTROL_TX_ALLOWED
#define BOARD_MCP2515_CONTROL_TX_ALLOWED BOARD_ENABLE_HOST_CAN_TX_MCP2515
#endif

#ifndef BOARD_MCP2515_CAPABILITY_TERMINATION_POLICY
#define BOARD_MCP2515_CAPABILITY_TERMINATION_POLICY 0
#endif

#ifndef BOARD_MCP2515_CAPABILITY_ISOLATION_POLICY
#define BOARD_MCP2515_CAPABILITY_ISOLATION_POLICY 0
#endif

#ifndef BOARD_MCP2515_INIT_RETRY_MIN_MS
#define BOARD_MCP2515_INIT_RETRY_MIN_MS 1000
#endif

#ifndef BOARD_MCP2515_INIT_RETRY_MAX_MS
#define BOARD_MCP2515_INIT_RETRY_MAX_MS 30000
#endif

#ifndef BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT
#define BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT 0
#endif

#ifndef BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED
#define BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED BOARD_ENABLE_HOST_CAN_TX_BUILTIN
#endif

#ifndef BOARD_ENABLE_VOLTAGE_ADC
#define BOARD_ENABLE_VOLTAGE_ADC 0
#endif

#ifndef BOARD_VOLTAGE_SAMPLE_PERIOD_MS
#define BOARD_VOLTAGE_SAMPLE_PERIOD_MS 20
#endif

#ifndef BOARD_BUILTIN_CAN_TX_PERIOD_MS
#define BOARD_BUILTIN_CAN_TX_PERIOD_MS 500
#endif

#ifndef BOARD_BUILTIN_CAN_TX_TEST_ID
#define BOARD_BUILTIN_CAN_TX_TEST_ID 0x322
#endif

#ifndef BOARD_ENABLE_CAN_TX_GATE_FOR_TEST
#define BOARD_ENABLE_CAN_TX_GATE_FOR_TEST BOARD_ENABLE_BUILTIN_CAN_TX_TEST
#endif

#ifndef BOARD_CAN_TRANSCEIVER_ENABLE_FOR_RX
#define BOARD_CAN_TRANSCEIVER_ENABLE_FOR_RX BOARD_ENABLE_BUILTIN_CAN_RX
#endif

#ifndef BOARD_PRODUCT_ACK_OBSERVE_MODE
#define BOARD_PRODUCT_ACK_OBSERVE_MODE BOARD_CSM_PROFILE_PASSIVE_PRODUCT
#endif

#ifndef BOARD_PRODUCT_PRE_SESSION_SAFE_RECEIVE
#define BOARD_PRODUCT_PRE_SESSION_SAFE_RECEIVE BOARD_CSM_PROFILE_PASSIVE_PRODUCT
#endif

#ifndef BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION
#define BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION BOARD_CSM_PROFILE_PASSIVE_PRODUCT
#endif

#ifndef BOARD_PASSIVE_SESSION_CAN_FRONTEND_QUIET_MS
#define BOARD_PASSIVE_SESSION_CAN_FRONTEND_QUIET_MS 750
#endif

#ifndef BOARD_PASSIVE_SESSION_CAN_FRONTEND_RETRY_MS
#define BOARD_PASSIVE_SESSION_CAN_FRONTEND_RETRY_MS 250
#endif

#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
#if BOARD_ENABLE_HOST_CAN_TX || BOARD_ENABLE_HOST_CAN_TX_BUILTIN || BOARD_ENABLE_HOST_CAN_TX_MCP2515
#error "Passive Product must compile out host CAN TX paths"
#endif
#if BOARD_ENABLE_HOST_DOWNLINK
#error "Passive Product must compile out HostDownlinkParser processing"
#endif
#if BOARD_MCP2515_CONTROL_TX_ALLOWED || BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED
#error "Passive Product must compile out control TX allowance"
#endif
#if BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_BUILTIN_CAN_TX_TEST
#error "Passive Product must compile out CAN TX test paths"
#endif
#if !BOARD_PRODUCT_ACK_OBSERVE_MODE
#error "Passive Product must be ACK-capable observe-only after stable host session"
#endif
#if !BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION
#error "Passive Product must defer CAN front-end init until CDC/DTR session is stable"
#endif
#if BOARD_USB_CDC_RECONNECT_RESET_MS != 0
#error "Passive Product must disable USB CDC reconnect reset"
#endif
#if !BOARD_ENABLE_BUILTIN_CAN_RX
#error "Passive Product must expose both vehicle CAN buses; fewer than two RX buses is invalid"
#endif
#endif

#if BOARD_ENABLE_BUILTIN_CAN_LANE
#include "drivers/CAN.h"
#endif

#if BOARD_ENABLE_MCP2515
#include <SPI.h>
#include <mcp2515.h>
#endif

// Build-time mode switch. Keep false for real CAN capture.
static constexpr bool kTestMode = false;

static constexpr uint8_t kFirmwareProfileUnknown = 0;
static constexpr uint8_t kFirmwareProfilePassiveProduct = 1;
static constexpr uint8_t kFirmwareProfileFullInstrumented = 2;
static constexpr uint8_t kProfileLockCompileTime = 1;
static constexpr uint8_t kVehicleImpactUnknown = 0;
static constexpr uint8_t kVehicleImpactPossible = 1;
static constexpr uint8_t kVehicleImpactConfiguredPassive = 2;
static constexpr uint8_t kVehicleImpactVerifiedPassive = 3;
static constexpr uint8_t kBusModeUnknown = 0;
static constexpr uint8_t kBusModeListenOnly = 1;
static constexpr uint8_t kBusModeHardwareSilent = 2;
static constexpr uint8_t kBusModeNormal = 3;

static constexpr uint32_t kPassiveViolationSerialDownlinkProcessed = (1u << 0);
static constexpr uint32_t kPassiveViolationCanTxCalled = (1u << 1);
static constexpr uint32_t kPassiveViolationMcpNormalMode = (1u << 2);
static constexpr uint32_t kPassiveViolationUsbResetAttempted = (1u << 3);
static constexpr uint32_t kPassiveViolationTransceiverSafeMissing = (1u << 4);
static constexpr uint32_t kPassiveViolationMcpReadbackMode = (1u << 5);
static constexpr uint32_t kPassiveViolationMcpTxreqSet = (1u << 6);

#if BOARD_ENABLE_MCP2515
static constexpr CAN_SPEED kMcp2515Bitrate = CAN_500KBPS;
static constexpr CAN_CLOCK kMcp2515Clock = MCP_8MHZ;
#ifndef BOARD_MCP2515_SPI_HZ
#define BOARD_MCP2515_SPI_HZ 250000
#endif
static constexpr uint32_t kMcp2515SpiHz = BOARD_MCP2515_SPI_HZ;
#endif

using csm::crc16_ccitt;
using csm::kFrameSof0;
using csm::kFrameSof1;
using csm::kHostCanTxAllowedPrimaryId;
using csm::kHostCanTxAllowedRangeEnd;
using csm::kHostCanTxAllowedRangeStart;
using csm::kMaxPayloadLen;
using csm::kProtocolVersion;
using csm::kCapabilityV1PayloadLen;
using csm::kCapabilityV2BusDescriptorLen;
using csm::kCapabilityV2PayloadLen;
using csm::kCapabilityV3PayloadLen;
using csm::kCapabilityV4PayloadLen;
using csm::kCapabilityV5PayloadLen;
using csm::kCapabilityV6PayloadLen;
using csm::kBoardHealthV2PayloadLen;
using csm::kBoardHealthV4PayloadLen;
using csm::kBoardHealthV6PayloadLen;
using csm::kBoardHealthV7PayloadLen;
using csm::kCanRxSegmentEntryLen;
using csm::kCanRxSegmentHeaderLen;
using csm::kCanRxSegmentMaxFrames;
using csm::mono64_us;
using csm::rd_u16_le;
using csm::rd_u32_le;
using csm::RecordType;
using csm::wr_i32_le;
using csm::wr_i64_le;
using csm::wr_u16_le;
using csm::wr_u32_le;
using csm::wr_u64_le;
using csm::board::uplink::CanRxSegmentBuilder;
using csm::board::uplink::CanRxSegmentItem;
using csm::board::uplink::SerialTxScheduler;
using csm::board::uplink::UplinkPriority;
using csm::board::uplink::UplinkScheduler;

using SafetyState = csm::board::SafetyState;

enum BoardEventCode : uint16_t {
  EventBoot = 1,
  EventCanBeginFailed = 2,
  EventCanRxQueueDrop = 3,
  EventEncoderFaultAsserted = 4,
  EventFieldPowerLost = 5,
  EventEstopAsserted = 6,
  EventEncoderIndex = 7,
  EventEncoderWrap = 8,
  EventMcp2515Error = 9,
  EventMcp2515SpiSnapshot = 10,
  EventBuiltinCanBeginFailed = 11,
  EventBuiltinCanTxFailed = 12,
  EventHostFrameCrcFailed = 13,
  EventHostCanTxRejected = 14,
  EventHostCanTxAccepted = 15,
  EventCan0BackendUnavailable = 16,
  EventMcp2515TxFailed = 17,
  EventSafetyStateChanged = 18,
  EventHostHeartbeat = 19,
  EventHostControlSession = 20,
  EventHostCommandUnsupported = 21,
  EventFaultLockoutCleared = 22,
  EventFirmwareIdentity = 23,
  EventSerialTxBackpressure = 24,
  EventSerialTxRingClear = 25,
  EventCanRxSegmentEnqueueFailed = 26,
  EventUsbCdcSessionOpen = 27,
  EventUsbCdcSessionClose = 28,
  EventUsbCdcDtrChange = 29,
  EventUsbHostAbsentCanDiscardSummary = 30,
  EventMcpPassiveModeReadback = 31,
  EventMcpPassiveModeViolation = 32,
  EventMcpTxreqViolation = 33,
  EventTransceiverSafeStateChanged = 34,
  EventUsbPowerOrResetSuspected = 35,
  EventCanFrontendPresessionHold = 36,
  EventCanFrontendSessionReady = 37,
  EventCanFrontendSessionInitFailed = 38,
  EventCanFrontendFaultHold = 39,
};

using CanRxItem = CanRxSegmentItem;

struct EncoderSnapshot {
  uint64_t mono_us;
  int64_t position;
  uint16_t tim3_count;
  uint8_t ab_state;
  uint8_t flags;
  uint32_t fault_flags;
};

struct BusRxRuntime {
  uint32_t rx_total;
  uint32_t drop_total;
  uint32_t queued;
  uint32_t high_water;
};

static constexpr uint32_t kCanQueueSize = BOARD_CAN_QUEUE_SIZE;
static constexpr uint32_t kCanQueueMask = kCanQueueSize - 1;
static_assert((kCanQueueSize & kCanQueueMask) == 0, "BOARD_CAN_QUEUE_SIZE must be a power of two");

static constexpr uint8_t kVoltageAdcBits = 12;
static constexpr uint8_t kVoltageChannelCount = 4;
#if BOARD_ENABLE_VOLTAGE_ADC
static constexpr pin_size_t kVoltagePins[kVoltageChannelCount] = {
    BoardPins::VoltageSense0,
    BoardPins::VoltageSense1,
    BoardPins::VoltageSense2,
    BoardPins::VoltageSense3,
};
static constexpr uint8_t kVoltageChannelIds[kVoltageChannelCount] = {0, 1, 2, 3};
#endif

static CanRxItem can_queue_bus0[kCanQueueSize];
static CanRxItem can_queue_bus1[kCanQueueSize];
static volatile uint32_t can_q_head[2] = {0, 0};
static volatile uint32_t can_q_tail[2] = {0, 0};
static BusRxRuntime can_bus_runtime[2] = {};

static uint64_t can_capture_seq_next = 0;
static uint8_t can_queue_pop_next_index = 0;
static volatile uint32_t can_rx_count_total = 0;
static volatile uint32_t can_rx_dropped_total = 0;
static volatile uint32_t can_fifo_overflow_total = 0;
static volatile uint32_t can_queue_high_water = 0;
static volatile uint32_t can_segment_enqueue_fail_total = 0;
static uint32_t pending_can_segment_enqueue_fail_frames = 0;
static uint32_t host_frame_crc_failed_total = 0;
static uint32_t host_heartbeat_total = 0;
static uint32_t host_control_session_total = 0;
static uint32_t host_can_tx_request_total = 0;
static uint32_t __attribute__((unused)) host_can_tx_accepted_total = 0;
static uint32_t host_can_tx_rejected_total = 0;

static volatile bool encoder_index_pending = false;
static volatile uint64_t encoder_index_mono_us = 0;
static volatile uint32_t encoder_index_count = 0;

static constexpr uint32_t kSerialTxRingSize = BOARD_SERIAL_TX_RING_SIZE;
static_assert(csm::encoded_typed_frame_len(kCanRxSegmentHeaderLen +
              kCanRxSegmentEntryLen * kCanRxSegmentMaxFrames) <= 512,
              "CAN_RX_SEGMENT typed frame must fit within one 512-byte USB HS packet");
static_assert(BOARD_UPLINK_CRITICAL_QUEUE_RECORDS >= 8,
              "critical uplink queue must reserve enough loss-evidence records");
static_assert(BOARD_UPLINK_CAN_TRUTH_QUEUE_RECORDS >= 16,
              "CAN truth queue must absorb high-load segment bursts");
static_assert(BOARD_UPLINK_POOL_LARGE_BLOCKS > BOARD_UPLINK_POOL_LARGE_CAN_RESERVE,
              "large pool must leave non-CAN space for health/capability records");
static_assert(BOARD_UPLINK_NORMAL_QUEUE_RECORDS >= 4,
              "normal uplink queue must absorb health/capability bursts");
static_assert(BOARD_UPLINK_DIAGNOSTIC_QUEUE_RECORDS >= 2,
              "diagnostic uplink queue must retain bounded low-value telemetry");
static SerialTxScheduler serial_tx_scheduler;
static UplinkScheduler uplink_scheduler;
static CanRxSegmentBuilder can_rx_segment_builder;
static uint32_t serial_tx_bytes_since_can_service = 0;
#if BOARD_ENABLE_MCP2515
static MCP2515* mcp2515 = nullptr;
static volatile bool mcp2515_irq_pending = false;
static volatile uint32_t mcp2515_irq_edges = 0;
#endif
static bool can_backend_ok = false;
static bool voltage_adc_ok = false;
#if BOARD_ENABLE_VOLTAGE_ADC
static uint32_t voltage_adc_sample_total = 0;
static uint32_t voltage_adc_drop_total = 0;
static uint32_t last_voltage_adc_sample_ms = 0;
#endif

#if BOARD_ENABLE_MCP2515
struct Mcp2515ServiceState {
  bool int_pin_enabled;
  bool exti_attached;
  bool last_int_low;
  uint32_t last_probe_us;
  uint32_t last_housekeeping_ms;
  uint32_t last_error_event_ms;
  uint32_t last_seen_irq_edges;
  uint32_t int_low_samples;
  uint32_t nomsg_while_int_low;
  uint32_t spi_error_total;
  uint32_t error_flag_total;
  uint32_t drain_time_budget_hit_total;
  uint8_t last_canintf;
  uint8_t last_eflg;
  uint8_t last_canctrl;
};

static Mcp2515ServiceState mcp_service = {};
#endif

#if BOARD_ENABLE_BUILTIN_CAN_LANE
static mbed::CAN builtin_can(PIN_CAN0_RX, PIN_CAN0_TX);
static bool builtin_can_tx_ok = false;
#endif

#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_BUILTIN
static uint32_t builtin_can_tx_total = 0;
static uint32_t builtin_can_tx_failed_total = 0;
#endif

#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST
static uint32_t last_builtin_can_tx_ms = 0;
#endif

#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
static uint32_t mcp2515_tx_total = 0;
static uint32_t mcp2515_tx_failed_total = 0;
static uint32_t mcp2515_tx_queue_full_total = 0;
struct Mcp2515PendingTx {
  bool active;
  uint32_t start_ms;
  uint8_t bus;
  uint32_t can_id_flags;
  uint8_t dlc;
  uint8_t data[8];
};
static Mcp2515PendingTx mcp2515_pending_tx = {};
static constexpr uint32_t kMcp2515TxQueueSize = 256;
static constexpr uint32_t kMcp2515TxQueueMask = kMcp2515TxQueueSize - 1;
static Mcp2515PendingTx mcp2515_tx_queue[kMcp2515TxQueueSize];
static uint32_t mcp2515_tx_q_head = 0;
static uint32_t mcp2515_tx_q_tail = 0;
#endif

#if BOARD_ENABLE_MCP2515 && BOARD_ENABLE_MCP2515_TX_TEST
static uint32_t last_mcp2515_tx_ms = 0;
#endif

#if BOARD_ENABLE_STATUS_LED
static csm::board::StatusLed status_led;
#endif

static TIM_HandleTypeDef htim3;
static bool encoder_timer_ok = false;
static uint16_t encoder_last_count = 0;
static int64_t encoder_position = 0;
static uint32_t encoder_wrap_events = 0;
static uint32_t encoder_fault_events = 0;
static bool encoder_fault_prev = false;
static bool field_power_prev = true;
static bool estop_prev = false;
static csm::board::SafetySupervisor safety_supervisor;
static SafetyState safety_state = SafetyState::MonitorOnly;

static uint32_t last_health_ms = 0;
static uint32_t last_capability_ms = 0;
static uint32_t last_encoder_derived_ms = 0;
static uint32_t uplink_boot_ms = 0;
static uint32_t last_watchdog_toggle_ms = 0;
static uint32_t last_can_init_retry_ms = 0;
static uint32_t can_init_retry_delay_ms = BOARD_MCP2515_INIT_RETRY_MIN_MS;
static uint32_t can_init_retry_count = 0;
#if BOARD_ENABLE_MCP2515
static bool mcp2515_listen_only_mode = false;
#endif
static bool ack_observe_enabled = false;
static bool usb_host_was_connected = false;
static bool uplink_session_was_open = false;
static uint32_t usb_disconnected_since_ms = 0;
static uint32_t usb_cdc_session_open_total = 0;
static uint32_t usb_cdc_session_close_total = 0;
static uint32_t usb_cdc_session_close_reported_total = 0;
static uint32_t usb_cdc_session_open_ms = 0;
static uint32_t usb_cdc_last_session_duration_ms = 0;
static uint32_t usb_cdc_dtr_change_total = 0;
static bool usb_cdc_last_dtr = false;
static bool usb_cdc_dtr_initialized = false;
static uint32_t usb_reconnect_count = 0;
static uint32_t usb_forced_reset_count = 0;
static uint32_t passive_violation_latch = 0;
static uint32_t can_rx_task_max_us = 0;
static uint32_t uplink_pool_high_water_bytes = 0;
static uint32_t capture_invalid_reason = 0;
static uint32_t host_absent_rx_discard_total[2] = {0, 0};
static uint32_t host_absent_fifo_overflow_total = 0;
static uint32_t host_absent_mcp_error_total = 0;
static uint32_t host_absent_duration_ms_total = 0;
static uint32_t host_absent_started_ms = 0;
static uint32_t host_absent_last_duration_ms = 0;
static bool host_absent_summary_pending = false;
static uint32_t host_session_epoch = 0;
static uint32_t transport_epoch = 0;
static uint32_t usb_attach_quarantine_total = 0;
static uint32_t host_absent_gap_total = 0;
static uint32_t pre_session_payload_replay_total = 0;
static uint32_t passive_readback_total = 0;
static uint32_t passive_readback_violation_total = 0;
static uint32_t txreq_violation_total = 0;
static uint32_t last_passive_readback_ms = 0;
static bool can_frontend_session_arm_pending = false;
static bool can_frontend_session_ready = false;
static uint32_t can_frontend_session_arm_after_ms = 0;
static uint32_t can_frontend_presession_hold_total = 0;
static uint32_t can_frontend_session_ready_total = 0;
static uint32_t can_frontend_session_init_fail_total = 0;
static uint32_t last_can_frontend_init_attempt_ms = 0;

static void __attribute__((unused)) latch_passive_violation(uint32_t mask) {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  passive_violation_latch |= mask;
#else
  (void)mask;
#endif
}

static uint8_t firmware_profile_id() {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  return kFirmwareProfilePassiveProduct;
#elif BOARD_CSM_PROFILE_FULL_INSTRUMENTED
  return kFirmwareProfileFullInstrumented;
#else
  return kFirmwareProfileUnknown;
#endif
}

static void discard_can_queue_for_session_quarantine();
static void set_can_observe_mode_for_session(bool enabled, bool force = false);
static bool init_can_backend();
static bool __attribute__((unused)) init_builtin_can_lane();

static uint8_t vehicle_impact_state() {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  return (BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE &&
          BOARD_PASSIVE_HARDWARE_SILENT_STRAPPED &&
          BOARD_PASSIVE_POWER_OFF_PASSIVE &&
          BOARD_PASSIVE_TXD_GATED &&
          BOARD_PASSIVE_HARDWARE_SAFETY_CASE_ID != 0 &&
          BOARD_PASSIVE_BENCH_VERIFICATION_ID != 0 &&
          BOARD_PASSIVE_EXTERNAL_ANALYZER_ARTIFACT_ID != 0 &&
          BOARD_PASSIVE_HOTPLUG_PASS_COUNT > 0)
             ? kVehicleImpactVerifiedPassive
             : kVehicleImpactConfiguredPassive;
#else
  return kVehicleImpactPossible;
#endif
}

static uint8_t __attribute__((unused)) passive_acceptance_allowed() {
  return vehicle_impact_state() == kVehicleImpactVerifiedPassive ? 1 : 0;
}

static void discard_session_uplink_payloads() {
  can_rx_segment_builder.discardPending();
  discard_can_queue_for_session_quarantine();
  uplink_scheduler.discardQueuedRecords();
}

static void note_can_rx_task_elapsed(uint32_t start_us) {
  const uint32_t elapsed = static_cast<uint32_t>(micros() - start_us);
  if (elapsed > can_rx_task_max_us) {
    can_rx_task_max_us = elapsed;
  }
}

static void reset_can_init_retry_backoff() {
  can_init_retry_delay_ms = BOARD_MCP2515_INIT_RETRY_MIN_MS;
  if (can_init_retry_delay_ms == 0) {
    can_init_retry_delay_ms = 1000;
  }
  can_init_retry_count = 0;
}

static void note_can_init_retry_failure() {
  can_init_retry_count++;
  uint32_t next_delay_ms = can_init_retry_delay_ms == 0 ? BOARD_MCP2515_INIT_RETRY_MIN_MS : can_init_retry_delay_ms;
  if (next_delay_ms == 0) {
    next_delay_ms = 1000;
  } else if (next_delay_ms < BOARD_MCP2515_INIT_RETRY_MAX_MS) {
    const uint32_t doubled = next_delay_ms * 2;
    next_delay_ms = (doubled > next_delay_ms) ? doubled : BOARD_MCP2515_INIT_RETRY_MAX_MS;
    if (next_delay_ms > BOARD_MCP2515_INIT_RETRY_MAX_MS) {
      next_delay_ms = BOARD_MCP2515_INIT_RETRY_MAX_MS;
    }
  }
  can_init_retry_delay_ms = next_delay_ms;
}

static void pump_can_rx_to_queue(int budget);
static bool emit_board_event(uint16_t code, uint16_t detail, uint32_t counter);
static bool emit_board_event_with_priority(uint16_t code, uint16_t detail, uint32_t counter,
                                           UplinkPriority priority);
static void service_deferred_loss_events();

static bool uplink_host_session_open() {
#if defined(SERIAL_CDC)
  return _SerialUSB.connected();
#else
  return true;
#endif
}

static bool usb_cdc_dtr_asserted() {
#if defined(SERIAL_CDC)
  return _SerialUSB.dtr();
#else
  return true;
#endif
}

static void service_serial_tx(uint32_t byte_budget = BOARD_SERIAL_TX_MAX_BYTES_PER_PUMP) {
  const csm::board::uplink::SerialTxServiceResult result =
      uplink_scheduler.service(byte_budget, millis(), micros());
  if (result.backpressure_event) {
    emit_board_event(EventSerialTxBackpressure,
                     static_cast<uint16_t>(result.backpressure_duration_ms & 0xFFFF),
                     serial_tx_scheduler.counters().backpressure_total);
  }
  serial_tx_bytes_since_can_service += result.actual_bytes;
  while (serial_tx_bytes_since_can_service >= BOARD_SERIAL_TX_CAN_INTERLEAVE_BYTES) {
    serial_tx_bytes_since_can_service -= BOARD_SERIAL_TX_CAN_INTERLEAVE_BYTES;
    if (!kTestMode) {
      pump_can_rx_to_queue(BOARD_SERIAL_TX_CAN_INTERLEAVE_MCP_BUDGET);
    }
  }
}

static void service_usb_cdc_reconnect_watchdog() {
#if defined(SERIAL_CDC)
#if BOARD_USB_CDC_RECONNECT_RESET_MS == 0
  (void)usb_host_was_connected;
  (void)usb_disconnected_since_ms;
  return;
#else
  const bool connected = _SerialUSB.connected();
  if (connected) {
    if (usb_disconnected_since_ms != 0) {
      usb_reconnect_count++;
    }
    usb_host_was_connected = true;
    usb_disconnected_since_ms = 0;
    return;
  }

  if (!usb_host_was_connected) {
    return;
  }

  const uint32_t now_ms = millis();
  if (usb_disconnected_since_ms == 0) {
    usb_disconnected_since_ms = now_ms;
    return;
  }

  if (now_ms - usb_disconnected_since_ms >= BOARD_USB_CDC_RECONNECT_RESET_MS) {
    usb_forced_reset_count++;
    latch_passive_violation(kPassiveViolationUsbResetAttempted);
    delay(10);
    NVIC_SystemReset();
  }
#endif
#endif
}

static bool enqueue_typed_record(RecordType type, const uint8_t* payload, uint16_t len,
                                 UplinkPriority priority, uint8_t flags = 0) {
#if !defined(SERIAL_CDC)
  static uint16_t serial_transport_seq = 0;
  return csm::emit_typed_record(Serial, type, payload, len, serial_transport_seq, flags);
#else
  if (!uplink_host_session_open()) {
    return false;
  }
  return uplink_scheduler.enqueueRecord(type, payload, len, priority, flags);
#endif
}

static bool uplink_boot_quiet_active() {
  return static_cast<uint32_t>(millis() - uplink_boot_ms) < BOARD_UPLINK_BOOT_QUIET_MS;
}

static bool uplink_tx_pressure_active() {
  return serial_tx_scheduler.backpressureActive() ||
         serial_tx_scheduler.hasActiveFrame() ||
         uplink_scheduler.pressureActive() ||
         uplink_scheduler.queuedPayloadBytes() >= BOARD_SERIAL_TX_NORMAL_LOW_WATER_BYTES;
}

static bool should_suppress_low_value_record(RecordType type, UplinkPriority priority) {
  if (priority == UplinkPriority::Critical ||
      priority == UplinkPriority::CanTruth ||
      type == RecordType::Capability ||
      type == RecordType::BoardHealth) {
    return false;
  }
  if (priority != UplinkPriority::Diagnostic) {
    return false;
  }
  return uplink_boot_quiet_active() || uplink_tx_pressure_active();
}

static bool emit_record(RecordType type, const uint8_t* payload, uint16_t len,
                        UplinkPriority priority, uint8_t flags = 0) {
  if (should_suppress_low_value_record(type, priority)) {
    return false;
  }
  return enqueue_typed_record(type, payload, len, priority, flags);
}

static bool emit_record(RecordType type, const uint8_t* payload, uint16_t len,
                        uint8_t flags = 0) {
  return emit_record(type, payload, len, csm::board::uplink::default_priority_for_record(type), flags);
}

#if BOARD_ENABLE_STATUS_LED
static void init_status_led() {
  status_led.begin();
}

static void service_status_led() {
  status_led.service(can_backend_ok);
}
#else
static void init_status_led() {}
static void service_status_led() {}
#endif

static void init_runtime_watchdog() {
#if BOARD_ENABLE_RUNTIME_WATCHDOG
  mbed::Watchdog::get_instance().start(BOARD_RUNTIME_WATCHDOG_TIMEOUT_MS);
#endif
}

static void kick_runtime_watchdog() {
#if BOARD_ENABLE_RUNTIME_WATCHDOG
  mbed::Watchdog::get_instance().kick();
#endif
}

static bool emit_board_event_with_priority(uint16_t code, uint16_t detail, uint32_t counter,
                                           UplinkPriority priority) {
  uint8_t payload[16];
  wr_u64_le(&payload[0], mono64_us());
  wr_u16_le(&payload[8], code);
  wr_u16_le(&payload[10], detail);
  wr_u32_le(&payload[12], counter);
  return emit_record(RecordType::BoardEvent, payload, sizeof(payload), priority);
}

static bool emit_board_event(uint16_t code, uint16_t detail, uint32_t counter) {
  return emit_board_event_with_priority(
      code, detail, counter, csm::board::uplink::priority_for_board_event(code));
}

static void note_pending_can_segment_enqueue_fail(uint32_t frames) {
  const uint32_t remaining = UINT32_MAX - pending_can_segment_enqueue_fail_frames;
  pending_can_segment_enqueue_fail_frames += frames > remaining ? remaining : frames;
}

static void service_deferred_loss_events() {
  if (pending_can_segment_enqueue_fail_frames == 0) {
    return;
  }
  if (!uplink_scheduler.hasQueueSpace(UplinkPriority::Critical)) {
    return;
  }

  const uint16_t detail = pending_can_segment_enqueue_fail_frames > 0xFFFFu
                              ? 0xFFFFu
                              : static_cast<uint16_t>(pending_can_segment_enqueue_fail_frames);
  if (emit_board_event_with_priority(EventCanRxSegmentEnqueueFailed, detail,
                                     can_segment_enqueue_fail_total,
                                     UplinkPriority::Critical)) {
    pending_can_segment_enqueue_fail_frames = 0;
  }
}

using csm::ControlAckAccepted;
using csm::ControlAckRejected;
using csm::ControlReasonBadBus;
using csm::ControlReasonBadLength;
using csm::ControlReasonBadProtocol;
using csm::ControlReasonCanNotReady;
using csm::ControlReasonCanWriteFailed;
using csm::ControlReasonControlLeaseExpired;
using csm::ControlReasonDlcOutOfRange;
using csm::ControlReasonEstopAsserted;
using csm::ControlReasonFieldPowerLost;
using csm::ControlReasonIdNotAllowed;
using csm::ControlReasonHostTimeout;
using csm::ControlReasonNotArmed;
using csm::ControlReasonOk;
using csm::ControlReasonQueueFull;
using csm::ControlReasonSafetyLockout;
using csm::ControlReasonUnsupportedFrame;
using csm::ControlReasonUnsupportedCommand;

static void __attribute__((unused)) emit_control_ack(uint32_t command_id, uint8_t status,
                                                     uint8_t reason, uint8_t bus,
                                                     uint32_t can_id_flags, uint8_t dlc,
                                                     uint32_t counter) {
  uint8_t payload[28];
  memset(payload, 0, sizeof(payload));
  wr_u64_le(&payload[0], mono64_us());
  wr_u32_le(&payload[8], command_id);
  payload[12] = status;
  payload[13] = reason;
  payload[14] = bus;
  payload[15] = dlc & 0x0F;
  wr_u32_le(&payload[16], can_id_flags);
  wr_u32_le(&payload[20], counter);
  wr_u32_le(&payload[24], host_can_tx_rejected_total);
  emit_record(RecordType::ControlAck, payload, sizeof(payload));
}

#if BOARD_HW_PROFILE_MID_MCP2515 || BOARD_HW_PROFILE_MID_TJA1051_DUAL
static csm::board::CapabilityBusDescriptor make_capability_bus_descriptor(
    uint8_t bus_id, uint8_t role, uint8_t backend, uint8_t transceiver,
    uint8_t rx_supported, uint8_t tx_supported, uint8_t control_tx_allowed,
    uint8_t termination_policy, uint8_t isolation_policy) {
  csm::board::CapabilityBusDescriptor bus;
  bus.bus_id = bus_id;
  bus.role = role;
  bus.backend = backend;
  bus.transceiver = transceiver;
  bus.rx_supported = rx_supported;
  bus.tx_supported = tx_supported;
  bus.control_tx_allowed = control_tx_allowed;
  bus.termination_policy = termination_policy;
  bus.isolation_policy = isolation_policy;
  return bus;
}
#endif

static void emit_capability() {
  csm::board::CapabilityPayloadConfig config;
#if BOARD_HW_PROFILE_MID_MCP2515
  config.profile_major = 3;   // Mid Carrier + external MCP2515 current CSM
#elif BOARD_HW_PROFILE_MID_TJA1051_DUAL
  config.profile_major = 2;   // Mid Carrier + TJA1051 target
#else
  config.profile_major = 1;
#endif
  config.profile_minor = 0;
  config.can_queue_size = kCanQueueSize;
  config.encoder_ppr = 2048;
  config.encoder_frequency_limit_hz = 300000;
  config.adc_sample_supported = BOARD_ENABLE_VOLTAGE_ADC;
  config.adc_channel_count = BOARD_ENABLE_VOLTAGE_ADC ? kVoltageChannelCount : 0;
  config.adc_resolution_bits = BOARD_ENABLE_VOLTAGE_ADC ? kVoltageAdcBits : 0;
  config.adc_sample_period_ms = static_cast<uint8_t>(BOARD_VOLTAGE_SAMPLE_PERIOD_MS & 0xFF);

  uint8_t lane_flags = 0;
#if BOARD_ENABLE_MCP2515
  lane_flags |= (1u << 0);
  lane_flags |= (BOARD_CAN_IRQ_MODE != 0) ? (1u << 4) : 0;
#endif
#if BOARD_ENABLE_BUILTIN_CAN_RX
  lane_flags |= (1u << 1);
#endif
#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_BUILTIN
  lane_flags |= (1u << 2);
#endif
#if BOARD_ENABLE_HOST_CAN_TX_BUILTIN
  lane_flags |= (1u << 3);
#endif
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  lane_flags |= (1u << 6);
#endif
#if BOARD_ENABLE_HOST_CAN_TX_MCP2515
  lane_flags |= (1u << 7);
#endif
#if BOARD_ENABLE_VOLTAGE_ADC
  lane_flags |= (1u << 5);
#endif
  config.lane_capability_flags = lane_flags;

  uint8_t limitation_flags = 0;
#if BOARD_ENABLE_BUILTIN_CAN_LANE
  limitation_flags |= (1u << 0);
#endif
#if BOARD_ENABLE_HOST_CAN_TX_ANY
  limitation_flags |= (1u << 1);
#endif
#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_MCP2515_TX_TEST
  limitation_flags |= (1u << 2);
#endif
  config.limitation_flags = limitation_flags;

#if BOARD_HW_PROFILE_MID_MCP2515 || BOARD_HW_PROFILE_MID_TJA1051_DUAL
  config.include_v2 = true;
  config.include_v3 = true;
#if BOARD_HW_PROFILE_MID_MCP2515
  config.bus_count = BOARD_ENABLE_BUILTIN_CAN_LANE ? 2 : 1;
#else
  config.bus_count = 2;
#endif
  config.capability_v2_flags = 0x0003;
  config.supported_uplink_records =
      (1u << static_cast<uint8_t>(RecordType::CanRxRaw)) |
      (1u << static_cast<uint8_t>(RecordType::AdcSample)) |
      (1u << static_cast<uint8_t>(RecordType::BoardEvent)) |
      (1u << static_cast<uint8_t>(RecordType::BoardHealth)) |
      (1u << static_cast<uint8_t>(RecordType::Capability)) |
      (1u << static_cast<uint8_t>(RecordType::CanRxSegment));
#if BOARD_ENABLE_HOST_CAN_TX_ANY || BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_BUILTIN_CAN_TX_TEST
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::CanTxRaw)) |
      (1u << static_cast<uint8_t>(RecordType::ControlAck));
#endif
#if BOARD_ENABLE_HOST_DOWNLINK
  config.supported_downlink_records =
      (1u << static_cast<uint8_t>(RecordType::HostCanTxRequest)) |
      (1u << static_cast<uint8_t>(RecordType::HostHeartbeat)) |
      (1u << static_cast<uint8_t>(RecordType::HostControlSession)) |
      (1u << static_cast<uint8_t>(RecordType::HostQueryCapability)) |
      (1u << static_cast<uint8_t>(RecordType::HostClearFaultLockout));
#else
  config.supported_downlink_records = 0;
#endif
  config.safety_feature_flags = 0x0000000Fu;
  config.host_tx_queue_size = BOARD_ENABLE_HOST_CAN_TX_ANY ? 32 : 0;
  config.capability_v3_flags = 0x0001;
  config.include_v4 = true;
  config.include_v5 = true;
  config.include_v6 = true;
  config.firmware_build_id = CSM_FW_BUILD_ID;
  config.firmware_identity_version = 1;
  config.firmware_dirty = CSM_FW_GIT_DIRTY != 0;
  config.firmware_irq_mode = BOARD_CAN_IRQ_MODE;
  config.firmware_build_epoch = CSM_FW_BUILD_EPOCH;
#if BOARD_ENABLE_MCP2515
  config.mcp_spi_hz = kMcp2515SpiHz;
#else
  config.mcp_spi_hz = 0;
#endif
  config.can_record_drain_budget = BOARD_CAN_SERIAL_DRAIN_BUDGET;
  config.serial_ring_kib = static_cast<uint16_t>(kSerialTxRingSize / 1024);
  config.firmware_git_sha = CSM_FW_GIT_SHA;
  config.firmware_env_name = CSM_FW_ENV_NAME;
  config.firmware_profile = firmware_profile_id();
  config.profile_lock_state = kProfileLockCompileTime;
  config.vehicle_impact_state = vehicle_impact_state();
  config.host_command_rx = BOARD_ENABLE_HOST_DOWNLINK ? 1 : 0;
  config.control_path = BOARD_ENABLE_HOST_CAN_TX_ANY ? 1 : 0;
  config.usb_backpressure_isolated = 1;
  config.dtr_reset_sensitive = BOARD_USB_CDC_RECONNECT_RESET_MS != 0 ? 1 : 0;
  config.passive_acceptance_allowed = passive_acceptance_allowed();
  config.usb_cdc_dtr_session_required = BOARD_USB_CDC_DTR_SESSION_REQUIRED ? 1 : 0;
  config.usb_cdc_dtr_session_only = BOARD_USB_CDC_DTR_SESSION_ONLY ? 1 : 0;
  config.hardware_safety_case_id = BOARD_PASSIVE_HARDWARE_SAFETY_CASE_ID;
  config.bench_verification_id = BOARD_PASSIVE_BENCH_VERIFICATION_ID;
  config.hardware_silent_strapped[0] = BOARD_PASSIVE_HARDWARE_SILENT_STRAPPED ? 1 : 0;
  config.hardware_silent_strapped[1] = BOARD_PASSIVE_HARDWARE_SILENT_STRAPPED ? 1 : 0;
  config.galvanic_isolated[0] = BOARD_PASSIVE_GALVANIC_ISOLATED ? 1 : 0;
  config.galvanic_isolated[1] = BOARD_PASSIVE_GALVANIC_ISOLATED ? 1 : 0;
  config.power_off_passive[0] = BOARD_PASSIVE_POWER_OFF_PASSIVE ? 1 : 0;
  config.power_off_passive[1] = BOARD_PASSIVE_POWER_OFF_PASSIVE ? 1 : 0;
  config.reset_safe[0] = BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE ? 1 : 0;
  config.reset_safe[1] = BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE ? 1 : 0;
  config.txd_gated[0] = BOARD_PASSIVE_TXD_GATED ? 1 : 0;
  config.txd_gated[1] = BOARD_PASSIVE_TXD_GATED ? 1 : 0;
  config.normal_enable_path_populated[0] = BOARD_PASSIVE_NORMAL_ENABLE_PATH_POPULATED ? 1 : 0;
  config.normal_enable_path_populated[1] = BOARD_PASSIVE_NORMAL_ENABLE_PATH_POPULATED ? 1 : 0;
  config.field_sku_id = BOARD_PASSIVE_FIELD_SKU_ID;
  config.external_analyzer_artifact_id = BOARD_PASSIVE_EXTERNAL_ANALYZER_ARTIFACT_ID;
  config.hotplug_pass_count = BOARD_PASSIVE_HOTPLUG_PASS_COUNT;
  config.host_session_epoch = host_session_epoch;
  config.transport_epoch = transport_epoch;
  config.usb_attach_quarantine_total = usb_attach_quarantine_total;
  config.host_absent_gap_total = host_absent_gap_total;
  config.pre_session_payload_replay_total = pre_session_payload_replay_total;
#if BOARD_TARGET_INTERNAL_CAN_LANE0 && !BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND
  config.capability_v2_flags |= (1u << 2);
#endif

#if BOARD_HW_PROFILE_MID_MCP2515
#if BOARD_ENABLE_MCP2515
  const uint8_t mcp_runtime_ready = (can_backend_ok && mcp2515 != nullptr) ? 1 : 0;
#else
  const uint8_t mcp_runtime_ready = 0;
#endif
  const uint8_t mcp_tx_runtime_supported =
      (mcp_runtime_ready && (BOARD_ENABLE_HOST_CAN_TX_MCP2515 || BOARD_ENABLE_MCP2515_TX_TEST)) ? 1 : 0;
  const uint8_t mcp_control_runtime_allowed =
      (mcp_tx_runtime_supported && BOARD_MCP2515_CONTROL_TX_ALLOWED) ? 1 : 0;
  config.buses[0] = make_capability_bus_descriptor(
      BOARD_MCP2515_BUS_ID,
      0,
      1,
      1,
      mcp_runtime_ready,
      mcp_tx_runtime_supported,
      mcp_control_runtime_allowed,
      BOARD_MCP2515_CAPABILITY_TERMINATION_POLICY,
      BOARD_MCP2515_CAPABILITY_ISOLATION_POLICY);
  config.bus_mode[0] = BOARD_PRODUCT_ACK_OBSERVE_MODE ? kBusModeNormal :
      (BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT ? kBusModeListenOnly : kBusModeNormal);
  config.bus_ack_capability[0] = BOARD_PRODUCT_ACK_OBSERVE_MODE ? 1 :
      (BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT ? 0 : 1);
  config.bus_error_frame_capability[0] = config.bus_ack_capability[0];
  config.bus_transceiver_reset_safe[0] = BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE ? 1 : 0;
#if BOARD_ENABLE_BUILTIN_CAN_LANE
  config.buses[1] = make_capability_bus_descriptor(
      BOARD_BUILTIN_CAN_BUS_ID,
      0,
      2,
      3,
      BOARD_ENABLE_BUILTIN_CAN_RX ? 1 : 0,
      (BOARD_ENABLE_HOST_CAN_TX_BUILTIN || BOARD_ENABLE_BUILTIN_CAN_TX_TEST) ? 1 : 0,
      BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED ? 1 : 0,
      0,
      0);
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  config.bus_mode[1] = BOARD_PRODUCT_ACK_OBSERVE_MODE ? kBusModeNormal : kBusModeListenOnly;
  config.bus_ack_capability[1] = BOARD_PRODUCT_ACK_OBSERVE_MODE ? 1 : 0;
  config.bus_error_frame_capability[1] = config.bus_ack_capability[1];
#else
  config.bus_mode[1] = kBusModeNormal;
  config.bus_ack_capability[1] = 1;
  config.bus_error_frame_capability[1] = 1;
#endif
  config.bus_transceiver_reset_safe[1] = BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE ? 1 : 0;
#endif
#else
  config.buses[0] = make_capability_bus_descriptor(
      0,
      0,
#if BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND
      3,
      2,
      1,
      1,
#else
      4,
      2,
      0,
      0,
#endif
      0,
      2,
      1);
  config.bus_mode[0] =
#if BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND
      kBusModeNormal;
#else
      kBusModeHardwareSilent;
#endif
  config.bus_ack_capability[0] = BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND ? 1 : 0;
  config.bus_error_frame_capability[0] = BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND ? 1 : 0;
  config.bus_transceiver_reset_safe[0] = BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE ? 1 : 0;

  config.buses[1] = make_capability_bus_descriptor(
      BOARD_BUILTIN_CAN_BUS_ID,
      0,
      2,
      3,
      BOARD_ENABLE_BUILTIN_CAN_RX ? 1 : 0,
      (BOARD_ENABLE_HOST_CAN_TX || BOARD_ENABLE_BUILTIN_CAN_TX_TEST) ? 1 : 0,
      BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED ? 1 : 0,
      0,
      0);
  config.bus_mode[1] = BOARD_ENABLE_BUILTIN_CAN_RX ? kBusModeNormal : kBusModeHardwareSilent;
  config.bus_ack_capability[1] = BOARD_ENABLE_BUILTIN_CAN_RX ? 1 : 0;
  config.bus_error_frame_capability[1] = BOARD_ENABLE_BUILTIN_CAN_RX ? 1 : 0;
  config.bus_transceiver_reset_safe[1] = BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE ? 1 : 0;
#endif
#endif

  uint8_t payload[kCapabilityV6PayloadLen];
  const uint16_t payload_len = csm::board::build_capability_payload(config, payload, sizeof(payload));
  if (payload_len > 0) {
    emit_record(RecordType::Capability, payload, payload_len);
  }
}

static void service_capability_advertisement() {
  const uint32_t now_ms = millis();
  if (!uplink_host_session_open()) {
    last_capability_ms = now_ms;
    return;
  }
  if (now_ms - last_capability_ms < BOARD_CAPABILITY_PERIOD_MS) {
    return;
  }
  last_capability_ms = now_ms;
  emit_capability();
}

static int8_t can_bus_runtime_index(uint8_t bus) {
  if (bus == BOARD_MCP2515_BUS_ID) {
    return 0;
  }
  if (bus == BOARD_BUILTIN_CAN_BUS_ID) {
    return 1;
  }
  return -1;
}

static void note_can_queue_push(uint8_t bus) {
  can_rx_count_total++;
  const int8_t index = can_bus_runtime_index(bus);
  if (index < 0) {
    return;
  }
  BusRxRuntime& runtime = can_bus_runtime[index];
  runtime.rx_total++;
  runtime.queued++;
  if (runtime.queued > runtime.high_water) {
    runtime.high_water = runtime.queued;
  }
}

static void note_can_queue_drop(uint8_t bus) {
  can_rx_dropped_total++;
  const int8_t index = can_bus_runtime_index(bus);
  if (index >= 0) {
    can_bus_runtime[index].drop_total++;
  }
}

static void note_can_queue_pop(uint8_t bus) {
  const int8_t index = can_bus_runtime_index(bus);
  if (index >= 0 && can_bus_runtime[index].queued > 0) {
    can_bus_runtime[index].queued--;
  }
}

static bool can_queue_push(const CanRxItem& item) {
  const int8_t index = can_bus_runtime_index(item.bus);
  if (index < 0) {
    note_can_queue_drop(item.bus);
    return false;
  }
  volatile uint32_t& head_ref = can_q_head[index];
  volatile uint32_t& tail_ref = can_q_tail[index];
  CanRxItem* queue = (index == 0) ? can_queue_bus0 : can_queue_bus1;
  const uint32_t head = head_ref;
  const uint32_t next = (head + 1) & kCanQueueMask;
  if (next == tail_ref) {
    note_can_queue_drop(item.bus);
    if ((can_rx_dropped_total & 0xFF) == 1) {
      emit_board_event(EventCanRxQueueDrop, item.bus, can_rx_dropped_total);
    }
    return false;
  }
  queue[head] = item;
  head_ref = next;
  const uint32_t total_queued = can_bus_runtime[0].queued + can_bus_runtime[1].queued + 1;
  if (total_queued > can_queue_high_water) {
    can_queue_high_water = total_queued;
  }
  note_can_queue_push(item.bus);
  return true;
}

static bool can_queue_pop(CanRxItem& out) {
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    const uint8_t index = static_cast<uint8_t>((can_queue_pop_next_index + attempt) & 0x01u);
    volatile uint32_t& head_ref = can_q_head[index];
    volatile uint32_t& tail_ref = can_q_tail[index];
    const uint32_t tail = tail_ref;
    if (tail == head_ref) {
      continue;
    }
    CanRxItem* queue = (index == 0) ? can_queue_bus0 : can_queue_bus1;
    out = queue[tail];
    tail_ref = (tail + 1) & kCanQueueMask;
    can_queue_pop_next_index = static_cast<uint8_t>((index + 1u) & 0x01u);
    note_can_queue_pop(out.bus);
    return true;
  }
  return false;
}

static void discard_can_queue_for_session_quarantine() {
  for (uint8_t index = 0; index < 2; ++index) {
    volatile uint32_t& head_ref = can_q_head[index];
    volatile uint32_t& tail_ref = can_q_tail[index];
    const uint32_t head = head_ref;
    const uint32_t tail = tail_ref;
    const uint32_t pending = (head - tail) & kCanQueueMask;
    if (pending == 0) {
      continue;
    }
    tail_ref = head;
    can_bus_runtime[index].queued = 0;
    host_absent_rx_discard_total[index] += pending;
  }
  can_queue_pop_next_index = 0;
}

static uint8_t read_ab_state() {
#if BOARD_ENABLE_ENCODER_IO
  const uint8_t a = digitalRead(BoardPins::EncoderA) ? 1 : 0;
  const uint8_t b = digitalRead(BoardPins::EncoderB) ? 1 : 0;
  return static_cast<uint8_t>((b << 1) | a);
#else
  return 0;
#endif
}

static uint32_t read_fault_flags() {
  uint32_t flags = 0;
#if BOARD_ENABLE_SAFETY_IO
  if (!digitalRead(BoardPins::EncoderFaultN)) {
    flags |= (1u << 0);
  }
  if (!digitalRead(BoardPins::FieldPowerOk)) {
    flags |= (1u << 1);
  }
  if (!digitalRead(BoardPins::EstopInN)) {
    flags |= (1u << 2);
  }
#endif
  return flags;
}

void on_encoder_index() {
  encoder_index_mono_us = mono64_us();
  encoder_index_count++;
  encoder_index_pending = true;
}

#if BOARD_ENABLE_MCP2515 && (BOARD_CAN_IRQ_MODE == 2)
void on_mcp2515_int() {
  mcp2515_irq_edges++;
  mcp2515_irq_pending = true;
}
#endif

static bool init_encoder_timer3() {
#if BOARD_ENABLE_TIM3_ENCODER
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(GPIOC, &gpio);

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 0xFFFF;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  TIM_Encoder_InitTypeDef enc = {};
  enc.EncoderMode = TIM_ENCODERMODE_TI12;
  enc.IC1Polarity = TIM_ICPOLARITY_RISING;
  enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  enc.IC1Prescaler = TIM_ICPSC_DIV1;
  enc.IC1Filter = 4;
  enc.IC2Polarity = TIM_ICPOLARITY_RISING;
  enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  enc.IC2Prescaler = TIM_ICPSC_DIV1;
  enc.IC2Filter = 4;

  if (HAL_TIM_Encoder_Init(&htim3, &enc) != HAL_OK) {
    return false;
  }
  if (HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL) != HAL_OK) {
    return false;
  }
  encoder_last_count = static_cast<uint16_t>(__HAL_TIM_GET_COUNTER(&htim3));
  return true;
#else
  encoder_last_count = 0;
  return false;
#endif
}

static EncoderSnapshot poll_encoder() {
  EncoderSnapshot snap;
  snap.mono_us = mono64_us();
  snap.tim3_count = encoder_timer_ok ? static_cast<uint16_t>(__HAL_TIM_GET_COUNTER(&htim3)) : 0;
  const int16_t delta = static_cast<int16_t>(snap.tim3_count - encoder_last_count);
  encoder_last_count = snap.tim3_count;
  encoder_position += delta;
  if (delta > 30000 || delta < -30000) {
    encoder_wrap_events++;
  }
  snap.position = encoder_position;
  snap.ab_state = read_ab_state();
  snap.flags = 0;
  snap.fault_flags = read_fault_flags();
  return snap;
}

#if BOARD_ENABLE_ENCODER_IO
static void emit_encoder_edge(const EncoderSnapshot& snap, uint8_t flags) {
  uint8_t payload[28];
  wr_u64_le(&payload[0], snap.mono_us);
  wr_i64_le(&payload[8], snap.position);
  wr_u16_le(&payload[16], snap.tim3_count);
  payload[18] = snap.ab_state;
  payload[19] = flags;
  wr_u32_le(&payload[20], snap.fault_flags);
  wr_u32_le(&payload[24], encoder_index_count);
  emit_record(RecordType::EncEdgeRaw, payload, sizeof(payload));
}

static void emit_encoder_derived(const EncoderSnapshot& snap, int32_t velocity_counts_per_s) {
  uint8_t payload[28];
  wr_u64_le(&payload[0], snap.mono_us);
  wr_i64_le(&payload[8], snap.position);
  wr_i32_le(&payload[16], velocity_counts_per_s);
  wr_u16_le(&payload[20], snap.tim3_count);
  payload[22] = snap.ab_state;
  payload[23] = snap.fault_flags ? 1 : 0;
  wr_u32_le(&payload[24], snap.fault_flags);
  emit_record(RecordType::EncDerived, payload, sizeof(payload));
}
#endif

static void __attribute__((unused)) emit_can_rx_raw(const CanRxItem& item) {
  uint8_t payload[30];
  wr_u64_le(&payload[0], item.mono_us);
  wr_u32_le(&payload[8], item.can_id_flags);
  payload[12] = item.dlc_flags;
  payload[13] = item.bus;
  memcpy(&payload[14], item.data, 8);
  wr_u32_le(&payload[22], can_rx_count_total);
  wr_u32_le(&payload[26], can_rx_dropped_total);
  emit_record(RecordType::CanRxRaw, payload, sizeof(payload));
}

static bool emit_can_rx_segment_payload(const CanRxItem* items, uint8_t count, uint64_t segment_seq) {
  if (items == nullptr || count == 0) {
    return true;
  }
  if (!uplink_host_session_open()) {
    return true;
  }
  if (count > kCanRxSegmentMaxFrames) {
    count = kCanRxSegmentMaxFrames;
  }

  uint8_t payload[kCanRxSegmentHeaderLen + kCanRxSegmentEntryLen * kCanRxSegmentMaxFrames];
  memset(payload, 0, sizeof(payload));
  wr_u64_le(&payload[0], segment_seq);
  wr_u64_le(&payload[8], items[0].capture_seq);
  wr_u16_le(&payload[16], count);
  payload[18] = kCanRxSegmentEntryLen;
  payload[19] = 0x01;
  wr_u32_le(&payload[20], can_rx_dropped_total);
  wr_u32_le(&payload[24], can_fifo_overflow_total);

  for (uint8_t index = 0; index < count; ++index) {
    const CanRxItem& item = items[index];
    uint8_t* entry = &payload[kCanRxSegmentHeaderLen + index * kCanRxSegmentEntryLen];
    wr_u64_le(&entry[0], item.capture_seq);
    wr_u64_le(&entry[8], item.mono_us);
    wr_u32_le(&entry[16], item.can_id_flags);
    entry[20] = item.dlc_flags;
    entry[21] = item.bus;
    memcpy(&entry[22], item.data, 8);
  }

  const uint16_t payload_len = static_cast<uint16_t>(kCanRxSegmentHeaderLen + count * kCanRxSegmentEntryLen);
  if (enqueue_typed_record(RecordType::CanRxSegment, payload, payload_len, UplinkPriority::CanTruth)) {
    return true;
  }

  can_segment_enqueue_fail_total += count;
  note_pending_can_segment_enqueue_fail(count);
  return false;
}

static bool emit_can_rx_segment_callback(void*, const CanRxSegmentItem* items,
                                         uint8_t count, uint64_t segment_seq) {
  return emit_can_rx_segment_payload(items, count, segment_seq);
}

static void __attribute__((unused)) emit_can_tx_raw(uint8_t bus, uint32_t can_id_flags, uint8_t dlc,
                                                    const uint8_t* data, uint32_t tx_total,
                                                    uint32_t tx_failed_total) {
  uint8_t payload[30];
  memset(payload, 0, sizeof(payload));
  wr_u64_le(&payload[0], mono64_us());
  wr_u32_le(&payload[8], can_id_flags);
  payload[12] = dlc & 0x0F;
  payload[13] = bus;
  if (data != nullptr) {
    memcpy(&payload[14], data, dlc > 8 ? 8 : dlc);
  }
  wr_u32_le(&payload[22], tx_total);
  wr_u32_le(&payload[26], tx_failed_total);
  emit_record(RecordType::CanTxRaw, payload, sizeof(payload));
}

static void emit_board_health(const EncoderSnapshot& snap) {
  const uint32_t queued0 = (can_q_head[0] - can_q_tail[0]) & kCanQueueMask;
  const uint32_t queued1 = (can_q_head[1] - can_q_tail[1]) & kCanQueueMask;
  const uint32_t queued = queued0 + queued1;
  const csm::board::uplink::UplinkCounters& uplink_counters = uplink_scheduler.counters();
  const csm::board::uplink::SerialTxCounters& serial_counters = serial_tx_scheduler.counters();

  uint8_t inputs = 0;
#if BOARD_ENABLE_SAFETY_IO
  inputs |= digitalRead(BoardPins::EstopInN) ? 0 : (1u << 0);
  inputs |= digitalRead(BoardPins::FieldPowerOk) ? (1u << 1) : 0;
  inputs |= digitalRead(BoardPins::EncoderFaultN) ? 0 : (1u << 2);
  inputs |= digitalRead(BoardPins::ArmKeyIn) ? (1u << 3) : 0;
#endif

  uint8_t payload[kBoardHealthV7PayloadLen];
  memset(payload, 0, sizeof(payload));
  wr_u64_le(&payload[0], mono64_us());
  wr_u32_le(&payload[8], can_rx_count_total);
  wr_u32_le(&payload[12], can_rx_dropped_total);
  wr_u32_le(&payload[16], can_fifo_overflow_total);
  wr_u32_le(&payload[20], uplink_counters.record_tx_total);
  wr_u32_le(&payload[24], queued);
  wr_u32_le(&payload[28], encoder_fault_events);
  wr_u32_le(&payload[32], encoder_wrap_events);
  wr_i64_le(&payload[36], snap.position);
  payload[44] = static_cast<uint8_t>(safety_state);
  payload[45] = inputs;
  payload[46] = encoder_timer_ok ? 1 : 0;
  payload[47] = 0;
  payload[47] |= kTestMode ? (1u << 0) : 0;
  payload[47] |= can_backend_ok ? (1u << 1) : 0;
#if BOARD_ENABLE_BUILTIN_CAN_LANE
  payload[47] |= builtin_can_tx_ok ? (1u << 2) : 0;
#endif
#if BOARD_ENABLE_MCP2515
  payload[47] |= (BOARD_CAN_IRQ_MODE != 0) ? (1u << 3) : 0;
  payload[47] |= (BOARD_CAN_IRQ_MODE == 2) ? (1u << 4) : 0;
#endif
#if BOARD_ENABLE_VOLTAGE_ADC
  payload[47] |= voltage_adc_ok ? (1u << 5) : 0;
#endif
  payload[47] |= (serial_tx_scheduler.queuedBytes() + uplink_scheduler.queuedPayloadBytes()) > 0
                     ? (1u << 6)
                     : 0;
  payload[47] |= uplink_counters.record_drop_total > 0 ? (1u << 7) : 0;
  wr_u32_le(&payload[48], snap.fault_flags);
  payload[52] = 7;  // BOARD_HEALTH payload version.
  payload[53] = static_cast<uint8_t>(sizeof(payload));
  payload[54] = static_cast<uint8_t>(safety_supervisor.state());
  payload[55] = safety_supervisor.faultBits();
  wr_u32_le(&payload[56], safety_supervisor.heartbeatAgeMs(millis()));
  wr_u32_le(&payload[60], safety_supervisor.leaseRemainingMs(millis()));
  wr_u32_le(&payload[64], host_frame_crc_failed_total);
  wr_u32_le(&payload[68], host_can_tx_request_total);
  wr_u32_le(&payload[72], host_can_tx_accepted_total);
  wr_u32_le(&payload[76], host_can_tx_rejected_total);
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  wr_u32_le(&payload[80], mcp2515_tx_total);
  wr_u32_le(&payload[84], mcp2515_tx_failed_total);
#endif
#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_BUILTIN
  wr_u32_le(&payload[88], builtin_can_tx_total);
  wr_u32_le(&payload[92], builtin_can_tx_failed_total);
#endif
#if BOARD_ENABLE_MCP2515
  wr_u32_le(&payload[96], mcp_service.spi_error_total);
  wr_u32_le(&payload[100], mcp_service.error_flag_total);
  payload[104] = mcp_service.last_canintf;
  payload[105] = mcp_service.last_eflg;
  payload[106] = mcp_service.last_canctrl;
  payload[107] = mcp_service.last_int_low ? 1 : 0;
#endif
  wr_u32_le(&payload[108], queued);
  wr_u32_le(&payload[112], safety_supervisor.transitionCounter());
  uint32_t backend_flags = 0;
  backend_flags |= can_backend_ok ? (1u << 0) : 0;
#if BOARD_ENABLE_BUILTIN_CAN_LANE
  backend_flags |= builtin_can_tx_ok ? (1u << 1) : 0;
#endif
  wr_u32_le(&payload[116], backend_flags);
  wr_u32_le(&payload[120], host_heartbeat_total);
  wr_u32_le(&payload[124], host_control_session_total);
  wr_u32_le(&payload[128], can_bus_runtime[0].rx_total);
  wr_u32_le(&payload[132], can_bus_runtime[0].drop_total);
  wr_u32_le(&payload[136], can_bus_runtime[0].queued);
  wr_u32_le(&payload[140], can_bus_runtime[0].high_water);
  wr_u32_le(&payload[144], can_bus_runtime[1].rx_total);
  wr_u32_le(&payload[148], can_bus_runtime[1].drop_total);
  wr_u32_le(&payload[152], can_bus_runtime[1].queued);
  wr_u32_le(&payload[156], can_bus_runtime[1].high_water);
  wr_u32_le(&payload[160], serial_counters.enqueue_fail_total);
  wr_u32_le(&payload[164], serial_counters.ring_clear_total);
  wr_u32_le(&payload[168], serial_counters.ring_cleared_bytes_total);
  wr_u32_le(&payload[172], serial_counters.backpressure_total);
  uint32_t uplink_high_water = serial_counters.high_water_bytes;
  if (uplink_scheduler.queuedPayloadHighWaterBytes() > uplink_high_water) {
    uplink_high_water = uplink_scheduler.queuedPayloadHighWaterBytes();
  }
  wr_u32_le(&payload[176], uplink_high_water);
  wr_u32_le(&payload[180], can_queue_high_water);
#if BOARD_ENABLE_MCP2515
  wr_u32_le(&payload[184], mcp_service.drain_time_budget_hit_total);
#endif
  wr_u32_le(&payload[188], can_segment_enqueue_fail_total);
  wr_u32_le(&payload[192], uplink_scheduler.poolLargeUsed());
  wr_u32_le(&payload[196], BOARD_UPLINK_POOL_LARGE_BLOCKS);
  wr_u32_le(&payload[200], uplink_scheduler.poolLargeCanReserveUsed());
  wr_u32_le(&payload[204], uplink_counters.can_truth_queue_high_water);
  wr_u32_le(&payload[208], uplink_counters.pool_alloc_fail_total);
  wr_u32_le(&payload[212], uplink_counters.can_truth_pool_alloc_fail_total);
  wr_u32_le(&payload[216], uplink_counters.descriptor_high_water_total);
  wr_u32_le(&payload[220], uplink_counters.diagnostic_suppressed_total);
  const uint32_t pool_high_bytes =
      uplink_counters.pool_large_used_high_water * csm::kMaxPayloadLen +
      uplink_counters.pool_medium_used_high_water * 128u +
      uplink_counters.pool_small_used_high_water * 64u;
  if (pool_high_bytes > uplink_pool_high_water_bytes) {
    uplink_pool_high_water_bytes = pool_high_bytes;
  }
  wr_u32_le(&payload[224], firmware_profile_id());
  wr_u32_le(&payload[228], vehicle_impact_state());
  wr_u32_le(&payload[232], can_rx_task_max_us);
  wr_u32_le(&payload[236], uplink_pool_high_water_bytes);
  wr_u32_le(&payload[240], uplink_counters.descriptor_high_water_total);
  wr_u32_le(&payload[244], usb_reconnect_count);
  wr_u32_le(&payload[248], usb_forced_reset_count);
  wr_u32_le(&payload[252], passive_violation_latch);
  wr_u32_le(&payload[256], capture_invalid_reason);
  wr_u32_le(&payload[260], host_absent_rx_discard_total[0]);
  wr_u32_le(&payload[264], host_absent_rx_discard_total[1]);
  wr_u32_le(&payload[268], host_absent_fifo_overflow_total);
  wr_u32_le(&payload[272], host_absent_mcp_error_total);
  wr_u32_le(&payload[276], host_absent_duration_ms_total);
  wr_u32_le(&payload[280], passive_readback_total);
  wr_u32_le(&payload[284], passive_readback_violation_total);
  wr_u32_le(&payload[288], txreq_violation_total);
  wr_u32_le(&payload[292], usb_cdc_dtr_change_total);
  emit_record(RecordType::BoardHealth, payload, sizeof(payload));
}

static void note_host_absent_active(uint32_t now_ms) {
  if (host_absent_started_ms == 0) {
    host_absent_started_ms = now_ms == 0 ? 1 : now_ms;
  }
}

static void close_host_absent_interval(uint32_t now_ms) {
  if (host_absent_started_ms == 0) {
    return;
  }
  const uint32_t duration_ms = static_cast<uint32_t>(now_ms - host_absent_started_ms);
  host_absent_last_duration_ms = duration_ms;
  host_absent_duration_ms_total += duration_ms;
  host_absent_started_ms = 0;
  host_absent_summary_pending = true;
  host_absent_gap_total++;
}

static void begin_passive_can_frontend_session_quarantine(uint32_t now_ms) {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  can_frontend_session_ready = false;
  can_frontend_session_arm_pending = true;
  can_frontend_session_arm_after_ms = now_ms + BOARD_PASSIVE_SESSION_CAN_FRONTEND_QUIET_MS;
  last_can_frontend_init_attempt_ms = 0;
  can_frontend_presession_hold_total++;
  set_can_observe_mode_for_session(false, true);
  emit_board_event(EventCanFrontendPresessionHold,
                   static_cast<uint16_t>(BOARD_PASSIVE_SESSION_CAN_FRONTEND_QUIET_MS),
                   can_frontend_presession_hold_total);
#else
  (void)now_ms;
#endif
}

static void enter_passive_can_frontend_fault_hold(uint16_t detail, uint32_t counter) {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  can_frontend_session_ready = false;
  can_frontend_session_arm_pending = false;
  ack_observe_enabled = false;

#if BOARD_ENABLE_SAFETY_IO
  digitalWrite(BoardPins::CanTxEnable, LOW);
#endif

#if BOARD_ENABLE_MCP2515
  if (mcp2515 != nullptr) {
    mcp2515->clearTXInterrupts();
    const MCP2515::ERROR err = mcp2515->setListenOnlyMode();
    if (err == MCP2515::ERROR_OK) {
      mcp2515_listen_only_mode = true;
    } else {
      emit_board_event(EventMcp2515Error, static_cast<uint16_t>(err), 6);
    }
  }
#endif

#if BOARD_ENABLE_BUILTIN_CAN_LANE
  if (builtin_can_tx_ok) {
    builtin_can.monitor(true);
  }
#endif

  emit_board_event(EventCanFrontendFaultHold, detail, counter);
#else
  (void)detail;
  (void)counter;
#endif
}

static bool ensure_passive_can_frontend_session_ready(uint32_t now_ms) {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  if (!uplink_host_session_open()) {
    return false;
  }
  if (can_frontend_session_ready) {
    return true;
  }
  if (!can_frontend_session_arm_pending) {
    begin_passive_can_frontend_session_quarantine(now_ms);
  }
  if (static_cast<int32_t>(now_ms - can_frontend_session_arm_after_ms) < 0) {
    return false;
  }
  if (last_can_frontend_init_attempt_ms != 0 &&
      now_ms - last_can_frontend_init_attempt_ms < BOARD_PASSIVE_SESSION_CAN_FRONTEND_RETRY_MS) {
    return false;
  }
  last_can_frontend_init_attempt_ms = now_ms;

  bool ready = true;
#if BOARD_ENABLE_MCP2515_INIT
  if (!can_backend_ok) {
    can_backend_ok = init_can_backend();
    if (!can_backend_ok) {
      ready = false;
      note_can_init_retry_failure();
    } else {
      reset_can_init_retry_backoff();
    }
  }
#endif
#if BOARD_ENABLE_BUILTIN_CAN_LANE
  if (!builtin_can_tx_ok) {
    builtin_can_tx_ok = init_builtin_can_lane();
    if (!builtin_can_tx_ok) {
      ready = false;
    }
  }
#endif

  if (!ready) {
    enter_passive_can_frontend_fault_hold(0x0001u, can_frontend_session_init_fail_total + 1u);
    can_frontend_session_init_fail_total++;
    emit_board_event(EventCanFrontendSessionInitFailed,
                     static_cast<uint16_t>((can_backend_ok ? 0x0001u : 0u) |
                                           (builtin_can_tx_ok ? 0x0002u : 0u)),
                     can_frontend_session_init_fail_total);
    return false;
  }

  set_can_observe_mode_for_session(true, true);
  can_frontend_session_ready = true;
  can_frontend_session_arm_pending = false;
  can_frontend_session_ready_total++;
  emit_board_event(EventCanFrontendSessionReady,
                   static_cast<uint16_t>((can_backend_ok ? 0x0001u : 0u) |
                                         (builtin_can_tx_ok ? 0x0002u : 0u) |
                                         (ack_observe_enabled ? 0x0004u : 0u)),
                   can_frontend_session_ready_total);
  emit_capability();
  last_capability_ms = now_ms;
  const EncoderSnapshot snap = poll_encoder();
  emit_board_health(snap);
  last_health_ms = now_ms;
  return true;
#else
  (void)now_ms;
  return true;
#endif
}

static void emit_host_absent_summary_if_needed() {
  if (!host_absent_summary_pending) {
    return;
  }
  host_absent_summary_pending = false;
  const uint16_t detail =
      static_cast<uint16_t>((host_absent_last_duration_ms > 0xFFFFu)
                                ? 0xFFFFu
                                : host_absent_last_duration_ms);
  const uint32_t discarded = host_absent_rx_discard_total[0] + host_absent_rx_discard_total[1];
  emit_board_event(EventUsbHostAbsentCanDiscardSummary, detail, discarded);
}

static void service_uplink_session_state() {
  const uint32_t now_ms = millis();
  const bool dtr = usb_cdc_dtr_asserted();
  if (!usb_cdc_dtr_initialized) {
    usb_cdc_dtr_initialized = true;
    usb_cdc_last_dtr = dtr;
  } else if (usb_cdc_last_dtr != dtr) {
    usb_cdc_last_dtr = dtr;
    usb_cdc_dtr_change_total++;
    if (uplink_host_session_open()) {
      emit_board_event(EventUsbCdcDtrChange, dtr ? 1 : 0, usb_cdc_dtr_change_total);
    }
  }

  const bool open = uplink_host_session_open();
  if (!open) {
    if (uplink_session_was_open) {
      set_can_observe_mode_for_session(false);
      can_frontend_session_ready = false;
      can_frontend_session_arm_pending = false;
      uplink_session_was_open = false;
      usb_cdc_session_close_total++;
      usb_cdc_last_session_duration_ms = static_cast<uint32_t>(now_ms - usb_cdc_session_open_ms);
      discard_session_uplink_payloads();
    }
    note_host_absent_active(now_ms);
    return;
  }
  if (uplink_session_was_open) {
    return;
  }

  uplink_session_was_open = true;
  usb_cdc_session_open_total++;
  host_session_epoch++;
  transport_epoch++;
  usb_attach_quarantine_total++;
  usb_cdc_session_open_ms = now_ms;
  close_host_absent_interval(now_ms);
  discard_session_uplink_payloads();
  can_rx_segment_builder.resetForEpoch(0);
  begin_passive_can_frontend_session_quarantine(now_ms);

  emit_capability();
  last_capability_ms = now_ms;

  uint16_t detail = BOARD_USB_CDC_DTR_SESSION_REQUIRED ? 0x0001u : 0x0000u;
  detail |= BOARD_USB_CDC_DTR_SESSION_ONLY ? 0x0002u : 0x0000u;
  emit_board_event(EventUsbCdcSessionOpen, detail, usb_cdc_session_open_total);
  if (usb_cdc_session_close_reported_total != usb_cdc_session_close_total) {
    const uint16_t duration_detail =
        static_cast<uint16_t>(usb_cdc_last_session_duration_ms > 0xFFFFu
                                  ? 0xFFFFu
                                  : usb_cdc_last_session_duration_ms);
    emit_board_event(EventUsbCdcSessionClose, duration_detail, usb_cdc_session_close_total);
    usb_cdc_session_close_reported_total = usb_cdc_session_close_total;
  }
  emit_host_absent_summary_if_needed();

  const EncoderSnapshot snap = poll_encoder();
  emit_board_health(snap);
  last_health_ms = now_ms;
}

static bool init_voltage_adc_lane() {
#if BOARD_ENABLE_VOLTAGE_ADC
  analogReadResolution(kVoltageAdcBits);
  for (uint8_t i = 0; i < kVoltageChannelCount; ++i) {
    pinMode(kVoltagePins[i], INPUT);
    (void)analogRead(kVoltagePins[i]);
  }
  voltage_adc_sample_total = 0;
  voltage_adc_drop_total = 0;
  last_voltage_adc_sample_ms = millis();
  return true;
#else
  return false;
#endif
}

static void service_voltage_adc_lane() {
#if BOARD_ENABLE_VOLTAGE_ADC
  if (!voltage_adc_ok) {
    return;
  }

  const uint32_t now_ms = millis();
  if (now_ms - last_voltage_adc_sample_ms < BOARD_VOLTAGE_SAMPLE_PERIOD_MS) {
    return;
  }
  last_voltage_adc_sample_ms = now_ms;

  uint8_t payload[44];
  memset(payload, 0, sizeof(payload));
  wr_u64_le(&payload[0], mono64_us());
  wr_u32_le(&payload[8], voltage_adc_sample_total);
  wr_u32_le(&payload[12], voltage_adc_drop_total);
  payload[16] = 0;                    // source 0: Portenta lab ADC
  payload[17] = kVoltageChannelCount;
  payload[18] = kVoltageAdcBits;
  payload[19] = 0x03;                 // bit0 raw-valid, bit1 direct MCU ADC

  for (uint8_t i = 0; i < kVoltageChannelCount; ++i) {
    payload[20 + i] = kVoltageChannelIds[i];
    int raw = analogRead(kVoltagePins[i]);
    if (raw < 0) {
      raw = 0;
      voltage_adc_drop_total++;
      payload[19] |= 0x80;
    }
    if (raw > ((1 << kVoltageAdcBits) - 1)) {
      raw = (1 << kVoltageAdcBits) - 1;
      payload[19] |= 0x40;
    }
    wr_u16_le(&payload[28 + (i * 2)], static_cast<uint16_t>(raw));
  }

  voltage_adc_sample_total++;
  emit_record(RecordType::AdcSample, payload, sizeof(payload));
#endif
}

static void init_safety_pins() {
#if BOARD_ENABLE_SAFETY_IO
  pinMode(BoardPins::CanTxEnable, OUTPUT);
  digitalWrite(BoardPins::CanTxEnable, LOW);

  pinMode(BoardPins::SafetyWatchdogToggle, OUTPUT);
  digitalWrite(BoardPins::SafetyWatchdogToggle, LOW);

  pinMode(BoardPins::EstopInN, INPUT_PULLUP);
  pinMode(BoardPins::ArmKeyIn, INPUT);
  pinMode(BoardPins::FieldPowerOk, INPUT_PULLUP);
  pinMode(BoardPins::EncoderFaultN, INPUT_PULLUP);
  pinMode(BoardPins::SpareServiceGpio, INPUT);
#endif
}

static bool should_enable_can_tx_gate_for_test() {
#if BOARD_ENABLE_CAN_TX_GATE_FOR_TEST && BOARD_ENABLE_BUILTIN_CAN_LANE
  return builtin_can_tx_ok;
#else
  return false;
#endif
}

static bool __attribute__((unused)) control_backend_ready_for_bus(uint8_t bus) {
#if BOARD_ENABLE_MCP2515 && BOARD_ENABLE_HOST_CAN_TX_MCP2515
  if (bus == BOARD_MCP2515_BUS_ID) {
    return can_backend_ok && mcp2515 != nullptr;
  }
#endif
#if BOARD_ENABLE_HOST_CAN_TX_BUILTIN
  if (bus == BOARD_BUILTIN_CAN_BUS_ID) {
    return builtin_can_tx_ok;
  }
#endif
  return false;
}

static bool any_control_backend_ready() {
  bool ready = false;
#if BOARD_ENABLE_MCP2515 && BOARD_ENABLE_HOST_CAN_TX_MCP2515
  ready = ready || (can_backend_ok && mcp2515 != nullptr);
#endif
#if BOARD_ENABLE_HOST_CAN_TX_BUILTIN
  ready = ready || builtin_can_tx_ok;
#endif
  return ready;
}

static csm::board::SafetyInputs read_safety_inputs() {
  csm::board::SafetyInputs inputs;
#if BOARD_ENABLE_SAFETY_IO
  inputs.estop_asserted = !digitalRead(BoardPins::EstopInN);
  inputs.field_power_ok = digitalRead(BoardPins::FieldPowerOk);
  inputs.encoder_fault = !digitalRead(BoardPins::EncoderFaultN);
  inputs.arm_key = digitalRead(BoardPins::ArmKeyIn);
#endif
  inputs.control_backend_ready = any_control_backend_ready();
  return inputs;
}

static void update_safety_state() {
#if BOARD_ENABLE_SAFETY_IO
  const csm::board::SafetyInputs inputs = read_safety_inputs();
  const SafetyState before = safety_supervisor.state();
  safety_supervisor.update(millis(), inputs);
  safety_state = safety_supervisor.state();
  const bool rx_transceiver_enable =
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
      (BOARD_CAN_TRANSCEIVER_ENABLE_FOR_RX != 0) && ack_observe_enabled;
#else
      BOARD_CAN_TRANSCEIVER_ENABLE_FOR_RX != 0;
#endif
  digitalWrite(BoardPins::CanTxEnable,
               (rx_transceiver_enable ||
                safety_supervisor.canDriveTxGate() ||
                should_enable_can_tx_gate_for_test()) ? HIGH : LOW);

  if (inputs.estop_asserted && !estop_prev) {
    emit_board_event(EventEstopAsserted, 0, 1);
  }
  if (!inputs.field_power_ok && field_power_prev) {
    emit_board_event(EventFieldPowerLost, 0, 1);
  }
  if (inputs.encoder_fault && !encoder_fault_prev) {
    encoder_fault_events++;
    emit_board_event(EventEncoderFaultAsserted, 0, encoder_fault_events);
  }
  if (before != safety_state) {
    emit_board_event(EventSafetyStateChanged,
                     (static_cast<uint16_t>(before) << 8) | static_cast<uint8_t>(safety_state),
                     safety_supervisor.transitionCounter());
  }

  estop_prev = inputs.estop_asserted;
  field_power_prev = inputs.field_power_ok;
  encoder_fault_prev = inputs.encoder_fault;
#else
  const csm::board::SafetyInputs inputs = read_safety_inputs();
  safety_supervisor.update(millis(), inputs);
  safety_state = safety_supervisor.state();
#endif
}

static void toggle_safety_watchdog_if_needed() {
#if BOARD_ENABLE_SAFETY_IO
  const uint32_t now_ms = millis();
  if (now_ms - last_watchdog_toggle_ms >= 50) {
    digitalWrite(BoardPins::SafetyWatchdogToggle, !digitalRead(BoardPins::SafetyWatchdogToggle));
    last_watchdog_toggle_ms = now_ms;
  }
#endif
}

#if BOARD_ENABLE_MCP2515
static constexpr uint8_t kMcpRegCanctrl = 0x0F;
static constexpr uint8_t kMcpRegCanintf = 0x2C;
static constexpr uint8_t kMcpRegEflg = 0x2D;
static constexpr uint8_t kMcpRegTxb0ctrl = 0x30;
static constexpr uint8_t kMcpRegTxb1ctrl = 0x40;
static constexpr uint8_t kMcpRegTxb2ctrl = 0x50;
static constexpr uint8_t kMcpInstrRead = 0x03;
static constexpr uint8_t kMcpInstrBitModify = 0x05;
static constexpr uint8_t kMcpCanctrlAbat = 0x10;
static constexpr uint8_t kMcpCanctrlOsm = 0x08;
static constexpr uint8_t kMcpTxbAbtf = 0x40;
static constexpr uint8_t kMcpTxbMloa = 0x20;
static constexpr uint8_t kMcpTxbTxerr = 0x10;
static constexpr uint8_t kMcpTxbTxreq = 0x08;

static uint8_t mcp2515_raw_read_register(uint8_t reg) {
  SPI.beginTransaction(SPISettings(kMcp2515SpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  SPI.transfer(kMcpInstrRead);
  SPI.transfer(reg);
  const uint8_t value = SPI.transfer(0x00);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  SPI.endTransaction();
  return value;
}

static void __attribute__((unused)) mcp2515_raw_modify_register(uint8_t reg, uint8_t mask, uint8_t data) {
  SPI.beginTransaction(SPISettings(kMcp2515SpiHz, MSBFIRST, SPI_MODE0));
  digitalWrite(BoardPins::CanSpiCsN, LOW);
  SPI.transfer(kMcpInstrBitModify);
  SPI.transfer(reg);
  SPI.transfer(mask);
  SPI.transfer(data);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
  SPI.endTransaction();
}
#endif

static void __attribute__((unused)) reset_mcp_service_state() {
#if BOARD_ENABLE_MCP2515
  mcp2515_irq_pending = false;
  mcp2515_irq_edges = 0;
  memset(&mcp_service, 0, sizeof(mcp_service));
  mcp_service.int_pin_enabled = BOARD_CAN_IRQ_MODE != 0;
  mcp_service.last_probe_us = micros();
  mcp_service.last_housekeeping_ms = millis();
  mcp_service.last_error_event_ms = millis() - BOARD_CAN_ERROR_EVENT_PERIOD_MS;
#endif
}

static void __attribute__((unused)) emit_mcp_status_event_throttled(uint8_t stage,
                                                                    uint8_t canintf,
                                                                    uint8_t eflg) {
#if BOARD_ENABLE_MCP2515
  const uint32_t now_ms = millis();
  if (now_ms - mcp_service.last_error_event_ms < BOARD_CAN_ERROR_EVENT_PERIOD_MS) {
    return;
  }
  mcp_service.last_error_event_ms = now_ms;
  const uint16_t detail = (static_cast<uint16_t>(stage) << 8) | canintf;
  const uint32_t packed = (static_cast<uint32_t>(eflg) << 24) |
                          (static_cast<uint32_t>(mcp_service.last_canctrl) << 16) |
                          (mcp_service.spi_error_total & 0xFFFFu);
  emit_board_event(EventMcp2515Error, detail, packed);
#else
  (void)stage;
  (void)canintf;
  (void)eflg;
#endif
}

static void __attribute__((unused)) service_mcp2515_status_after_drain(bool force_event) {
#if BOARD_ENABLE_MCP2515
  if (mcp2515 == nullptr) {
    return;
  }

  const uint8_t canintf = mcp2515->getInterrupts();
  const uint8_t eflg = mcp2515->getErrorFlags();
  const uint8_t canctrl = mcp2515->getControlRegister();
  mcp_service.last_canintf = canintf;
  mcp_service.last_eflg = eflg;
  mcp_service.last_canctrl = canctrl;

  // A transient all-ones status read is a bus/SPI status fault, not proof of
  // MCP RX FIFO overflow. Counting it as FIFO loss makes HIL diagnostics lie.
  if (canintf == 0xFFu || eflg == 0xFFu || canctrl == 0xFFu) {
    mcp_service.spi_error_total++;
    emit_mcp_status_event_throttled(7, canintf, eflg);
    return;
  }

  bool error_status = false;
  if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
    can_fifo_overflow_total++;
    mcp_service.error_flag_total++;
    mcp2515->clearRXnOVRFlags();
    mcp2515->clearERRIF();
    error_status = true;
  } else if (canintf & MCP2515::CANINTF_ERRIF) {
    mcp_service.error_flag_total++;
    mcp2515->clearERRIF();
    error_status = true;
  }

  if (canintf & MCP2515::CANINTF_MERRF) {
    mcp_service.error_flag_total++;
    mcp2515->clearMERR();
    error_status = true;
  }

  if (canintf & (MCP2515::CANINTF_TX0IF | MCP2515::CANINTF_TX1IF | MCP2515::CANINTF_TX2IF)) {
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
    if (!mcp2515_pending_tx.active) {
      mcp2515->clearTXInterrupts();
    }
#else
    mcp2515->clearTXInterrupts();
#endif
  }

  const bool rx_irq_left = (canintf & (MCP2515::CANINTF_RX0IF | MCP2515::CANINTF_RX1IF)) != 0;
  const bool int_low = (BOARD_CAN_IRQ_MODE != 0) && !digitalRead(BoardPins::CanIntN);
  mcp_service.last_int_low = int_low;
  mcp2515_irq_pending = int_low || rx_irq_left;

  if (force_event || error_status) {
    emit_mcp_status_event_throttled(6, canintf, eflg);
  }
#else
  (void)force_event;
#endif
}

static void __attribute__((unused)) service_mcp2515_passive_readback_guard(bool force_event = false) {
#if BOARD_ENABLE_MCP2515
  if (!BOARD_CSM_PROFILE_PASSIVE_PRODUCT || mcp2515 == nullptr) {
    return;
  }
  const uint32_t now_ms = millis();
  if (!force_event && static_cast<uint32_t>(now_ms - last_passive_readback_ms) < 250) {
    return;
  }
  last_passive_readback_ms = now_ms;

  const uint8_t canctrl = mcp2515_raw_read_register(kMcpRegCanctrl);
  const uint8_t txb0 = mcp2515_raw_read_register(kMcpRegTxb0ctrl);
  const uint8_t txb1 = mcp2515_raw_read_register(kMcpRegTxb1ctrl);
  const uint8_t txb2 = mcp2515_raw_read_register(kMcpRegTxb2ctrl);
  const uint8_t txreq = ((txb0 & kMcpTxbTxreq) ? 1u : 0u) |
                        ((txb1 & kMcpTxbTxreq) ? 2u : 0u) |
                        ((txb2 & kMcpTxbTxreq) ? 4u : 0u);
  passive_readback_total++;

  if (canctrl == 0xFFu) {
    host_absent_mcp_error_total++;
    mcp_service.spi_error_total++;
    latch_passive_violation(kPassiveViolationMcpReadbackMode);
    emit_board_event(EventMcpPassiveModeReadback, 0xFFu, passive_readback_total);
    enter_passive_can_frontend_fault_hold(0xFF00u, passive_readback_total);
    return;
  }

  const uint8_t mode_bits = canctrl & 0xE0u;
  const bool listen_only = mode_bits == 0x60u;
  const bool normal_mode = mode_bits == 0x00u;
  const bool expected_listen_only =
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
      !ack_observe_enabled;
#else
      BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT != 0;
#endif
  const bool mode_ok = expected_listen_only ? listen_only : normal_mode;
  const uint16_t detail = (static_cast<uint16_t>(canctrl) << 8) | txreq;
  if (!mode_ok) {
    passive_readback_violation_total++;
    latch_passive_violation(kPassiveViolationMcpReadbackMode);
    emit_board_event(EventMcpPassiveModeViolation, detail, passive_readback_violation_total);
    enter_passive_can_frontend_fault_hold(detail, passive_readback_violation_total);
  } else if (force_event) {
    emit_board_event(EventMcpPassiveModeReadback, detail, passive_readback_total);
  }

  if (txreq != 0) {
    txreq_violation_total++;
    latch_passive_violation(kPassiveViolationMcpTxreqSet);
    emit_board_event(EventMcpTxreqViolation, detail, txreq_violation_total);
    enter_passive_can_frontend_fault_hold(detail, txreq_violation_total);
  }
#else
  (void)force_event;
#endif
}

static bool __attribute__((unused)) should_service_mcp2515_rx() {
#if BOARD_ENABLE_MCP2515
  if (BOARD_CAN_IRQ_MODE == 0) {
    return true;
  }

  const uint32_t now_us = micros();
  const bool fallback_due =
      static_cast<uint32_t>(now_us - mcp_service.last_probe_us) >= BOARD_CAN_POLL_FALLBACK_US;
  const bool int_low = !digitalRead(BoardPins::CanIntN);
  if (int_low) {
    mcp_service.int_low_samples++;
  }

  bool edge_seen = false;
#if BOARD_CAN_IRQ_MODE == 2
  noInterrupts();
  const uint32_t edges = mcp2515_irq_edges;
  const bool pending = mcp2515_irq_pending;
  mcp2515_irq_pending = false;
  interrupts();
  edge_seen = pending || edges != mcp_service.last_seen_irq_edges;
  mcp_service.last_seen_irq_edges = edges;
#endif

  if (int_low || edge_seen || fallback_due) {
    mcp_service.last_probe_us = now_us;
    return true;
  }
  return false;
#else
  return false;
#endif
}

static void __attribute__((unused)) service_mcp2515_host_absent_drain(int budget) {
#if BOARD_ENABLE_MCP2515
  if (mcp2515 == nullptr || !should_service_mcp2515_rx()) {
    return;
  }

  bool saw_error = false;
  const uint32_t overflow_before = can_fifo_overflow_total;
  const uint32_t start_us = micros();
  while (budget-- > 0) {
    if (static_cast<uint32_t>(micros() - start_us) > BOARD_MCP2515_RX_DRAIN_TIME_BUDGET_US) {
      mcp_service.drain_time_budget_hit_total++;
      break;
    }
    struct can_frame msg;
    const MCP2515::ERROR err = mcp2515->readMessage(&msg);
    if (err == MCP2515::ERROR_NOMSG) {
      if ((BOARD_CAN_IRQ_MODE != 0) && !digitalRead(BoardPins::CanIntN)) {
        mcp_service.nomsg_while_int_low++;
      }
      break;
    }
    if (err != MCP2515::ERROR_OK) {
      mcp_service.spi_error_total++;
      host_absent_mcp_error_total++;
      saw_error = true;
      break;
    }
    host_absent_rx_discard_total[BOARD_MCP2515_BUS_ID & 0x01u]++;
  }
  note_can_rx_task_elapsed(start_us);
  service_mcp2515_status_after_drain(saw_error);
  const uint32_t overflow_delta = can_fifo_overflow_total - overflow_before;
  if (overflow_delta > 0) {
    host_absent_fifo_overflow_total += overflow_delta;
  }
  service_mcp2515_passive_readback_guard(false);
#else
  (void)budget;
#endif
}

static bool init_can_backend() {
#if BOARD_ENABLE_MCP2515
  reset_mcp_service_state();
  pinMode(BoardPins::CanSpiCsN, OUTPUT);
  digitalWrite(BoardPins::CanSpiCsN, HIGH);
#if BOARD_CAN_IRQ_MODE != 0
  pinMode(BoardPins::CanIntN, INPUT_PULLUP);
#endif

  static MCP2515 can_controller(BoardPins::CanSpiCsN, kMcp2515SpiHz);
  mcp2515 = &can_controller;
  mcp2515_listen_only_mode = false;

  SPI.begin();

  auto emit_spi_snapshot = [&](uint8_t stage) {
    const uint8_t canstat = mcp2515_raw_read_register(0x0E);
    const uint8_t canctrl = mcp2515_raw_read_register(kMcpRegCanctrl);
    const uint8_t canintf = mcp2515_raw_read_register(kMcpRegCanintf);
    const uint8_t eflg = mcp2515_raw_read_register(kMcpRegEflg);
    const uint16_t detail = (static_cast<uint16_t>(stage) << 8) | canstat;
    const uint32_t packed = (static_cast<uint32_t>(canctrl) << 24) |
                            (static_cast<uint32_t>(canintf) << 16) |
                            (static_cast<uint32_t>(eflg) << 8);
    emit_board_event(EventMcp2515SpiSnapshot, detail, packed);
  };

  emit_spi_snapshot(1);
  mcp2515->reset();
  emit_spi_snapshot(2);
  mcp2515->clearTXInterrupts();
  mcp2515->clearERRIF();
  mcp2515->clearMERR();
  mcp2515->clearRXnOVRFlags();

  MCP2515::ERROR err = mcp2515->setBitrate(kMcp2515Bitrate, kMcp2515Clock);
  if (err != MCP2515::ERROR_OK) {
    emit_spi_snapshot(3);
    emit_board_event(EventMcp2515Error, static_cast<uint16_t>(err), 1);
    return false;
  }
  err =
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT || BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT
      mcp2515->setListenOnlyMode();
#else
      (latch_passive_violation(kPassiveViolationMcpNormalMode), mcp2515->setNormalMode());
#endif
#if BOARD_MCP2515_TX_USE_ONESHOT
  if (err == MCP2515::ERROR_OK && !BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT && !BOARD_CSM_PROFILE_PASSIVE_PRODUCT) {
    mcp2515_raw_modify_register(kMcpRegCanctrl, kMcpCanctrlOsm, kMcpCanctrlOsm);
  }
#endif
  if (err != MCP2515::ERROR_OK) {
    emit_spi_snapshot(4);
    emit_board_event(EventMcp2515Error, static_cast<uint16_t>(err), 2);
    return false;
  }
  mcp2515_listen_only_mode = (BOARD_CSM_PROFILE_PASSIVE_PRODUCT || BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT) != 0;
#if BOARD_CAN_IRQ_MODE == 2
  if (!mcp_service.exti_attached) {
    attachInterrupt(digitalPinToInterrupt(BoardPins::CanIntN), on_mcp2515_int, FALLING);
    mcp_service.exti_attached = true;
  }
#endif
#if BOARD_CAN_IRQ_MODE != 0
  mcp_service.last_int_low = !digitalRead(BoardPins::CanIntN);
  mcp2515_irq_pending = mcp_service.last_int_low;
#endif
  emit_spi_snapshot(5);
  service_mcp2515_passive_readback_guard(true);
  return true;
#else
  return false;
#endif
}

static void pump_can_rx_to_queue(int budget) {
#if BOARD_ENABLE_MCP2515
  if (mcp2515 == nullptr) {
    return;
  }
  if (!should_service_mcp2515_rx()) {
    return;
  }

  bool saw_error = false;
  const uint32_t start_us = micros();
  while (budget-- > 0) {
    if (static_cast<uint32_t>(micros() - start_us) > BOARD_MCP2515_RX_DRAIN_TIME_BUDGET_US) {
      mcp_service.drain_time_budget_hit_total++;
      break;
    }
    struct can_frame msg;
    const MCP2515::ERROR err = mcp2515->readMessage(&msg);
    if (err == MCP2515::ERROR_NOMSG) {
      if ((BOARD_CAN_IRQ_MODE != 0) && !digitalRead(BoardPins::CanIntN)) {
        mcp_service.nomsg_while_int_low++;
      }
      break;
    }
    if (err != MCP2515::ERROR_OK) {
      mcp_service.spi_error_total++;
      saw_error = true;
      break;
    }

    CanRxItem item;
    item.capture_seq = can_capture_seq_next++;
    item.mono_us = mono64_us();

    const bool is_ext = (msg.can_id & CAN_EFF_FLAG) != 0;
    const bool is_rtr = (msg.can_id & CAN_RTR_FLAG) != 0;
    const uint32_t id = is_ext ? (msg.can_id & CAN_EFF_MASK) : (msg.can_id & CAN_SFF_MASK);
    uint32_t can_id_flags = id & 0x1FFFFFFF;
    if (is_ext) {
      can_id_flags |= (1u << 29);
    }
    if (is_rtr) {
      can_id_flags |= (1u << 30);
    }
    item.can_id_flags = can_id_flags;

    uint8_t dlc = msg.can_dlc;
    if (dlc > 8) {
      dlc = 8;
    }
    item.dlc_flags = dlc & 0x0F;
    item.bus = BOARD_MCP2515_BUS_ID;
    memset(item.data, 0, sizeof(item.data));
    for (uint8_t i = 0; i < dlc; ++i) {
      item.data[i] = msg.data[i];
    }

    can_queue_push(item);
  }
  note_can_rx_task_elapsed(start_us);
  service_mcp2515_status_after_drain(saw_error);
  service_mcp2515_passive_readback_guard(false);
#else
  (void)budget;
#endif
}

static bool __attribute__((unused)) init_builtin_can_lane() {
#if BOARD_ENABLE_BUILTIN_CAN_LANE
  if (builtin_can.frequency(500000) != 1) {
    emit_board_event(EventBuiltinCanBeginFailed, 0, 1);
    return false;
  }
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  builtin_can.monitor(true);
#else
  builtin_can.monitor(false);
#endif
#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_BUILTIN
  builtin_can_tx_total = 0;
  builtin_can_tx_failed_total = 0;
#endif
#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST
  last_builtin_can_tx_ms = millis() - BOARD_BUILTIN_CAN_TX_PERIOD_MS;
#endif
  return true;
#else
  return false;
#endif
}

static void set_can_observe_mode_for_session(bool enabled, bool force) {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  const bool target_ack_observe = enabled && (BOARD_PRODUCT_ACK_OBSERVE_MODE != 0);
  if (!force && ack_observe_enabled == target_ack_observe) {
    return;
  }

#if BOARD_ENABLE_SAFETY_IO
  if (!target_ack_observe) {
    digitalWrite(BoardPins::CanTxEnable, LOW);
  }
#endif

#if BOARD_ENABLE_MCP2515
  if (mcp2515 != nullptr) {
    const MCP2515::ERROR err = target_ack_observe ? mcp2515->setNormalMode()
                                                  : mcp2515->setListenOnlyMode();
    if (err == MCP2515::ERROR_OK) {
      mcp2515_listen_only_mode = !target_ack_observe;
    } else {
      emit_board_event(EventMcp2515Error, static_cast<uint16_t>(err),
                       target_ack_observe ? 4 : 5);
    }
  }
#endif

#if BOARD_ENABLE_BUILTIN_CAN_LANE
  if (builtin_can_tx_ok) {
    builtin_can.monitor(!target_ack_observe);
  }
#endif

  ack_observe_enabled = target_ack_observe;

#if BOARD_ENABLE_SAFETY_IO
  digitalWrite(BoardPins::CanTxEnable,
               (ack_observe_enabled && (BOARD_CAN_TRANSCEIVER_ENABLE_FOR_RX != 0)) ? HIGH : LOW);
#endif
  emit_board_event(EventTransceiverSafeStateChanged,
                   ack_observe_enabled ? 1u : 0u,
                   usb_attach_quarantine_total);
  service_mcp2515_passive_readback_guard(true);
#else
  (void)enabled;
#endif
}

static void service_builtin_can_rx_to_queue(int budget) {
#if BOARD_ENABLE_BUILTIN_CAN_RX
  if (!builtin_can_tx_ok) {
    return;
  }

  mbed::CANMessage msg;
  while (budget-- > 0 && builtin_can.read(msg) > 0) {
    if (msg.type != CANData) {
      continue;
    }

    CanRxItem item;
    item.capture_seq = can_capture_seq_next++;
    item.mono_us = mono64_us();

    uint32_t can_id_flags = msg.id;
    can_id_flags &= 0x1FFFFFFF;
    if (msg.format == CANExtended) {
      can_id_flags |= (1u << 29);
    }
    item.can_id_flags = can_id_flags;

    uint8_t dlc = msg.len;
    if (dlc > 8) {
      dlc = 8;
    }
    item.dlc_flags = dlc & 0x0F;
    item.bus = BOARD_BUILTIN_CAN_BUS_ID;
    memset(item.data, 0, sizeof(item.data));
    for (uint8_t i = 0; i < dlc; ++i) {
      item.data[i] = msg.data[i];
    }

    can_queue_push(item);
  }
#else
  (void)budget;
#endif
}

static void __attribute__((unused)) service_builtin_can_rx_host_absent_drain(int budget) {
#if BOARD_ENABLE_BUILTIN_CAN_RX
  if (!builtin_can_tx_ok) {
    return;
  }
  mbed::CANMessage msg;
  while (budget-- > 0 && builtin_can.read(msg) > 0) {
    host_absent_rx_discard_total[BOARD_BUILTIN_CAN_BUS_ID & 0x01u]++;
  }
#else
  (void)budget;
#endif
}

static bool __attribute__((unused)) is_allowed_host_can_id(uint32_t can_id, bool extended) {
  if (extended) {
    return false;
  }
  return can_id == kHostCanTxAllowedPrimaryId ||
         (can_id >= kHostCanTxAllowedRangeStart && can_id <= kHostCanTxAllowedRangeEnd);
}

#if BOARD_ENABLE_MCP2515
static uint32_t __attribute__((unused)) mcp2515_tx_queue_count() {
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  return (mcp2515_tx_q_head - mcp2515_tx_q_tail) & kMcp2515TxQueueMask;
#else
  return 0;
#endif
}

static bool __attribute__((unused)) enqueue_mcp2515_tx(uint8_t bus,
                                                       uint32_t can_id_flags,
                                                       uint8_t dlc,
                                                       const uint8_t* data) {
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  const uint32_t next = (mcp2515_tx_q_head + 1) & kMcp2515TxQueueMask;
  if (next == mcp2515_tx_q_tail) {
    mcp2515_tx_queue_full_total++;
    return false;
  }

  Mcp2515PendingTx& item = mcp2515_tx_queue[mcp2515_tx_q_head];
  item.active = true;
  item.start_ms = 0;
  item.bus = bus;
  item.can_id_flags = can_id_flags;
  item.dlc = dlc;
  memset(item.data, 0, sizeof(item.data));
  memcpy(item.data, data, dlc > 8 ? 8 : dlc);
  mcp2515_tx_q_head = next;
  return true;
#else
  (void)bus;
  (void)can_id_flags;
  (void)dlc;
  (void)data;
  return false;
#endif
}

static MCP2515::ERROR __attribute__((unused)) start_mcp2515_tx_audit(uint8_t bus,
                                                                     uint32_t can_id_flags,
                                                                     uint8_t dlc,
                                                                     const uint8_t* data) {
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  if (mcp2515 == nullptr || mcp2515_pending_tx.active) {
    return MCP2515::ERROR_ALLTXBUSY;
  }
#if BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT
  if (mcp2515_listen_only_mode) {
    latch_passive_violation(kPassiveViolationMcpNormalMode);
    const MCP2515::ERROR mode_err = mcp2515->setNormalMode();
    if (mode_err != MCP2515::ERROR_OK) {
      return mode_err;
    }
    mcp2515_listen_only_mode = false;
#if BOARD_MCP2515_TX_USE_ONESHOT
    mcp2515_raw_modify_register(kMcpRegCanctrl, kMcpCanctrlOsm, kMcpCanctrlOsm);
#endif
  }
#endif
  if ((mcp2515_raw_read_register(kMcpRegTxb0ctrl) & kMcpTxbTxreq) != 0) {
    return MCP2515::ERROR_ALLTXBUSY;
  }
  mcp2515_raw_modify_register(kMcpRegTxb0ctrl,
                              kMcpTxbAbtf | kMcpTxbMloa | kMcpTxbTxerr,
                              0);

  const bool extended = (can_id_flags & (1u << 29)) != 0;
  const uint32_t can_id = can_id_flags & 0x1FFFFFFF;

  struct can_frame msg;
  msg.can_id = can_id;
  if (extended) {
    msg.can_id |= CAN_EFF_FLAG;
  }
  msg.can_dlc = dlc;
  memset(msg.data, 0, sizeof(msg.data));
  memcpy(msg.data, data, dlc > 8 ? 8 : dlc);

  mcp2515->clearTXInterrupts();
  mcp2515->clearMERR();
  mcp2515->clearERRIF();

  latch_passive_violation(kPassiveViolationCanTxCalled);
  const MCP2515::ERROR err = mcp2515->sendMessage(MCP2515::TXB0, &msg);
  if (err != MCP2515::ERROR_OK) {
    return err;
  }

  mcp2515_pending_tx.active = true;
  mcp2515_pending_tx.start_ms = millis();
  mcp2515_pending_tx.bus = bus;
  mcp2515_pending_tx.can_id_flags = can_id_flags;
  mcp2515_pending_tx.dlc = dlc;
  memset(mcp2515_pending_tx.data, 0, sizeof(mcp2515_pending_tx.data));
  memcpy(mcp2515_pending_tx.data, data, dlc > 8 ? 8 : dlc);
  return MCP2515::ERROR_OK;
#else
  (void)bus;
  (void)can_id_flags;
  (void)dlc;
  (void)data;
  return MCP2515::ERROR_FAILTX;
#endif
}

static bool __attribute__((unused)) start_next_mcp2515_queued_tx() {
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  if (mcp2515_pending_tx.active || mcp2515 == nullptr || mcp2515_tx_q_tail == mcp2515_tx_q_head) {
    return false;
  }

  const Mcp2515PendingTx& item = mcp2515_tx_queue[mcp2515_tx_q_tail];
  const MCP2515::ERROR err = start_mcp2515_tx_audit(item.bus, item.can_id_flags, item.dlc, item.data);
  if (err == MCP2515::ERROR_OK) {
    mcp2515_tx_q_tail = (mcp2515_tx_q_tail + 1) & kMcp2515TxQueueMask;
    return true;
  }
  if (err == MCP2515::ERROR_ALLTXBUSY) {
    return false;
  }

  mcp2515_tx_q_tail = (mcp2515_tx_q_tail + 1) & kMcp2515TxQueueMask;
  mcp2515_tx_failed_total++;
  emit_board_event(EventMcp2515TxFailed, static_cast<uint16_t>(err), mcp2515_tx_failed_total);
  return false;
#else
  return false;
#endif
}

static void __attribute__((unused)) finish_mcp2515_pending_tx(bool success, uint16_t detail) {
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  if (!mcp2515_pending_tx.active) {
    return;
  }

  if (success) {
    mcp2515_tx_total++;
    emit_can_tx_raw(mcp2515_pending_tx.bus, mcp2515_pending_tx.can_id_flags,
                    mcp2515_pending_tx.dlc, mcp2515_pending_tx.data,
                    mcp2515_tx_total, mcp2515_tx_failed_total);
  } else {
    mcp2515_tx_failed_total++;
    emit_board_event(EventMcp2515TxFailed, detail, mcp2515_tx_failed_total);
  }

  mcp2515_pending_tx.active = false;
  if (mcp2515 != nullptr) {
    mcp2515->clearTXInterrupts();
    mcp2515->clearMERR();
    mcp2515->clearERRIF();
    mcp2515_raw_modify_register(kMcpRegTxb0ctrl,
                                kMcpTxbAbtf | kMcpTxbMloa | kMcpTxbTxerr,
                                0);
  }
#else
  (void)success;
  (void)detail;
#endif
}

static void service_mcp2515_tx_audit() {
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  if (!mcp2515_pending_tx.active || mcp2515 == nullptr) {
    start_next_mcp2515_queued_tx();
    return;
  }

  const uint8_t canintf = mcp2515_raw_read_register(kMcpRegCanintf);
  const uint8_t ctrl = mcp2515_raw_read_register(kMcpRegTxb0ctrl);
  const uint8_t eflg = mcp2515_raw_read_register(kMcpRegEflg);
  const bool tx0_done = (canintf & MCP2515::CANINTF_TX0IF) != 0;
  const bool txreq = (ctrl & kMcpTxbTxreq) != 0;
  const bool tx_ctrl_error = (ctrl & (kMcpTxbAbtf | kMcpTxbMloa | kMcpTxbTxerr)) != 0;
  const bool tx_bus_error = (eflg & (MCP2515::EFLG_TXBO | MCP2515::EFLG_TXEP)) != 0;

  if (tx_ctrl_error || tx_bus_error) {
    const uint16_t detail = (static_cast<uint16_t>(ctrl) << 8) | eflg;
    finish_mcp2515_pending_tx(false, detail);
    start_next_mcp2515_queued_tx();
    return;
  }

  if (tx0_done || !txreq) {
    finish_mcp2515_pending_tx(true, 0);
    start_next_mcp2515_queued_tx();
    return;
  }

  if (millis() - mcp2515_pending_tx.start_ms >= BOARD_MCP2515_TX_AUDIT_TIMEOUT_MS) {
    mcp2515_raw_modify_register(kMcpRegCanctrl, kMcpCanctrlAbat, kMcpCanctrlAbat);
    delayMicroseconds(20);
    mcp2515_raw_modify_register(kMcpRegCanctrl, kMcpCanctrlAbat, 0);
    const uint16_t detail = (static_cast<uint16_t>(ctrl) << 8) | eflg;
    finish_mcp2515_pending_tx(false, detail);
    start_next_mcp2515_queued_tx();
  }
#endif
}
#endif

#if BOARD_ENABLE_HOST_DOWNLINK
static void handle_host_can_tx_request(const uint8_t* payload, uint16_t len) {
#if BOARD_ENABLE_HOST_CAN_TX_ANY
  uint32_t command_id = 0;
  uint8_t bus = 0;
  uint8_t dlc = 0;
  uint32_t can_id_flags = 0;

  host_can_tx_request_total++;

  if (len != 19) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, ControlReasonBadLength, host_can_tx_rejected_total);
    return;
  }

  command_id = rd_u32_le(&payload[0]);
  bus = payload[4];
  const uint8_t frame_flags = payload[5];
  const bool extended = (frame_flags & 0x01) != 0;
  const bool rtr = (frame_flags & 0x02) != 0;
  const uint32_t raw_can_id = rd_u32_le(&payload[6]);
  dlc = payload[10];
  can_id_flags = raw_can_id & 0x1FFFFFFF;
  if (extended) {
    can_id_flags |= (1u << 29);
  }
  if (rtr) {
    can_id_flags |= (1u << 30);
  }

  const bool target_mcp2515 = (bus == BOARD_MCP2515_BUS_ID);
  const bool target_builtin_can = (bus == BOARD_BUILTIN_CAN_BUS_ID);
  const bool supported_bus =
      (target_mcp2515 && BOARD_ENABLE_HOST_CAN_TX_MCP2515 && BOARD_MCP2515_CONTROL_TX_ALLOWED) ||
      (target_builtin_can && BOARD_ENABLE_HOST_CAN_TX_BUILTIN && BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED);
  if (!supported_bus) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadBus, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, ControlReasonBadBus, host_can_tx_rejected_total);
    return;
  }
  if (rtr) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonUnsupportedFrame, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, ControlReasonUnsupportedFrame, host_can_tx_rejected_total);
    return;
  }
  if (dlc > 8) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonDlcOutOfRange, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, ControlReasonDlcOutOfRange, host_can_tx_rejected_total);
    return;
  }

  const uint32_t can_id = extended ? (raw_can_id & 0x1FFFFFFF) : (raw_can_id & 0x7FF);
  if (!is_allowed_host_can_id(can_id, extended)) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonIdNotAllowed, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, ControlReasonIdNotAllowed, host_can_tx_rejected_total);
    return;
  }

  uint8_t data[8] = {0};
  memcpy(data, &payload[11], dlc);

  uint8_t safety_reason = ControlReasonOk;
  if (!safety_supervisor.canAcceptTx(millis(), control_backend_ready_for_bus(bus), &safety_reason)) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, safety_reason, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, safety_reason, host_can_tx_rejected_total);
    return;
  }

#if BOARD_ENABLE_MCP2515 && BOARD_ENABLE_HOST_CAN_TX_MCP2515
  if (target_mcp2515) {
    if (!can_backend_ok || mcp2515 == nullptr) {
      host_can_tx_rejected_total++;
      emit_control_ack(command_id, ControlAckRejected, ControlReasonCanNotReady, bus, can_id_flags, dlc,
                       host_can_tx_request_total);
      emit_board_event(EventHostCanTxRejected, ControlReasonCanNotReady, host_can_tx_rejected_total);
      return;
    }

    if (!enqueue_mcp2515_tx(bus, can_id_flags, dlc, data)) {
      host_can_tx_rejected_total++;
      emit_control_ack(command_id, ControlAckRejected, ControlReasonQueueFull, bus, can_id_flags, dlc,
                       host_can_tx_request_total);
      emit_board_event(EventHostCanTxRejected, ControlReasonQueueFull, host_can_tx_rejected_total);
      return;
    }

    start_next_mcp2515_queued_tx();
    host_can_tx_accepted_total++;
    emit_control_ack(command_id, ControlAckAccepted, ControlReasonOk, bus, can_id_flags, dlc,
                     host_can_tx_accepted_total);
    safety_supervisor.noteControlTx(millis());
    return;
  }
#endif

#if BOARD_ENABLE_HOST_CAN_TX_BUILTIN
  if (!target_builtin_can) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadBus, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, ControlReasonBadBus, host_can_tx_rejected_total);
    return;
  }

  if (!builtin_can_tx_ok) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonCanNotReady, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventHostCanTxRejected, ControlReasonCanNotReady, host_can_tx_rejected_total);
    return;
  }

  const mbed::CANMessage msg(can_id, data, dlc, CANData, extended ? CANExtended : CANStandard);
  latch_passive_violation(kPassiveViolationCanTxCalled);
  const int rc = builtin_can.write(msg);
  if (rc <= 0) {
    builtin_can_tx_failed_total++;
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonCanWriteFailed, bus, can_id_flags, dlc,
                     host_can_tx_request_total);
    emit_board_event(EventBuiltinCanTxFailed, static_cast<uint16_t>(-rc), builtin_can_tx_failed_total);
    return;
  }

  builtin_can_tx_total++;
  host_can_tx_accepted_total++;
  emit_control_ack(command_id, ControlAckAccepted, ControlReasonOk, bus, can_id_flags, dlc,
                   host_can_tx_accepted_total);
  safety_supervisor.noteControlTx(millis());
  emit_can_tx_raw(bus, can_id_flags, dlc, data, builtin_can_tx_total, builtin_can_tx_failed_total);
#else
  host_can_tx_rejected_total++;
  emit_control_ack(command_id, ControlAckRejected, ControlReasonBadBus, bus, can_id_flags, dlc,
                   host_can_tx_request_total);
  emit_board_event(EventHostCanTxRejected, ControlReasonBadBus, host_can_tx_rejected_total);
#endif
#else
  (void)payload;
  (void)len;
#endif
}

static void handle_host_heartbeat(uint16_t seq, const uint8_t* payload, uint16_t len) {
  uint32_t command_id = seq;
  if (len >= 4) {
    command_id = rd_u32_le(&payload[0]);
  }
  if (len != csm::kHostHeartbeatPayloadLen) {
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength, 0xFF, 0, 0,
                     host_heartbeat_total);
    return;
  }

  host_heartbeat_total++;
  safety_supervisor.heartbeat(millis());
  if ((host_heartbeat_total & 0x3Fu) == 1) {
    emit_board_event(EventHostHeartbeat, 0, host_heartbeat_total);
  }
}

static void handle_host_control_session(uint16_t seq, const uint8_t* payload, uint16_t len) {
  uint32_t command_id = seq;
  uint8_t action = 0xFF;
  uint8_t requested_bus = 0xFF;
  uint16_t lease_ms = 0;

  host_control_session_total++;

  if (len >= 4) {
    command_id = rd_u32_le(&payload[0]);
  }
  if (len != csm::kHostControlSessionPayloadLen) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength, requested_bus, 0, 0,
                     host_control_session_total);
    emit_board_event(EventHostControlSession, ControlReasonBadLength, host_control_session_total);
    return;
  }

  action = payload[4];
  requested_bus = payload[5];
  lease_ms = rd_u16_le(&payload[8]);

  const csm::board::SafetyInputs inputs = read_safety_inputs();
  safety_supervisor.update(millis(), inputs);
  safety_state = safety_supervisor.state();

  uint8_t reason = ControlReasonOk;
  uint8_t status = ControlAckAccepted;
  const bool backend_ready =
      requested_bus == 0xFF ? any_control_backend_ready() : control_backend_ready_for_bus(requested_bus);

  switch (action) {
    case csm::HostControlDisarm:
      safety_supervisor.disarm(millis());
      break;
    case csm::HostControlArm:
      reason = safety_supervisor.arm(millis(), lease_ms, backend_ready);
      break;
    case csm::HostControlRenewLease:
      reason = safety_supervisor.renewLease(millis(), lease_ms);
      break;
    case csm::HostControlInstallNeutralProfile:
      reason = ControlReasonUnsupportedCommand;
      break;
    default:
      reason = ControlReasonUnsupportedCommand;
      break;
  }

  if (reason != ControlReasonOk) {
    status = ControlAckRejected;
    host_can_tx_rejected_total++;
  }

  safety_state = safety_supervisor.state();
  emit_control_ack(command_id, status, reason, requested_bus, 0, 0, host_control_session_total);
  emit_board_event(EventHostControlSession,
                   (static_cast<uint16_t>(action) << 8) | reason,
                   host_control_session_total);
}

static void handle_host_query_capability(uint16_t seq, const uint8_t* payload, uint16_t len) {
  uint32_t command_id = seq;
  if (len >= 4) {
    command_id = rd_u32_le(&payload[0]);
  }
  if (len != 0 && len != 4) {
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength, 0xFF, 0, 0, 0);
    return;
  }
  emit_capability();
  emit_control_ack(command_id, ControlAckAccepted, ControlReasonOk, 0xFF, 0, 0, 0);
}

static void handle_host_clear_fault_lockout(uint16_t seq, const uint8_t* payload, uint16_t len) {
  uint32_t command_id = seq;
  if (len >= 4) {
    command_id = rd_u32_le(&payload[0]);
  }
  if (len != csm::kHostClearFaultLockoutPayloadLen) {
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength, 0xFF, 0, 0, 0);
    return;
  }

  const csm::board::SafetyInputs inputs = read_safety_inputs();
  uint8_t reason = ControlReasonOk;
  if (inputs.estop_asserted) {
    reason = ControlReasonEstopAsserted;
  } else if (!inputs.field_power_ok) {
    reason = ControlReasonFieldPowerLost;
  } else if (inputs.encoder_fault) {
    reason = csm::ControlReasonEncoderFault;
  }

  if (reason == ControlReasonOk) {
    safety_supervisor.clearFaultLockout(millis());
    safety_state = safety_supervisor.state();
    emit_control_ack(command_id, ControlAckAccepted, ControlReasonOk, 0xFF, 0, 0, 0);
    emit_board_event(EventFaultLockoutCleared, 0, safety_supervisor.transitionCounter());
  } else {
    emit_control_ack(command_id, ControlAckRejected, reason, 0xFF, 0, 0, 0);
  }
}

static void dispatch_host_frame(uint8_t version, uint8_t record_type, uint16_t seq,
                                const uint8_t* payload, uint16_t len) {
  if (version != kProtocolVersion) {
    emit_control_ack(seq, ControlAckRejected, ControlReasonBadProtocol, 0, 0, 0, host_can_tx_request_total);
    return;
  }

  if (record_type == static_cast<uint8_t>(RecordType::HostCanTxRequest)) {
    handle_host_can_tx_request(payload, len);
  } else if (record_type == static_cast<uint8_t>(RecordType::HostHeartbeat)) {
    handle_host_heartbeat(seq, payload, len);
  } else if (record_type == static_cast<uint8_t>(RecordType::HostControlSession)) {
    handle_host_control_session(seq, payload, len);
  } else if (record_type == static_cast<uint8_t>(RecordType::HostQueryCapability)) {
    handle_host_query_capability(seq, payload, len);
  } else if (record_type == static_cast<uint8_t>(RecordType::HostClearFaultLockout)) {
    handle_host_clear_fault_lockout(seq, payload, len);
  } else {
    emit_control_ack(seq, ControlAckRejected, ControlReasonUnsupportedCommand, 0xFF, 0, 0,
                     host_can_tx_request_total);
    emit_board_event(EventHostCommandUnsupported, record_type, host_can_tx_request_total);
  }
}

static void handle_host_downlink_frame(void*, uint8_t version, uint8_t record_type,
                                       uint16_t seq, const uint8_t* payload, uint16_t len) {
  dispatch_host_frame(version, record_type, seq, payload, len);
}

static void handle_host_downlink_crc_failure(void*) {
  host_frame_crc_failed_total++;
  if ((host_frame_crc_failed_total & 0x0F) == 1) {
    emit_board_event(EventHostFrameCrcFailed, 0, host_frame_crc_failed_total);
  }
}

static csm::board::HostDownlinkParser host_downlink_parser(
    handle_host_downlink_frame, handle_host_downlink_crc_failure);

static void service_host_downlink(int budget) {
  host_downlink_parser.service(Serial, budget);
}
#else
static void service_host_downlink(int budget) {
  while (budget-- > 0 && Serial.available() > 0) {
    (void)Serial.read();
  }
}
#endif

#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST
static void service_builtin_can_tx_test() {
  if (!builtin_can_tx_ok) {
    return;
  }

  const uint32_t now_ms = millis();
  if (now_ms - last_builtin_can_tx_ms < BOARD_BUILTIN_CAN_TX_PERIOD_MS) {
    return;
  }
  last_builtin_can_tx_ms = now_ms;

  const uint32_t counter = builtin_can_tx_total;
  uint8_t data[8] = {
    0xB7,
    0x1D,
    static_cast<uint8_t>(counter & 0xFF),
    static_cast<uint8_t>((counter >> 8) & 0xFF),
    static_cast<uint8_t>((counter >> 16) & 0xFF),
    static_cast<uint8_t>((counter >> 24) & 0xFF),
    0x50,
    0x07,
  };

  const mbed::CANMessage msg(BOARD_BUILTIN_CAN_TX_TEST_ID, data, sizeof(data), CANData, CANStandard);
  latch_passive_violation(kPassiveViolationCanTxCalled);
  const int rc = builtin_can.write(msg);
  if (rc <= 0) {
    builtin_can_tx_failed_total++;
    if ((builtin_can_tx_failed_total & 0x0F) == 1) {
      emit_board_event(EventBuiltinCanTxFailed, static_cast<uint16_t>(-rc), builtin_can_tx_failed_total);
    }
    return;
  }

  builtin_can_tx_total++;
  emit_can_tx_raw(BOARD_BUILTIN_CAN_BUS_ID, BOARD_BUILTIN_CAN_TX_TEST_ID, sizeof(data), data,
                  builtin_can_tx_total, builtin_can_tx_failed_total);
}
#endif

#if BOARD_ENABLE_MCP2515 && BOARD_ENABLE_MCP2515_TX_TEST
static void service_mcp2515_tx_test() {
  if (!can_backend_ok || mcp2515 == nullptr) {
    return;
  }

  const uint32_t now_ms = millis();
  if (now_ms - last_mcp2515_tx_ms < BOARD_MCP2515_TX_PERIOD_MS) {
    return;
  }
  last_mcp2515_tx_ms = now_ms;

  const uint32_t counter = mcp2515_tx_total + mcp2515_tx_failed_total;
  uint8_t data[8] = {
    0xC5,
    0x10,
    static_cast<uint8_t>(counter & 0xFF),
    static_cast<uint8_t>((counter >> 8) & 0xFF),
    static_cast<uint8_t>((counter >> 16) & 0xFF),
    static_cast<uint8_t>((counter >> 24) & 0xFF),
    0x08,
    0x00,
  };

  const MCP2515::ERROR err = start_mcp2515_tx_audit(BOARD_MCP2515_BUS_ID, BOARD_MCP2515_TX_TEST_ID,
                                                    sizeof(data), data);
  if (err != MCP2515::ERROR_OK) {
    mcp2515_tx_failed_total++;
    if ((mcp2515_tx_failed_total & 0x0F) == 1) {
      emit_board_event(EventMcp2515TxFailed, static_cast<uint16_t>(err), mcp2515_tx_failed_total);
    }
    return;
  }
}
#endif

static void test_push_fake_can_if_needed() {
  static uint32_t last_fake_us = 0;
  const uint32_t now_us = micros();
  if (now_us - last_fake_us < 10000) {
    return;
  }
  last_fake_us = now_us;

  CanRxItem item;
  item.capture_seq = can_capture_seq_next++;
  item.mono_us = mono64_us();
  item.can_id_flags = 0x123;
  item.dlc_flags = 8;
  item.bus = BOARD_MCP2515_BUS_ID;
  for (uint8_t i = 0; i < 8; ++i) {
    item.data[i] = i;
  }
  can_queue_push(item);
}

static void drain_can_records(int budget) {
  CanRxItem item;
  int records_since_mcp_service = 0;
  const uint32_t start_us = micros();

  if (!can_rx_segment_builder.flushIfDue(start_us)) {
    return;
  }

  while (budget-- > 0) {
    if (static_cast<uint32_t>(micros() - start_us) > BOARD_CAN_RECORD_DRAIN_TIME_BUDGET_US) {
      break;
    }
    if (!can_queue_pop(item)) {
      break;
    }
    if (!can_rx_segment_builder.push(item, micros())) {
      break;
    }
    ++records_since_mcp_service;
    if (records_since_mcp_service >= BOARD_CAN_RECORDS_BEFORE_MCP_SERVICE) {
      records_since_mcp_service = 0;
      if (!kTestMode && can_backend_ok) {
        pump_can_rx_to_queue(BOARD_SERIAL_TX_CAN_INTERLEAVE_MCP_BUDGET);
      }
    }
  }
  can_rx_segment_builder.flushIfDue(micros());
  note_can_rx_task_elapsed(start_us);
}

static void service_encoder() {
#if !BOARD_ENABLE_ENCODER_IO
  return;
#else
  static int64_t last_position_for_velocity = 0;
  static uint32_t last_velocity_ms = 0;

  EncoderSnapshot snap = poll_encoder();

  if (encoder_index_pending) {
    noInterrupts();
    const uint64_t index_mono = encoder_index_mono_us;
    encoder_index_pending = false;
    interrupts();
    snap.mono_us = index_mono;
    emit_encoder_edge(snap, 0x01);
    emit_board_event(EventEncoderIndex, snap.ab_state, encoder_index_count);
  }

  const uint32_t now_ms = millis();
  uint32_t derived_period_ms = BOARD_ENCODER_DERIVED_PERIOD_MS;
  if (uplink_boot_quiet_active()) {
    derived_period_ms = BOARD_ENCODER_DERIVED_BOOT_QUIET_PERIOD_MS;
  } else if (uplink_tx_pressure_active()) {
    derived_period_ms = BOARD_ENCODER_DERIVED_PRESSURE_PERIOD_MS;
  }
  if (now_ms - last_encoder_derived_ms >= derived_period_ms) {
    const uint32_t dt_ms = last_velocity_ms == 0 ? derived_period_ms : now_ms - last_velocity_ms;
    const int64_t delta = snap.position - last_position_for_velocity;
    int32_t cps = 0;
    if (dt_ms > 0) {
      cps = static_cast<int32_t>((delta * 1000) / static_cast<int64_t>(dt_ms));
    }
    emit_encoder_derived(snap, cps);
    last_position_for_velocity = snap.position;
    last_velocity_ms = now_ms;
    last_encoder_derived_ms = now_ms;
  }
#endif
}

void setup() {
  uplink_boot_ms = millis();
  init_safety_pins();
  init_status_led();
  init_runtime_watchdog();
  Serial.begin(115200);

  csm::board::uplink::SerialTxSchedulerConfig serial_tx_config;
  serial_tx_config.drain_time_budget_us = BOARD_SERIAL_TX_DRAIN_TIME_BUDGET_US;
  serial_tx_config.max_writes_per_pump = BOARD_SERIAL_TX_MAX_WRITES_PER_PUMP;
  serial_tx_config.max_bytes_per_pump = BOARD_SERIAL_TX_MAX_BYTES_PER_PUMP;
  serial_tx_scheduler.begin(serial_tx_config);
  uplink_scheduler.begin(&serial_tx_scheduler);
  can_rx_segment_builder.begin(emit_can_rx_segment_callback, nullptr, BOARD_CAN_RX_SEGMENT_FLUSH_US);

  safety_supervisor.begin(millis());
  safety_state = safety_supervisor.state();
  voltage_adc_ok = init_voltage_adc_lane();

#if BOARD_ENABLE_ENCODER_IO
  pinMode(BoardPins::EncoderA, INPUT);
  pinMode(BoardPins::EncoderB, INPUT);
  pinMode(BoardPins::EncoderZ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BoardPins::EncoderZ), on_encoder_index, RISING);
#endif

  encoder_timer_ok = init_encoder_timer3();

  emit_capability();
  last_capability_ms = millis();
  emit_board_event(EventBoot, encoder_timer_ok ? 1 : 0, 1);
  emit_board_event(EventFirmwareIdentity,
                   static_cast<uint16_t>(1u | ((CSM_FW_GIT_DIRTY ? 1u : 0u) << 8)),
                   CSM_FW_BUILD_ID);
#if BOARD_HW_PROFILE_MID_TJA1051_DUAL && BOARD_TARGET_INTERNAL_CAN_LANE0 && !BOARD_ENABLE_INTERNAL_CAN_LANE0_BACKEND
  emit_board_event(EventCan0BackendUnavailable, 0, 1);
#endif

  if (!kTestMode && BOARD_ENABLE_MCP2515_INIT &&
      (!BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION || uplink_host_session_open())) {
    can_backend_ok = init_can_backend();
    if (!can_backend_ok) {
      last_can_init_retry_ms = millis();
      note_can_init_retry_failure();
      emit_board_event(EventCanBeginFailed, 0, can_init_retry_count);
    } else {
      reset_can_init_retry_backoff();
    }
  }

#if BOARD_ENABLE_BUILTIN_CAN_LANE
#if BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION
  builtin_can_tx_ok = false;
#else
  builtin_can_tx_ok = init_builtin_can_lane();
#endif
#endif

  emit_capability();
  last_capability_ms = millis();

  last_health_ms = millis();
  last_encoder_derived_ms = millis();
  last_watchdog_toggle_ms = millis();
}

void loop() {
  kick_runtime_watchdog();
  service_usb_cdc_reconnect_watchdog();
  mono64_us();
  service_uplink_session_state();
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  if (uplink_host_session_open() && !ensure_passive_can_frontend_session_ready(millis())) {
    update_safety_state();
    toggle_safety_watchdog_if_needed();
    service_status_led();
    service_capability_advertisement();
    service_serial_tx(1024);
    kick_runtime_watchdog();
    return;
  }
  if (!uplink_host_session_open()) {
    if (!kTestMode && can_backend_ok) {
      service_mcp2515_host_absent_drain(BOARD_MCP2515_LOOP_ENTRY_DRAIN_BUDGET);
    } else if (!BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION &&
               BOARD_ENABLE_MCP2515_INIT &&
               (millis() - last_can_init_retry_ms >= can_init_retry_delay_ms)) {
      last_can_init_retry_ms = millis();
      can_backend_ok = init_can_backend();
      if (!can_backend_ok) {
        note_can_init_retry_failure();
      } else {
        reset_can_init_retry_backoff();
      }
    }
    service_builtin_can_rx_host_absent_drain(128);
    update_safety_state();
    toggle_safety_watchdog_if_needed();
    service_status_led();
    last_health_ms = millis();
    kick_runtime_watchdog();
    return;
  }
#endif
  if (!kTestMode && can_backend_ok) {
    pump_can_rx_to_queue(BOARD_MCP2515_LOOP_ENTRY_DRAIN_BUDGET);
  }
  service_serial_tx(1024);
  service_deferred_loss_events();
  service_host_downlink(256);
  update_safety_state();
  toggle_safety_watchdog_if_needed();

  if (kTestMode) {
    test_push_fake_can_if_needed();
  } else if (can_backend_ok) {
    pump_can_rx_to_queue(512);
  } else if (!BOARD_PASSIVE_DEFER_CAN_FRONTEND_INIT_UNTIL_SESSION &&
             BOARD_ENABLE_MCP2515_INIT &&
             (millis() - last_can_init_retry_ms >= can_init_retry_delay_ms)) {
    last_can_init_retry_ms = millis();
    can_backend_ok = init_can_backend();
    if (!can_backend_ok) {
      note_can_init_retry_failure();
      emit_board_event(EventCanBeginFailed, 1, can_init_retry_count);
    } else {
      reset_can_init_retry_backoff();
      if (uplink_host_session_open()) {
        set_can_observe_mode_for_session(true, true);
      }
      emit_capability();
      last_capability_ms = millis();
    }
  }

  service_builtin_can_rx_to_queue(128);
#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST
  service_builtin_can_tx_test();
#endif
#if BOARD_ENABLE_MCP2515
  service_mcp2515_tx_audit();
#endif
#if BOARD_ENABLE_MCP2515 && BOARD_ENABLE_MCP2515_TX_TEST
  service_mcp2515_tx_test();
#endif
  service_status_led();
  service_capability_advertisement();
  service_encoder();
  drain_can_records(BOARD_CAN_SERIAL_DRAIN_BUDGET);
  if (!kTestMode && can_backend_ok) {
    pump_can_rx_to_queue(512);
  }
  service_voltage_adc_lane();

  const uint32_t now_ms = millis();
  if (!uplink_host_session_open()) {
    last_health_ms = now_ms;
  } else if (now_ms - last_health_ms >= 1000) {
    const EncoderSnapshot snap = poll_encoder();
    emit_board_health(snap);
    last_health_ms = now_ms;
  }
  service_serial_tx(1024);
  service_deferred_loss_events();
  service_usb_cdc_reconnect_watchdog();
  kick_runtime_watchdog();
}
