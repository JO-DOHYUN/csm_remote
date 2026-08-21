#include <Arduino.h>
#include <cstring>
#include <mbed.h>
#include <new>
#if DEVICE_RESET_REASON
#include "drivers/ResetReason.h"
#endif
#if defined(SERIAL_CDC)
#include "USB/PluggableUSBSerial.h"
#endif

#include "BoardPins.h"
#include "board/CapabilityPublisher.h"
#include "board/HostDownlinkParser.h"
#include "board/SafetySupervisor.h"
#include "board/StatusLed.h"
#include "board/control/HostCommandFreshness.h"
#include "board/control/HostControlAuthorityGate.h"
#include "board/control/RemoteControlRuntime.h"
#include "board/control_island/ControlIslandSharedMemory.h"
#include "board/control_island/ControlSourceManager.h"
#include "board/diagnostics/BootProgress.h"
#include "board/diagnostics/RetainedCallLatch.h"
#include "board/diagnostics/RuntimeSupervisor.h"
#include "board/feeder/FeederUartIngress.h"
#include "board/memory/ProductMemoryProfile.h"
#include "board/uplink/CanRxQueueOrder.h"
#include "board/uplink/CanRxSegmentBuilder.h"
#include "board/uplink/CanonicalPublisher.h"
#include "board/uplink/UplinkPriorityPolicy.h"
#include "board/uplink/UsbCdcSink.h"
#include "board/uplink/WifiTcpSink.h"
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

#ifndef CSM_FW_SOURCE_ID32
#define CSM_FW_SOURCE_ID32 0
#endif

#ifndef CSM_FW_SOURCE_ID64
#define CSM_FW_SOURCE_ID64 0ULL
#endif

#ifndef CSM_FW_RUNTIME_CONTRACT_ID64
#define CSM_FW_RUNTIME_CONTRACT_ID64 0ULL
#endif

#ifndef CSM_FW_RUNTIME_CONTRACT_ID32
#define CSM_FW_RUNTIME_CONTRACT_ID32 0
#endif

#ifndef BOARD_WIFI_AP_SSID
#define BOARD_WIFI_AP_SSID "VSM-CSM-DEV"
#endif

#ifndef BOARD_WIFI_AP_PASSPHRASE
#define BOARD_WIFI_AP_PASSPHRASE "vsm-csm-dev"
#endif

#ifndef BOARD_WIFI_TCP_PORT
#define BOARD_WIFI_TCP_PORT 3333
#endif

#ifndef BOARD_WIFI_AP_CHANNEL
#define BOARD_WIFI_AP_CHANNEL 6
#endif

#ifndef BOARD_HW_PROFILE_MID_TJA1051_DUAL
#define BOARD_HW_PROFILE_MID_TJA1051_DUAL 0
#endif

#ifndef BOARD_HW_PROFILE_MID_MCP2515
#define BOARD_HW_PROFILE_MID_MCP2515 0
#endif

#ifndef BOARD_HW_PROFILE_MID_FEEDER_UART
#define BOARD_HW_PROFILE_MID_FEEDER_UART 0
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

#ifndef BOARD_ENABLE_FEEDER_UART
#define BOARD_ENABLE_FEEDER_UART 0
#endif

#ifndef BOARD_FEEDER_UART_BAUD
#define BOARD_FEEDER_UART_BAUD 1000000UL
#endif

#ifndef BOARD_FEEDER_UART_STALE_MS
#define BOARD_FEEDER_UART_STALE_MS 250UL
#endif

#ifndef BOARD_FEEDER_UART_SERVICE_BYTE_BUDGET
#define BOARD_FEEDER_UART_SERVICE_BYTE_BUDGET 4096UL
#endif

#ifndef BOARD_FEEDER_UART_SERVICE_TIME_BUDGET_US
#define BOARD_FEEDER_UART_SERVICE_TIME_BUDGET_US 750UL
#endif

#ifndef BOARD_FEEDER_CAN_BUS_ID
#define BOARD_FEEDER_CAN_BUS_ID 0
#endif
#ifndef BOARD_FEEDER_CAN_BUS_ROLE
#define BOARD_FEEDER_CAN_BUS_ROLE 2
#endif

#ifndef BOARD_CSM_PROFILE_PASSIVE_PRODUCT
#define BOARD_CSM_PROFILE_PASSIVE_PRODUCT 0
#endif

#ifndef BOARD_CSM_PROFILE_REMOTE_PRODUCT
#define BOARD_CSM_PROFILE_REMOTE_PRODUCT 0
#endif

#ifndef BOARD_CSM_PROFILE_FULL_INSTRUMENTED
#define BOARD_CSM_PROFILE_FULL_INSTRUMENTED \
  (!BOARD_CSM_PROFILE_PASSIVE_PRODUCT && !BOARD_CSM_PROFILE_REMOTE_PRODUCT)
#endif

#ifndef BOARD_ENABLE_REMOTE_CONTROL
#define BOARD_ENABLE_REMOTE_CONTROL BOARD_CSM_PROFILE_REMOTE_PRODUCT
#endif

#ifndef BOARD_ENABLE_REMOTE_AUTHORITY
#define BOARD_ENABLE_REMOTE_AUTHORITY BOARD_ENABLE_REMOTE_CONTROL
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
#define BOARD_CAN_QUEUE_SIZE 512
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
#ifndef BOARD_ENABLE_PERIODIC_CAPABILITY
#define BOARD_ENABLE_PERIODIC_CAPABILITY 1
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

#ifndef BOARD_DIAG_SUPPRESS_REMOTE_CAN_TX
#define BOARD_DIAG_SUPPRESS_REMOTE_CAN_TX 0
#endif

#ifndef BOARD_ENABLE_MDPS_BENCH_MAPPING
#define BOARD_ENABLE_MDPS_BENCH_MAPPING 0
#endif

#ifndef BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING
#define BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING 0
#endif

#ifndef BOARD_ENABLE_SERVICE_HIL_VEHICLE_COMMAND_MAPPING
#define BOARD_ENABLE_SERVICE_HIL_VEHICLE_COMMAND_MAPPING 0
#endif

#ifndef BOARD_REMOTE_DRIVE_CHANNEL_INDEX
#define BOARD_REMOTE_DRIVE_CHANNEL_INDEX 3
#endif

#ifndef BOARD_REMOTE_STEERING_CHANNEL_INDEX
#define BOARD_REMOTE_STEERING_CHANNEL_INDEX 1
#endif

#ifndef BOARD_CSM_PROFILE_REMOTE_MDPS_BENCH
#define BOARD_CSM_PROFILE_REMOTE_MDPS_BENCH 0
#endif

// A remote input pipeline is not, by itself, a vehicle-control capability.
// Product/local CAN TX is advertised only when an explicit vehicle mapping is
// selected and the diagnostic safety interlock has not suppressed output.
#define BOARD_REMOTE_SEMANTIC_CONTROL_ENABLED \
  (BOARD_ENABLE_REMOTE_CONTROL && \
   (BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING || \
    BOARD_ENABLE_SERVICE_HIL_VEHICLE_COMMAND_MAPPING || \
    BOARD_ENABLE_MDPS_BENCH_MAPPING) && \
   !BOARD_DIAG_SUPPRESS_REMOTE_CAN_TX)

#ifndef BOARD_RUNTIME_DIAGNOSTIC_PERIOD_MS
#define BOARD_RUNTIME_DIAGNOSTIC_PERIOD_MS 100
#endif

#ifndef BOARD_WIFI_TRANSPORT_DIAGNOSTIC_PERIOD_MS
#define BOARD_WIFI_TRANSPORT_DIAGNOSTIC_PERIOD_MS 1000
#endif

#ifndef BOARD_ENABLE_WIFI_DEEP_DIAGNOSTICS
#define BOARD_ENABLE_WIFI_DEEP_DIAGNOSTICS 0
#endif

#ifndef BOARD_BUILTIN_CAN_TX_COMPLETION_TIMEOUT_US
#define BOARD_BUILTIN_CAN_TX_COMPLETION_TIMEOUT_US 0
#endif
#ifndef BOARD_HOST_DOWNLINK_SERVICE_BYTE_BUDGET
// Bounded parser ingress budget. Host CAN requests are independent one-shot
// attempts; this budget never creates a retention or scheduling layer.
#define BOARD_HOST_DOWNLINK_SERVICE_BYTE_BUDGET 256
#endif

#ifndef BOARD_HOST_HEARTBEAT_MAX_EXTRA_LAG_MS
#define BOARD_HOST_HEARTBEAT_MAX_EXTRA_LAG_MS 0
#endif
#ifndef BOARD_HOST_CAN_TX_MAX_AGE_MS
#define BOARD_HOST_CAN_TX_MAX_AGE_MS 0
#endif
#ifndef BOARD_HOST_CLOCK_FUTURE_TOLERANCE_MS
#define BOARD_HOST_CLOCK_FUTURE_TOLERANCE_MS 0
#endif

#define BOARD_ENABLE_HOST_CAN_TX_ANY (BOARD_ENABLE_HOST_CAN_TX || BOARD_ENABLE_HOST_CAN_TX_BUILTIN || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
#define BOARD_APPLICATION_CAN_DATA_TX_ENABLED                               \
  (BOARD_ENABLE_HOST_CAN_TX_ANY || BOARD_ENABLE_MCP2515_TX_TEST ||          \
   BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_REMOTE_SEMANTIC_CONTROL_ENABLED)
#define BOARD_ENABLE_BUILTIN_CAN_LANE \
  (BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_BUILTIN_CAN_RX || \
   BOARD_ENABLE_HOST_CAN_TX_BUILTIN)

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS && !BOARD_ENABLE_BUILTIN_CAN_LANE
#error "Runtime diagnostics require the built-in FDCAN lane"
#endif

#ifndef BOARD_ENABLE_HOST_DOWNLINK
#define BOARD_ENABLE_HOST_DOWNLINK 0
#endif

#ifndef BOARD_ENABLE_CONTROL_ISLAND
#define BOARD_ENABLE_CONTROL_ISLAND 0
#endif

#ifndef BOARD_CONTROL_ISLAND_HEALTH_TIMEOUT_MS
// Zero means that the M4 readiness observation is not qualified. M7 therefore
// publishes permit_mask=0 until HIL freezes the bound.
#define BOARD_CONTROL_ISLAND_HEALTH_TIMEOUT_MS 0UL
#endif

#ifndef BOARD_HOST_DOWNLINK_TRANSPORT_WIFI
#define BOARD_HOST_DOWNLINK_TRANSPORT_WIFI 0
#endif

#ifndef BOARD_ENABLE_SERVICE_HIL_JOYSTICK_IDS
#define BOARD_ENABLE_SERVICE_HIL_JOYSTICK_IDS 0
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

#if BOARD_CSM_PROFILE_REMOTE_MDPS_BENCH && \
    (!BOARD_CSM_PROFILE_REMOTE_PRODUCT || !BOARD_ENABLE_REMOTE_CONTROL || \
     !BOARD_ENABLE_REMOTE_AUTHORITY || !BOARD_ENABLE_MDPS_BENCH_MAPPING || \
     BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING || \
     (!BOARD_ENABLE_FEEDER_UART && \
      (!BOARD_ENABLE_MCP2515 || !BOARD_ENABLE_MCP2515_INIT)) || \
     (BOARD_ENABLE_MCP2515 && BOARD_MCP2515_LISTEN_ONLY_BY_DEFAULT) || \
     BOARD_ENABLE_HOST_CAN_TX || \
     BOARD_ENABLE_HOST_CAN_TX_BUILTIN || BOARD_ENABLE_HOST_CAN_TX_MCP2515 || \
     BOARD_MCP2515_CONTROL_TX_ALLOWED)
#error "Remote MDPS bench profile violates its authority/CAN safety contract"
#endif

#ifndef BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED
#define BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED BOARD_ENABLE_HOST_CAN_TX_BUILTIN
#endif

// Production local control still requires an explicit autonomy-release source.
// Service/HIL may use the bounded virtual provider; it is never production evidence.
#ifndef BOARD_AUTONOMY_RELEASE_PROVIDER_AVAILABLE
#define BOARD_AUTONOMY_RELEASE_PROVIDER_AVAILABLE 0
#endif
#ifndef BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH
#define BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH 0
#endif

#if BOARD_CSM_PROFILE_REMOTE_PRODUCT && !BOARD_CSM_PROFILE_REMOTE_MDPS_BENCH && \
    BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH
#error "RemoteProduct may not use virtual autonomy evidence"
#endif

#if BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING && \
    (!BOARD_CSM_PROFILE_REMOTE_PRODUCT || BOARD_CSM_PROFILE_REMOTE_MDPS_BENCH || \
     !BOARD_ENABLE_REMOTE_CONTROL || !BOARD_ENABLE_REMOTE_AUTHORITY || \
     !BOARD_HW_PROFILE_MID_FEEDER_UART || !BOARD_ENABLE_FEEDER_UART || \
     BOARD_ENABLE_MDPS_BENCH_MAPPING || BOARD_ENABLE_SERVICE_HIL_JOYSTICK_IDS || \
     BOARD_ENABLE_HOST_CAN_TX || BOARD_ENABLE_HOST_CAN_TX_BUILTIN || \
     BOARD_ENABLE_HOST_CAN_TX_MCP2515 || BOARD_ENABLE_HOST_DOWNLINK || \
     BOARD_ENABLE_MCP2515 || BOARD_ENABLE_MCP2515_INIT || \
     !BOARD_ENABLE_CONTROL_ISLAND || BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED)
#error "Product vehicle command mapping violates the feeder/J4 RC contract"
#endif

#if BOARD_ENABLE_SERVICE_HIL_VEHICLE_COMMAND_MAPPING && \
    (BOARD_CSM_PROFILE_REMOTE_PRODUCT || !BOARD_CSM_PROFILE_FULL_INSTRUMENTED || \
     !BOARD_ENABLE_SERVICE_HIL_JOYSTICK_IDS || !BOARD_ENABLE_REMOTE_CONTROL || \
     !BOARD_ENABLE_REMOTE_AUTHORITY || !BOARD_HW_PROFILE_MID_FEEDER_UART || \
     !BOARD_ENABLE_FEEDER_UART || BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING || \
     BOARD_ENABLE_MDPS_BENCH_MAPPING || BOARD_ENABLE_HOST_CAN_TX_BUILTIN || \
     !BOARD_ENABLE_HOST_DOWNLINK || !BOARD_HOST_DOWNLINK_TRANSPORT_WIFI || \
     !BOARD_ENABLE_CONTROL_ISLAND || BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED || \
     !BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH)
#error "Service/HIL vehicle mapping violates the feeder/J4 bench contract"
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

#if BOARD_CSM_PROFILE_REMOTE_PRODUCT
#if !BOARD_ENABLE_REMOTE_CONTROL || !BOARD_ENABLE_REMOTE_AUTHORITY
#error "Remote Product requires the RC frontend and authority boundary"
#endif
#if BOARD_ENABLE_HOST_CAN_TX || BOARD_ENABLE_HOST_CAN_TX_BUILTIN || BOARD_ENABLE_HOST_CAN_TX_MCP2515
#error "Remote Product keeps app/host control compiled out; use a separately qualified service profile"
#endif
#if BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_BUILTIN_CAN_TX_TEST
#error "Remote Product must compile out CAN TX test paths"
#endif
#if !BOARD_ENABLE_CONTROL_ISLAND || BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED
#error "Remote Product requires the M4 control island and forbids M7 CAN TX ownership"
#endif
#endif

#if BOARD_ENABLE_WIFI_UPLINK && BOARD_HOST_DOWNLINK_TRANSPORT_WIFI && BOARD_ENABLE_SERVICE_HIL_JOYSTICK_IDS
#if BOARD_USB_CDC_RECONNECT_RESET_MS != 0
#error "Service/HIL Wi-Fi must not reset the board on transient USB CDC disconnects"
#endif
#if !BOARD_ENABLE_REMOTE_CONTROL || !BOARD_ENABLE_REMOTE_AUTHORITY
#error "Service/HIL Wi-Fi requires the RC authority boundary"
#endif
#if !BOARD_ENABLE_CONTROL_ISLAND || BOARD_ENABLE_HOST_CAN_TX_BUILTIN || \
    BOARD_BUILTIN_CAN_CONTROL_TX_ALLOWED
#error "Service/HIL Wi-Fi requires the M4 control island and forbids M7 CAN TX ownership"
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
static constexpr uint8_t kFirmwareProfileRemoteProduct = 3;
static constexpr uint8_t kProfileLockCompileTime = 1;
static constexpr uint8_t kVehicleImpactUnknown = 0;
static constexpr uint8_t kVehicleImpactPossible = 1;
static constexpr uint8_t kVehicleImpactConfiguredPassive = 2;
static constexpr uint8_t kVehicleImpactVerifiedPassive = 3;
static constexpr uint8_t kBusModeUnknown = 0;
static constexpr uint8_t kBusModeListenOnly = 1;
static constexpr uint8_t kBusModeHardwareSilent = 2;
static constexpr uint8_t kBusModeNormal = 3;
#if BOARD_ENABLE_SERVICE_HIL_JOYSTICK_IDS
static constexpr uint32_t kServiceHilAllowedEhbCanId = 0x364u;
#endif

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
using csm::kCapabilityV7PayloadLen;
using csm::kBoardHealthV2PayloadLen;
using csm::kBoardHealthV4PayloadLen;
using csm::kBoardHealthV6PayloadLen;
using csm::kBoardHealthV7PayloadLen;
using csm::kBoardHealthV8PayloadLen;
using csm::kBoardHealthV9PayloadLen;
using csm::kBoardHealthV10PayloadLen;
using csm::kBoardHealthV11PayloadLen;
using csm::kBoardHealthV13PayloadLen;
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
using csm::board::uplink::encode_can_rx_segment_payload;
using csm::board::uplink::kNoReadyCanRxQueue;
using csm::board::uplink::select_can_rx_queue_index;
using csm::board::uplink::CanonicalPublisher;
using csm::board::uplink::SessionAnnouncementReason;
using csm::board::uplink::UplinkPriority;
using csm::board::uplink::UsbCdcSink;
using csm::board::uplink::WifiTcpSink;
using csm::board::feeder::FeederCanFrame;
using csm::board::feeder::FeederStatus;
using csm::board::feeder::FeederUartIngress;
using csm::board::feeder::FeederUartIngressConfig;
using csm::board::feeder::FeederUartIngressStats;
using csm::board::feeder::FeederWireStats;

using SafetyState = csm::board::SafetyState;

enum BoardEventCode : uint16_t {
  EventBoot = csm::kBoardEventBootCode,
  EventCanBeginFailed = csm::kBoardEventCanBeginFailedCode,
  EventCanRxQueueDrop = csm::kBoardEventCanRxQueueDropCode,
  EventEncoderFaultAsserted = csm::kBoardEventEncoderFaultAssertedCode,
  EventFieldPowerLost = csm::kBoardEventFieldPowerLostCode,
  EventEstopAsserted = csm::kBoardEventEstopAssertedCode,
  EventEncoderIndex = csm::kBoardEventEncoderIndexCode,
  EventEncoderWrap = csm::kBoardEventEncoderWrapCode,
  EventMcp2515Error = csm::kBoardEventMcp2515ErrorCode,
  EventMcp2515SpiSnapshot = csm::kBoardEventMcp2515SpiSnapshotCode,
  EventBuiltinCanBeginFailed = csm::kBoardEventBuiltinCanBeginFailedCode,
  EventBuiltinCanTxFailed = csm::kBoardEventBuiltinCanTxFailedCode,
  EventHostFrameCrcFailed = csm::kBoardEventHostFrameCrcFailedCode,
  EventHostCanTxRejected = csm::kBoardEventHostCanTxRejectedCode,
  EventHostCanTxAccepted = csm::kBoardEventHostCanTxAcceptedCode,
  EventCan0BackendUnavailable = csm::kBoardEventCan0BackendUnavailableCode,
  EventMcp2515TxFailed = csm::kBoardEventMcp2515TxFailedCode,
  EventSafetyStateChanged = csm::kBoardEventSafetyStateChangedCode,
  EventHostHeartbeat = csm::kBoardEventHostHeartbeatCode,
  EventHostControlSession = csm::kBoardEventHostControlSessionCode,
  EventHostCommandUnsupported = csm::kBoardEventHostCommandUnsupportedCode,
  EventFaultLockoutCleared = csm::kBoardEventFaultLockoutClearedCode,
  EventFirmwareIdentity = csm::kBoardEventFirmwareIdentityCode,
  EventSerialTxBackpressure = csm::kBoardEventSerialTxBackpressureCode,
  EventSerialTxRingClear = csm::kBoardEventSerialTxRingClearCode,
  EventCanRxSegmentEnqueueFailed = csm::kBoardEventCanRxSegmentEnqueueFailedCode,
  EventUsbCdcSessionOpen = csm::kBoardEventUsbCdcSessionOpenCode,
  EventUsbCdcSessionClose = csm::kBoardEventUsbCdcSessionCloseCode,
  EventUsbCdcDtrChange = csm::kBoardEventUsbCdcDtrChangeCode,
  EventUsbHostAbsentCanDiscardSummary = csm::kBoardEventUsbHostAbsentCanDiscardSummaryCode,
  EventMcpPassiveModeReadback = csm::kBoardEventMcpPassiveModeReadbackCode,
  EventMcpPassiveModeViolation = csm::kBoardEventMcpPassiveModeViolationCode,
  EventMcpTxreqViolation = csm::kBoardEventMcpTxreqViolationCode,
  EventTransceiverSafeStateChanged = csm::kBoardEventTransceiverSafeStateChangedCode,
  EventUsbPowerOrResetSuspected = csm::kBoardEventUsbPowerOrResetSuspectedCode,
  EventCanFrontendPresessionHold = csm::kBoardEventCanFrontendPresessionHoldCode,
  EventCanFrontendSessionReady = csm::kBoardEventCanFrontendSessionReadyCode,
  EventCanFrontendSessionInitFailed = csm::kBoardEventCanFrontendSessionInitFailedCode,
  EventCanFrontendFaultHold = csm::kBoardEventCanFrontendFaultHoldCode,
  EventWifiTxBackpressure = csm::kBoardEventWifiTxBackpressureCode,
  EventWifiQueuePressureIsolated =
      csm::kBoardEventWifiQueuePressureIsolatedCode,
  EventWifiClientClosed = csm::kBoardEventWifiClientClosedCode,
  EventRuntimeBreadcrumbRecovered = csm::kBoardEventRuntimeBreadcrumbRecoveredCode,
  EventRemoteControlInitFailed = csm::kBoardEventRemoteControlInitFailedCode,
  EventRemoteControlStateChanged = csm::kBoardEventRemoteControlStateChangedCode,
  EventFeederLinkStarted = csm::kBoardEventFeederLinkStartedCode,
  EventFeederSessionChanged = csm::kBoardEventFeederSessionChangedCode,
  EventFeederSequenceGap = csm::kBoardEventFeederSequenceGapCode,
  EventFeederTransportError = csm::kBoardEventFeederTransportErrorCode,
  EventFeederSourceFault = csm::kBoardEventFeederSourceFaultCode,
  EventFeederLinkStale = csm::kBoardEventFeederLinkStaleCode,
  EventDataLinkIntegrityFault =
      csm::kBoardEventDataLinkIntegrityFaultCode,
};

enum RuntimeBreadcrumbStage : uint8_t {
  RuntimeStageIdle = 0,
  RuntimeStageUsbConnectionPoll = 1,
  RuntimeStageWifiConnectionPoll = 2,
  RuntimeStageCanonicalPublish = 3,
  RuntimeStageUsbTransmit = 4,
  RuntimeStageWifiTransmit = 5,
  RuntimeStageHostDownlink = 6,
  RuntimeStageMcpEntryDrain = 7,
  RuntimeStageMcpInterleaveDrain = 8,
  RuntimeStageMcpMainDrain = 9,
  RuntimeStageBuiltinCanDrain = 10,
  RuntimeStageCanRecordDrain = 11,
  RuntimeStageStatusAndSensors = 12,
  RuntimeStageHealthPublish = 13,
  RuntimeStageSetup = 14,
};

struct RuntimeBreadcrumbSlot {
  uint32_t magic;
  uint32_t firmware_build_id;
  uint32_t write_sequence;
  uint32_t stage;
  uint32_t detail;
  uint32_t uptime_ms;
  uint32_t checksum;
  uint32_t reserved;
};

static_assert(sizeof(RuntimeBreadcrumbSlot) == 32,
              "retained runtime breadcrumb must remain one cache line");

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
enum RuntimeDiagnosticBootPhase : uint8_t {
  RuntimeDiagBootNone = 0,
  RuntimeDiagBootSetupEnter = 1,
  RuntimeDiagBootResetCaptured = 2,
  RuntimeDiagBootSafetyWatchdogReady = 3,
  RuntimeDiagBootUsbReady = 4,
  RuntimeDiagBootWifiReady = 5,
  RuntimeDiagBootPublisherReady = 6,
  RuntimeDiagBootMcpInitEnter = 7,
  RuntimeDiagBootMcpInitReturn = 8,
  RuntimeDiagBootFdcanConstructEnter = 9,
  RuntimeDiagBootFdcanConstructReturn = 10,
  RuntimeDiagBootFdcan500kEnter = 11,
  RuntimeDiagBootFdcan500kReturn = 12,
  RuntimeDiagBootRemoteRuntimeEnter = 13,
  RuntimeDiagBootRemoteRuntimeReturn = 14,
  RuntimeDiagBootM4Issued = 15,
  RuntimeDiagBootSetupComplete = 16,
  RuntimeDiagBootFirstLoop = 17,
};

struct alignas(32) RuntimeDiagnosticRetainedSlot {
  uint32_t magic;
  uint32_t firmware_build_id;
  uint32_t write_sequence;
  uint32_t checksum;
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  uint32_t reserved[4];
};

static_assert(sizeof(RuntimeDiagnosticRetainedSlot) == 160,
              "runtime diagnostic retained slot must cover five cache lines");

#endif

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
static volatile uint32_t can_rx_count_total = 0;
static volatile uint32_t can_rx_dropped_total = 0;
static volatile uint32_t can_fifo_overflow_total = 0;
static volatile uint32_t can_queue_high_water = 0;
static volatile uint32_t can_segment_enqueue_fail_total = 0;
static uint32_t pending_can_segment_enqueue_fail_frames = 0;
static uint32_t host_frame_crc_failed_total = 0;
static uint32_t host_heartbeat_total = 0;
static uint32_t host_control_session_total = 0;
static uint32_t host_control_state_request_total = 0;
static uint32_t __attribute__((unused)) host_can_tx_accepted_total = 0;
static uint32_t host_can_tx_rejected_total = 0;
static uint32_t host_can_tx_transient_rejected_total = 0;
static csm::board::control::HostCommandFreshness host_command_freshness;
static bool host_command_freshness_ok = false;
static csm::board::control::HostControlAuthorityGate host_authority_gate;
static csm::board::control_island::ControlSourceManager control_source_manager;
static csm::board::control_island::ControlHealthPayload control_island_health = {};
static uint32_t control_island_health_sequence = 0;
static uint32_t control_island_health_seen_ms = 0;
static uint32_t control_snapshot_publish_total = 0;
static uint32_t control_snapshot_publish_failed_total = 0;
static uint32_t last_control_snapshot_publish_ms = 0;
static uint32_t last_control_health_emit_ms = 0;
static uint32_t host_control_lease_sequence = 0;
static bool control_island_health_valid = false;

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
static UsbCdcSink usb_cdc_sink;
#if BOARD_ENABLE_WIFI_UPLINK
static_assert(sizeof(WifiTcpSink::TxStorage) == 53248,
              "product Wi-Fi live FIFO storage contract changed");
__attribute__((section(".wifi_tx_queue_dtcm"), aligned(32), used))
static WifiTcpSink::TxStorage wifi_tx_storage;
static WifiTcpSink wifi_tcp_sink(wifi_tx_storage);
#endif
static CanonicalPublisher canonical_publisher;
static CanRxSegmentBuilder can_rx_segment_builder;
#if BOARD_ENABLE_FEEDER_UART
static FeederUartIngress feeder_uart_ingress;
static bool feeder_uart_ready = false;
static bool feeder_session_announced = false;
static bool feeder_operational_last = false;
static bool feeder_stale_latched = true;
static bool feeder_stale_event_pending = false;
static bool feeder_recovery_event_pending = false;
static uint32_t feeder_stale_total = 0;
static uint32_t feeder_last_parser_failures = 0;
static FeederWireStats feeder_last_wire_stats = {};
static FeederUartIngressStats feeder_last_ingress_stats = {};
static FeederStatus feeder_last_source_status = {};
#endif
static uint32_t uplink_tx_bytes_since_can_service = 0;
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
#if BOARD_ENABLE_REMOTE_CONTROL
static csm::board::control::RemoteControlRuntime remote_control_runtime;
static bool remote_control_runtime_ok = false;
static uint32_t last_remote_state_emit_ms = 0;
static uint32_t remote_state_transition_total = 0;
static uint16_t last_remote_state_signature = 0xFFFFu;
#endif

static uint32_t last_health_ms = 0;
#if BOARD_ENABLE_WIFI_UPLINK
static uint32_t last_wifi_transport_diagnostic_ms = 0;
#endif
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
static bool __attribute__((unused)) ack_observe_enabled = false;
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
static uint32_t main_loop_last_entry_us = 0;
static uint32_t main_loop_max_gap_us = 0;
static uint32_t boot_reset_cause_bits = 0;
static uint32_t boot_reset_status_raw = 0;

static bool prepare_boot_recovery_storage(void* context, void* storage,
                                          size_t bytes);
static void commit_boot_recovery_storage(void* context, const void* address,
                                         size_t bytes);
static void boot_recovery_barrier(void* context);

static constexpr uintptr_t kBootRecoveryRetainedOffset = 512u;
static constexpr uintptr_t kBootRecoveryRetainedAddress =
    D3_BKPSRAM_BASE + kBootRecoveryRetainedOffset;
static_assert(kBootRecoveryRetainedOffset +
                      csm::board::diagnostics::BootRecovery::kRequiredStorageBytes <=
                  4096u,
              "boot recovery and deep diagnostics must fit backup SRAM");

static csm::board::diagnostics::RuntimeSupervisor runtime_supervisor(
    reinterpret_cast<void*>(kBootRecoveryRetainedAddress),
    csm::board::diagnostics::BootRecovery::kRequiredStorageBytes,
    csm::board::diagnostics::BootRecoveryConfig{30000u, 2u},
    csm::board::diagnostics::RetainedStorageAdapter{
        nullptr, prepare_boot_recovery_storage, commit_boot_recovery_storage,
        boot_recovery_barrier});
#if BOARD_ENABLE_WIFI_UPLINK
static constexpr uintptr_t kWifiCallLatchRetainedOffset = 3072u;
static constexpr uintptr_t kWifiCallLatchRetainedAddress =
    D3_BKPSRAM_BASE + kWifiCallLatchRetainedOffset;
static_assert(kBootRecoveryRetainedOffset +
                      csm::board::diagnostics::BootRecovery::kRequiredStorageBytes <=
                  kWifiCallLatchRetainedOffset,
              "retained call latch must not overlap BootRecovery");
static_assert(kWifiCallLatchRetainedOffset +
                      csm::board::diagnostics::RetainedCallLatch::kRequiredStorageBytes <=
                  4096u,
              "retained call latch must fit backup SRAM");
static csm::board::diagnostics::RetainedCallLatch wifi_call_latch(
    reinterpret_cast<void*>(kWifiCallLatchRetainedAddress),
    csm::board::diagnostics::RetainedCallLatch::kRequiredStorageBytes,
    csm::board::diagnostics::RetainedStorageAdapter{
        nullptr, prepare_boot_recovery_storage, commit_boot_recovery_storage,
        boot_recovery_barrier});
static csm::board::diagnostics::RetainedCallSnapshot
    previous_wifi_call_latch{};
#endif
static bool runtime_watchdog_requested = false;
static bool runtime_watchdog_start_called = false;
static bool runtime_watchdog_start_succeeded = false;
static bool runtime_watchdog_effective = false;
static bool runtime_watchdog_timeout_matches = false;
static uint32_t runtime_watchdog_observed_timeout_ms = 0;
#if BOARD_ENABLE_WIFI_UPLINK
static csm::board::uplink::WifiRuntimeMode requested_wifi_runtime_mode =
    csm::board::uplink::WifiRuntimeMode::Disabled;
static csm::board::uplink::WifiRuntimeMode effective_wifi_runtime_mode =
    csm::board::uplink::WifiRuntimeMode::Disabled;
#endif

static volatile RuntimeBreadcrumbSlot* const retained_runtime_breadcrumb =
    reinterpret_cast<volatile RuntimeBreadcrumbSlot*>(D3_BKPSRAM_BASE);
static uint32_t runtime_breadcrumb_write_sequence = 0;
static bool previous_runtime_breadcrumb_valid = false;
static bool previous_runtime_breadcrumb_pending = false;
static uint8_t previous_runtime_breadcrumb_stage = RuntimeStageIdle;
static uint8_t previous_runtime_breadcrumb_detail = 0;
static uint32_t previous_runtime_breadcrumb_uptime_ms = 0;
static uint32_t previous_runtime_breadcrumb_sequence = 0;
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
static volatile RuntimeDiagnosticRetainedSlot* const retained_runtime_diagnostic =
    reinterpret_cast<volatile RuntimeDiagnosticRetainedSlot*>(
        D3_BKPSRAM_BASE + sizeof(RuntimeBreadcrumbSlot) * 2u);
static_assert(sizeof(RuntimeBreadcrumbSlot) * 2u +
                      sizeof(RuntimeDiagnosticRetainedSlot) * 2u <=
                  4096u,
              "runtime retained evidence must fit backup SRAM");
static uint32_t runtime_diagnostic_retained_sequence = 0;
static uint8_t runtime_diagnostic_previous_payload[csm::kRuntimeDiagnosticPayloadLen] = {};
static bool runtime_diagnostic_previous_pending = false;
static bool runtime_diagnostic_previous_different_build = false;
static uint8_t runtime_diagnostic_boot_history[18][csm::kRuntimeDiagnosticPayloadLen] = {};
static uint8_t runtime_diagnostic_boot_history_count = 0;
static uint8_t runtime_diagnostic_boot_history_next = 0;
static RuntimeDiagnosticBootPhase runtime_diagnostic_boot_phase = RuntimeDiagBootNone;
static RuntimeBreadcrumbStage runtime_diagnostic_runtime_stage = RuntimeStageIdle;
static uint8_t runtime_diagnostic_runtime_detail = 0;
static uint32_t runtime_diagnostic_attempt_sequence = 0;
static uint32_t runtime_diagnostic_last_can_id_flags = 0;
static uint32_t runtime_diagnostic_last_write_duration_us = 0;
static int32_t runtime_diagnostic_last_write_result = 0;
static bool runtime_diagnostic_write_in_progress = false;
static csm::board::can::BuiltinFdcanSnapshot runtime_diagnostic_before = {};
static csm::board::can::BuiltinFdcanSnapshot runtime_diagnostic_after = {};
static uint32_t runtime_diagnostic_last_periodic_ms = 0;
static bool runtime_diagnostic_first_loop_recorded = false;
static csm::board::diagnostics::BootRecoveryEvent
    runtime_recovery_event_replay[
        csm::board::diagnostics::BootRecovery::kEventSlotCount] = {};
static uint8_t runtime_recovery_event_replay_count = 0;
static uint8_t runtime_recovery_event_replay_next = 0;
static uint32_t runtime_recovery_replay_last_ms = 0;
static bool previous_wifi_call_latch_pending = false;
#endif
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
static uint32_t __attribute__((unused)) last_passive_readback_ms = 0;
static bool can_frontend_session_arm_pending = false;
static bool can_frontend_session_ready = false;
static uint32_t __attribute__((unused)) can_frontend_session_arm_after_ms = 0;
static uint32_t __attribute__((unused)) can_frontend_presession_hold_total = 0;
static uint32_t __attribute__((unused)) can_frontend_session_ready_total = 0;
static uint32_t __attribute__((unused)) can_frontend_session_init_fail_total = 0;
static uint32_t __attribute__((unused)) last_can_frontend_init_attempt_ms = 0;

static constexpr uint32_t kRuntimeBreadcrumbMagic = 0x43534D42u;  // "CSMB"
static constexpr uint32_t kRuntimeBreadcrumbChecksumSeed = 0xA53C91E7u;

static void __attribute__((unused)) clean_retained_dcache(
    const volatile void* address, size_t length) {
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  const void* const nonvolatile = const_cast<const void*>(address);
  const uintptr_t raw_start = reinterpret_cast<uintptr_t>(nonvolatile);
  const uintptr_t start = raw_start & ~static_cast<uintptr_t>(31u);
  const uintptr_t end = (raw_start + length + 31u) & ~static_cast<uintptr_t>(31u);
  SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(start),
                          static_cast<int32_t>(end - start));
  __DSB();
#else
  (void)address;
  (void)length;
#endif
}

static void enable_runtime_retained_storage() {
  RCC->AHB4ENR |= RCC_AHB4ENR_BKPRAMEN;
  (void)RCC->AHB4ENR;
  PWR->CR1 |= PWR_CR1_DBP;
  PWR->CR2 |= PWR_CR2_BREN;
  __DSB();
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
  SCB_InvalidateDCache_by_Addr(
      reinterpret_cast<uint32_t*>(D3_BKPSRAM_BASE), 4096);
  __DSB();
#endif
}

static bool prepare_boot_recovery_storage(void* context, void* storage,
                                          size_t bytes) {
  (void)context;
  const uintptr_t start = reinterpret_cast<uintptr_t>(storage);
  const uintptr_t retained_start = static_cast<uintptr_t>(D3_BKPSRAM_BASE);
  const uintptr_t retained_end = retained_start + 4096u;
  if (start < retained_start || bytes > 4096u || start > retained_end - bytes) {
    return false;
  }
  // This is the first retained-memory operation in setup(). It enables backup
  // SRAM and invalidates stale cache lines before any recovery read.
  enable_runtime_retained_storage();
  return true;
}

static void commit_boot_recovery_storage(void* context, const void* address,
                                         size_t bytes) {
  (void)context;
  clean_retained_dcache(address, bytes);
}

static void boot_recovery_barrier(void* context) {
  (void)context;
  __DMB();
  __DSB();
}

static uint32_t runtime_breadcrumb_checksum(uint32_t firmware_build_id,
                                            uint32_t write_sequence,
                                            uint32_t stage,
                                            uint32_t detail,
                                            uint32_t uptime_ms) {
  return kRuntimeBreadcrumbChecksumSeed ^ kRuntimeBreadcrumbMagic ^
         firmware_build_id ^ write_sequence ^ stage ^ detail ^ uptime_ms;
}

static bool runtime_breadcrumb_slot_valid(
    const volatile RuntimeBreadcrumbSlot& slot) {
  if (slot.magic != kRuntimeBreadcrumbMagic ||
      slot.firmware_build_id != static_cast<uint32_t>(CSM_FW_BUILD_ID)) {
    return false;
  }
  return slot.checksum == runtime_breadcrumb_checksum(
                              slot.firmware_build_id, slot.write_sequence,
                              slot.stage, slot.detail, slot.uptime_ms);
}

static void recover_runtime_breadcrumb() {
  const bool valid0 = runtime_breadcrumb_slot_valid(retained_runtime_breadcrumb[0]);
  const bool valid1 = runtime_breadcrumb_slot_valid(retained_runtime_breadcrumb[1]);
  if (!valid0 && !valid1) return;

  uint8_t selected = 0;
  if (!valid0 ||
      (valid1 && static_cast<int32_t>(retained_runtime_breadcrumb[1].write_sequence -
                                     retained_runtime_breadcrumb[0].write_sequence) > 0)) {
    selected = 1;
  }
  const volatile RuntimeBreadcrumbSlot& slot = retained_runtime_breadcrumb[selected];
  runtime_breadcrumb_write_sequence = slot.write_sequence;
  previous_runtime_breadcrumb_valid = true;
  previous_runtime_breadcrumb_stage = static_cast<uint8_t>(slot.stage & 0xFFu);
  previous_runtime_breadcrumb_detail = static_cast<uint8_t>(slot.detail & 0xFFu);
  previous_runtime_breadcrumb_uptime_ms = slot.uptime_ms;
  previous_runtime_breadcrumb_sequence = slot.write_sequence;
  previous_runtime_breadcrumb_pending = true;
}

static void record_runtime_breadcrumb(RuntimeBreadcrumbStage stage,
                                      uint8_t detail = 0) {
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_runtime_stage = stage;
  runtime_diagnostic_runtime_detail = detail;
#endif
  const uint32_t sequence = ++runtime_breadcrumb_write_sequence;
  volatile RuntimeBreadcrumbSlot& slot = retained_runtime_breadcrumb[sequence & 1u];
  const uint32_t uptime_ms = millis();
  slot.checksum = 0;
  slot.magic = kRuntimeBreadcrumbMagic;
  slot.firmware_build_id = static_cast<uint32_t>(CSM_FW_BUILD_ID);
  slot.write_sequence = sequence;
  slot.stage = static_cast<uint32_t>(stage);
  slot.detail = detail;
  slot.uptime_ms = uptime_ms;
  slot.reserved = 0;
  __DMB();
  slot.checksum = runtime_breadcrumb_checksum(
      slot.firmware_build_id, slot.write_sequence, slot.stage, slot.detail,
      slot.uptime_ms);
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  clean_retained_dcache(&slot, sizeof(slot));
#endif
  __DSB();
}

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
static constexpr uint32_t kRuntimeDiagnosticMagic = 0x43534D44u;  // "CSMD"
static constexpr uint32_t kRuntimeDiagnosticChecksumSeed = 2166136261u;

static uint32_t runtime_diagnostic_checksum(uint32_t firmware_build_id,
                                            uint32_t write_sequence,
                                            const uint8_t* payload) {
  uint32_t hash = kRuntimeDiagnosticChecksumSeed;
  const uint32_t header[] = {kRuntimeDiagnosticMagic, firmware_build_id,
                             write_sequence};
  for (uint32_t word : header) {
    for (uint8_t shift = 0; shift < 32; shift += 8) {
      hash ^= static_cast<uint8_t>(word >> shift);
      hash *= 16777619u;
    }
  }
  for (uint16_t index = 0; index < csm::kRuntimeDiagnosticPayloadLen; ++index) {
    hash ^= payload[index];
    hash *= 16777619u;
  }
  return hash;
}

static bool runtime_diagnostic_slot_valid(
    const volatile RuntimeDiagnosticRetainedSlot& slot) {
  if (slot.magic != kRuntimeDiagnosticMagic) return false;
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  for (uint16_t index = 0; index < sizeof(payload); ++index) {
    payload[index] = slot.payload[index];
  }
  return slot.checksum == runtime_diagnostic_checksum(
                              slot.firmware_build_id, slot.write_sequence,
                              payload);
}

static void recover_runtime_diagnostic() {
  const bool valid0 = runtime_diagnostic_slot_valid(retained_runtime_diagnostic[0]);
  const bool valid1 = runtime_diagnostic_slot_valid(retained_runtime_diagnostic[1]);
  if (!valid0 && !valid1) return;

  uint8_t selected = 0;
  if (!valid0 ||
      (valid1 && static_cast<int32_t>(retained_runtime_diagnostic[1].write_sequence -
                                     retained_runtime_diagnostic[0].write_sequence) > 0)) {
    selected = 1;
  }
  const volatile RuntimeDiagnosticRetainedSlot& slot =
      retained_runtime_diagnostic[selected];
  runtime_diagnostic_retained_sequence = slot.write_sequence;
  for (uint16_t index = 0; index < csm::kRuntimeDiagnosticPayloadLen; ++index) {
    runtime_diagnostic_previous_payload[index] = slot.payload[index];
  }
  runtime_diagnostic_previous_different_build =
      slot.firmware_build_id != static_cast<uint32_t>(CSM_FW_BUILD_ID);
  runtime_diagnostic_previous_pending = true;
}

static void runtime_diagnostic_write_registers(
    uint8_t* payload, uint8_t offset,
    const csm::board::can::BuiltinFdcanSnapshot& snapshot) {
  const uint32_t registers[csm::kRuntimeDiagnosticRegisterCount] = {
      snapshot.cccr, snapshot.psr, snapshot.ecr, snapshot.txfqs,
      snapshot.txbrp, snapshot.txbto, snapshot.txbcf, snapshot.ir};
  for (uint8_t index = 0; index < csm::kRuntimeDiagnosticRegisterCount; ++index) {
    wr_u32_le(&payload[offset + index * 4u], registers[index]);
  }
}

static uint8_t runtime_diagnostic_flags(
    const csm::board::can::BuiltinFdcanSnapshot& after,
    int32_t write_result, bool write_in_progress,
    bool recovered = false, bool different_build = false) {
  uint8_t flags = 0;
  if (after.valid) flags |= csm::kRuntimeDiagnosticFlagFdcanValid;
  if (write_in_progress) flags |= csm::kRuntimeDiagnosticFlagWriteInProgress;
  if (write_result > 0) flags |= csm::kRuntimeDiagnosticFlagWriteAccepted;
  if (after.valid && after.latest_tx_request_mask != 0) {
    const uint32_t mask = after.latest_tx_request_mask;
    if ((after.txbto & mask) != 0) flags |= csm::kRuntimeDiagnosticFlagTxOccurred;
    if ((after.txbrp & mask) != 0) flags |= csm::kRuntimeDiagnosticFlagTxPending;
    if ((after.txbcf & mask) != 0) flags |= csm::kRuntimeDiagnosticFlagTxCancelled;
  }
  if (recovered) flags |= csm::kRuntimeDiagnosticFlagRecovered;
  if (different_build) flags |= csm::kRuntimeDiagnosticFlagDifferentBuild;
  return flags;
}

static void runtime_diagnostic_make_payload(
    uint8_t* payload, uint8_t phase, uint32_t attempt_sequence,
    uint32_t can_id_flags, uint32_t write_duration_us, int32_t write_result,
    bool write_in_progress,
    const csm::board::can::BuiltinFdcanSnapshot& before,
    const csm::board::can::BuiltinFdcanSnapshot& after) {
  memset(payload, 0, csm::kRuntimeDiagnosticPayloadLen);
  wr_u64_le(&payload[csm::kRuntimeDiagnosticMonoUsOffset], mono64_us());
  payload[csm::kRuntimeDiagnosticSchemaOffset] = csm::kRuntimeDiagnosticSchema;
  payload[csm::kRuntimeDiagnosticPhaseOffset] = phase;
  payload[csm::kRuntimeDiagnosticBootPhaseOffset] =
      static_cast<uint8_t>(runtime_diagnostic_boot_phase);
  payload[csm::kRuntimeDiagnosticFlagsOffset] =
      runtime_diagnostic_flags(after, write_result, write_in_progress);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticAttemptSequenceOffset], attempt_sequence);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticCanIdFlagsOffset], can_id_flags);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWriteDurationUsOffset], write_duration_us);
  wr_i32_le(&payload[csm::kRuntimeDiagnosticWriteResultOffset], write_result);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticLatestTxRequestMaskOffset],
            after.valid ? after.latest_tx_request_mask
                        : before.latest_tx_request_mask);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticHalStateOffset],
            after.valid ? after.hal_state : before.hal_state);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticHalErrorOffset],
            after.valid ? after.hal_error : before.hal_error);
  const csm::board::can::BuiltinFdcanSnapshot& fdcan =
      phase == csm::kRuntimeDiagnosticPhaseTxWriteBefore ? before : after;
  runtime_diagnostic_write_registers(
      payload, csm::kRuntimeDiagnosticFdcanRegistersOffset, fdcan);
  wr_u64_le(&payload[csm::kRuntimeDiagnosticBootSessionOffset],
            canonical_publisher.bootSessionId());
  const uint32_t runtime_stage =
      static_cast<uint32_t>(runtime_diagnostic_runtime_stage) |
      (static_cast<uint32_t>(runtime_diagnostic_runtime_detail) << 8u) |
      ((runtime_breadcrumb_write_sequence & 0xFFFFu) << 16u);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticRuntimeStageOffset], runtime_stage);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticFirmwareBuildIdOffset], CSM_FW_BUILD_ID);
#if BOARD_ENABLE_WIFI_UPLINK
  const csm::board::uplink::WifiWorkerCallSnapshot wifi_call =
      wifi_tcp_sink.workerCallSnapshot();
  const csm::board::uplink::WifiWorkerStateSnapshot wifi_state =
      wifi_tcp_sink.workerStateSnapshot();
  payload[csm::kRuntimeDiagnosticWifiCallPhaseOffset] =
      static_cast<uint8_t>(wifi_call.phase);
  uint8_t wifi_flags = 0;
  if (wifi_call.coherent && wifi_call.in_progress) {
    wifi_flags |= csm::kRuntimeDiagnosticWifiCallFlagInProgress;
  }
  if (wifi_tcp_sink.connected()) {
    wifi_flags |= csm::kRuntimeDiagnosticWifiCallFlagSinkConnected;
  }
  const uint32_t wifi_heartbeat_age_ms =
      wifi_tcp_sink.workerHeartbeatAgeMs(millis());
  if (wifi_heartbeat_age_ms >= BOARD_WIFI_CALL_STALL_TIMEOUT_MS) {
    wifi_flags |= csm::kRuntimeDiagnosticWifiCallFlagWorkerHeartbeatStale;
  }
  if (wifi_state.running) {
    wifi_flags |= csm::kRuntimeDiagnosticWifiCallFlagWorkerRunning;
  }
  if (wifi_state.network_ready) {
    wifi_flags |= csm::kRuntimeDiagnosticWifiCallFlagNetworkReady;
  }
  payload[csm::kRuntimeDiagnosticWifiCallFlagsOffset] = wifi_flags;
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiCallSequenceOffset],
            wifi_call.sequence);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiCallStartedMsOffset],
            wifi_call.started_ms);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiCallDurationUsOffset],
            wifi_call.duration_us);
  wr_i32_le(&payload[csm::kRuntimeDiagnosticWifiCallResultOffset],
            wifi_call.result);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiWorkerHeartbeatAgeMsOffset],
            wifi_heartbeat_age_ms);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiCallStallTotalOffset],
            wifi_tcp_sink.counters().tx_worker_stall_total);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiConnectionEpochOffset],
            wifi_tcp_sink.counters().connection_epoch);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiWorkerStackFreeOffset],
            wifi_state.stack_free_bytes);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiWorkerStackMaxUsedOffset],
            wifi_state.stack_max_used_bytes);
#endif
}

static void runtime_diagnostic_commit_payload(const uint8_t* payload) {
  const uint32_t sequence = ++runtime_diagnostic_retained_sequence;
  volatile RuntimeDiagnosticRetainedSlot& slot =
      retained_runtime_diagnostic[sequence & 1u];
  slot.checksum = 0;
  slot.magic = kRuntimeDiagnosticMagic;
  slot.firmware_build_id = static_cast<uint32_t>(CSM_FW_BUILD_ID);
  slot.write_sequence = sequence;
  for (uint16_t index = 0; index < csm::kRuntimeDiagnosticPayloadLen; ++index) {
    slot.payload[index] = payload[index];
  }
  for (uint8_t index = 0; index < 4; ++index) slot.reserved[index] = 0;
  __DMB();
  slot.checksum = runtime_diagnostic_checksum(
      slot.firmware_build_id, slot.write_sequence, payload);
  __DMB();
  clean_retained_dcache(&slot, sizeof(slot));
}

static void runtime_diagnostic_boot_checkpoint(RuntimeDiagnosticBootPhase phase) {
  runtime_diagnostic_boot_phase = phase;
  const csm::board::can::BuiltinFdcanSnapshot current =
      builtin_fdcan_diagnostics.snapshot();
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  runtime_diagnostic_make_payload(
      payload, csm::kRuntimeDiagnosticPhaseBoot,
      runtime_diagnostic_attempt_sequence, runtime_diagnostic_last_can_id_flags,
      runtime_diagnostic_last_write_duration_us,
      runtime_diagnostic_last_write_result, false, current, current);
  runtime_diagnostic_commit_payload(payload);
  if (runtime_diagnostic_boot_history_count <
      static_cast<uint8_t>(sizeof(runtime_diagnostic_boot_history) /
                           sizeof(runtime_diagnostic_boot_history[0]))) {
    memcpy(runtime_diagnostic_boot_history[runtime_diagnostic_boot_history_count],
           payload, sizeof(payload));
    ++runtime_diagnostic_boot_history_count;
  }
}

static void runtime_diagnostic_before_can_write(uint32_t can_id_flags) {
  runtime_diagnostic_last_can_id_flags = can_id_flags;
  runtime_diagnostic_last_write_duration_us = 0;
  runtime_diagnostic_last_write_result = INT32_MIN;
  runtime_diagnostic_write_in_progress = true;
  ++runtime_diagnostic_attempt_sequence;
  runtime_diagnostic_before = builtin_fdcan_diagnostics.snapshot();
  runtime_diagnostic_after = runtime_diagnostic_before;
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  runtime_diagnostic_make_payload(
      payload, csm::kRuntimeDiagnosticPhaseTxWriteBefore,
      runtime_diagnostic_attempt_sequence, can_id_flags, 0, INT32_MIN, true,
      runtime_diagnostic_before, runtime_diagnostic_before);
  runtime_diagnostic_commit_payload(payload);
}

static void runtime_diagnostic_after_can_write(uint32_t started_us,
                                               int32_t write_result) {
  runtime_diagnostic_last_write_duration_us = micros() - started_us;
  runtime_diagnostic_last_write_result = write_result;
  runtime_diagnostic_write_in_progress = false;
  runtime_diagnostic_after = builtin_fdcan_diagnostics.snapshot();
  if (write_result <= 0) {
    runtime_diagnostic_after.latest_tx_request_mask = 0;
  }
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  runtime_diagnostic_make_payload(
      payload, csm::kRuntimeDiagnosticPhaseTxWriteReturn,
      runtime_diagnostic_attempt_sequence, runtime_diagnostic_last_can_id_flags,
      runtime_diagnostic_last_write_duration_us, write_result, false,
      runtime_diagnostic_before, runtime_diagnostic_after);
  runtime_diagnostic_commit_payload(payload);
}
#endif

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
#elif BOARD_CSM_PROFILE_REMOTE_PRODUCT
  return kFirmwareProfileRemoteProduct;
#elif BOARD_CSM_PROFILE_FULL_INSTRUMENTED
  return kFirmwareProfileFullInstrumented;
#else
  return kFirmwareProfileUnknown;
#endif
}

static void discard_can_queue_for_session_quarantine();
static void set_can_observe_mode_for_session(bool enabled, bool force = false);
static bool init_can_backend();
static bool control_island_runtime_ready(uint32_t now_ms);

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
  canonical_publisher.discardQueuedRecords();
  usb_cdc_sink.abortQueuedFrames();
  // The Wi-Fi worker clears its TX queue synchronously on both accept and
  // close. Posting another asynchronous abort here can race the new epoch's
  // STREAM_SESSION into the queue and delete that anchor after acceptance.
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
  return canonical_publisher.hasConnectedSink();
}

static bool usb_cdc_dtr_asserted() {
#if defined(SERIAL_CDC)
  return _SerialUSB.dtr();
#else
  return true;
#endif
}

static constexpr uint8_t kUsbSessionSinkMask = (1u << 0);
static constexpr uint8_t kWifiSessionSinkMask = (1u << 1);
static uint32_t last_wifi_capability_epoch = 0;
static bool pending_wifi_close_event = false;
static uint8_t pending_wifi_close_reason = 0;
static uint32_t pending_wifi_disconnect_total = 0;
static uint32_t observed_wifi_disconnect_total = 0;
static uint32_t observed_wifi_live_fifo_loss_total = 0;
static void emit_capability();

static void request_connection_session(bool usb_epoch_changed,
                                       bool wifi_epoch_changed) {
  uint8_t session_targets = 0;
  if (usb_epoch_changed && usb_cdc_sink.connected()) {
    session_targets |= kUsbSessionSinkMask;
  }
#if BOARD_ENABLE_WIFI_UPLINK
  bool refresh_wifi_capability = false;
  if (wifi_epoch_changed && wifi_tcp_sink.socketConnected()) {
    // The closed epoch's unsent copies were discarded. Publish a fresh full
    // sequence anchor before this connection's current live queue.
    session_targets |= kWifiSessionSinkMask;
    const uint32_t epoch = wifi_tcp_sink.counters().connection_epoch;
    if (epoch != last_wifi_capability_epoch) {
      last_wifi_capability_epoch = epoch;
      refresh_wifi_capability = true;
    }
  }
#endif
  if (session_targets != 0) {
    canonical_publisher.requestSessionAnnouncement(
        SessionAnnouncementReason::SinkEpochChanged, session_targets);
  }
#if BOARD_ENABLE_WIFI_UPLINK
  if (refresh_wifi_capability) emit_capability();
#endif
}

static void poll_uplink_connections(
    uint32_t now_ms,
    csm::board::uplink::SinkServiceResult* usb_poll_result = nullptr,
    csm::board::uplink::SinkServiceResult* wifi_poll_result = nullptr) {
  record_runtime_breadcrumb(RuntimeStageUsbConnectionPoll);
  const csm::board::uplink::SinkServiceResult usb_poll =
      usb_cdc_sink.service(0, now_ms, micros());
  if (usb_poll_result != nullptr) *usb_poll_result = usb_poll;
  record_runtime_breadcrumb(RuntimeStageIdle);
#if BOARD_ENABLE_WIFI_UPLINK
  record_runtime_breadcrumb(RuntimeStageWifiConnectionPoll);
  const csm::board::uplink::SinkServiceResult wifi_poll =
      wifi_tcp_sink.service(0, now_ms, micros());
  if (wifi_poll_result != nullptr) *wifi_poll_result = wifi_poll;
  record_runtime_breadcrumb(RuntimeStageIdle);
#endif
  request_connection_session(usb_poll.epoch_changed, wifi_poll.epoch_changed);
}

static void merge_sink_service_result(
    csm::board::uplink::SinkServiceResult& target,
    const csm::board::uplink::SinkServiceResult& source) {
  target.actual_bytes += source.actual_bytes;
  target.frames_completed += source.frames_completed;
  target.epoch_changed = target.epoch_changed || source.epoch_changed;
  if (source.backpressure_event) {
    target.backpressure_event = true;
    target.backpressure_duration_ms = source.backpressure_duration_ms;
  }
  target.queue_pressure_event =
      target.queue_pressure_event || source.queue_pressure_event;
}

static void service_uplink(uint32_t byte_budget = BOARD_SERIAL_TX_MAX_BYTES_PER_PUMP) {
  const uint32_t now_ms = millis();
  csm::board::uplink::SinkServiceResult usb_poll_result;
  csm::board::uplink::SinkServiceResult wifi_poll_result;
  poll_uplink_connections(now_ms, &usb_poll_result, &wifi_poll_result);
  record_runtime_breadcrumb(RuntimeStageCanonicalPublish);
  const csm::board::uplink::PublishServiceResult publish_result =
      canonical_publisher.service(mono64_us());
#if BOARD_ENABLE_WIFI_UPLINK
  if (publish_result.session_record &&
      (publish_result.missed_required_sink_mask & kWifiSessionSinkMask) != 0) {
    // The connection cannot consume any normal canonical record without the
    // exact full-sequence anchor for its epoch. Close only this Wi-Fi epoch;
    // USB and the global publisher sequence continue independently.
    wifi_tcp_sink.isolateMissedSessionAnchor(publish_result.publish_seq);
  }
#endif
  record_runtime_breadcrumb(RuntimeStageIdle);
  record_runtime_breadcrumb(RuntimeStageUsbTransmit);
  csm::board::uplink::SinkServiceResult usb_result =
      usb_cdc_sink.service(byte_budget, now_ms, micros());
  merge_sink_service_result(usb_result, usb_poll_result);
  record_runtime_breadcrumb(RuntimeStageIdle);
#if BOARD_ENABLE_WIFI_UPLINK
  record_runtime_breadcrumb(RuntimeStageWifiTransmit);
  csm::board::uplink::SinkServiceResult wifi_result =
      wifi_tcp_sink.service(byte_budget, now_ms, micros());
  merge_sink_service_result(wifi_result, wifi_poll_result);
  record_runtime_breadcrumb(RuntimeStageIdle);
#endif
  request_connection_session(usb_result.epoch_changed,
                             wifi_result.epoch_changed);
  if (usb_result.backpressure_event) {
    emit_board_event(EventSerialTxBackpressure,
                     static_cast<uint16_t>(usb_result.backpressure_duration_ms & 0xFFFF),
                     usb_cdc_sink.counters().backpressure_total);
  }
#if BOARD_ENABLE_WIFI_UPLINK
  if (wifi_result.backpressure_event && wifi_result.epoch_changed) {
    const uint32_t duration_ms = wifi_result.backpressure_duration_ms;
    emit_board_event(
        EventWifiTxBackpressure,
        static_cast<uint16_t>(duration_ms > 0xFFFFu ? 0xFFFFu : duration_ms),
        wifi_tcp_sink.counters().stall_close_total);
  }
  if (wifi_result.queue_pressure_event) {
    emit_board_event(EventWifiQueuePressureIsolated, 0,
                     wifi_tcp_sink.counters().queue_pressure_close_total);
  }
  const auto& wifi_counters = wifi_tcp_sink.counters();
  if (wifi_counters.live_fifo_loss_total !=
      observed_wifi_live_fifo_loss_total) {
    observed_wifi_live_fifo_loss_total = wifi_counters.live_fifo_loss_total;
    emit_board_event(
        EventDataLinkIntegrityFault,
        static_cast<uint16_t>(
            wifi_counters.first_lost_publish_seq & 0xFFFFu),
        wifi_counters.live_fifo_loss_total);
  }
  const uint32_t wifi_disconnect_total =
      wifi_tcp_sink.counters().disconnect_total;
  if (wifi_disconnect_total != observed_wifi_disconnect_total) {
    observed_wifi_disconnect_total = wifi_disconnect_total;
    pending_wifi_close_event = true;
    pending_wifi_close_reason =
        static_cast<uint8_t>(wifi_tcp_sink.lastCloseReason());
    pending_wifi_disconnect_total = wifi_disconnect_total;
  }
  if (pending_wifi_close_event && uplink_host_session_open() &&
      emit_board_event(EventWifiClientClosed, pending_wifi_close_reason,
                       pending_wifi_disconnect_total)) {
    pending_wifi_close_event = false;
  }
  uplink_tx_bytes_since_can_service +=
      usb_result.actual_bytes + wifi_result.actual_bytes;
#else
  uplink_tx_bytes_since_can_service += usb_result.actual_bytes;
#endif
  while (uplink_tx_bytes_since_can_service >= BOARD_SERIAL_TX_CAN_INTERLEAVE_BYTES) {
    uplink_tx_bytes_since_can_service -= BOARD_SERIAL_TX_CAN_INTERLEAVE_BYTES;
    if (!kTestMode) {
      record_runtime_breadcrumb(RuntimeStageMcpInterleaveDrain);
      pump_can_rx_to_queue(BOARD_SERIAL_TX_CAN_INTERLEAVE_MCP_BUDGET);
      record_runtime_breadcrumb(RuntimeStageIdle);
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
  return canonical_publisher.enqueueRecord(type, payload, len, priority, flags);
#endif
}

static bool uplink_boot_quiet_active() {
  return static_cast<uint32_t>(millis() - uplink_boot_ms) < BOARD_UPLINK_BOOT_QUIET_MS;
}

static bool uplink_tx_pressure_active() {
  return usb_cdc_sink.backpressureActive() ||
         usb_cdc_sink.hasPendingFrames() ||
#if BOARD_ENABLE_WIFI_UPLINK
         wifi_tcp_sink.backpressureActive() ||
         wifi_tcp_sink.hasPendingFrames() ||
#endif
         canonical_publisher.pressureActive() ||
         canonical_publisher.queuedPayloadBytes() >= BOARD_SERIAL_TX_NORMAL_LOW_WATER_BYTES;
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
#if BOARD_ENABLE_WIFI_UPLINK
  if (type == RecordType::TransportDiagnostic) {
    // This 1 Hz record is the evidence that distinguishes producer, queue,
    // socket, and peer loss. Keep it subject to the bounded diagnostic
    // admission lane, but do not hide it merely because a sink has backlog.
    return false;
  }
#endif
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  if (type == RecordType::RuntimeDiagnostic) {
    // The first FDCAN outcomes are the evidence under test. Keep the bounded
    // diagnostic lane active during boot, but still yield under real pressure.
    return uplink_tx_pressure_active();
  }
#endif
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

#if BOARD_ENABLE_WIFI_UPLINK
static void emit_wifi_transport_diagnostic(uint32_t now_ms) {
#if BOARD_ENABLE_WIFI_DEEP_DIAGNOSTICS
  uint8_t payload[csm::kTransportDiagnosticPayloadLen] = {};
  const auto snapshot =
      wifi_tcp_sink.diagnosticSnapshot(mono64_us(), now_ms);
  const uint16_t length =
      csm::board::uplink::build_wifi_transport_diagnostic_payload(
          snapshot, payload, sizeof(payload));
  if (length == sizeof(payload)) {
    emit_record(RecordType::TransportDiagnostic, payload, length,
                UplinkPriority::Diagnostic);
  }
#else
  (void)now_ms;
#endif
}
#endif

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
static void service_runtime_diagnostic_recovery_replay() {
  if (!uplink_host_session_open()) return;

#if BOARD_ENABLE_WIFI_UPLINK
  if (previous_wifi_call_latch_pending) {
    uint8_t payload[csm::kRuntimeDiagnosticPayloadLen] = {};
    wr_u64_le(&payload[csm::kRuntimeDiagnosticMonoUsOffset], mono64_us());
    payload[csm::kRuntimeDiagnosticSchemaOffset] =
        csm::kRuntimeDiagnosticSchema;
    payload[csm::kRuntimeDiagnosticPhaseOffset] =
        csm::kRuntimeDiagnosticPhaseRecoveredWifiCall;
    payload[csm::kRuntimeDiagnosticBootPhaseOffset] =
        static_cast<uint8_t>(previous_wifi_call_latch.owner);
    payload[csm::kRuntimeDiagnosticFlagsOffset] =
        csm::kRuntimeDiagnosticFlagRecovered;
    wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveredCallBootSequenceOffset],
              previous_wifi_call_latch.boot_sequence);
    wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveredCallCompletedMsOffset],
              previous_wifi_call_latch.completed_uptime_ms);
    wr_u64_le(&payload[csm::kRuntimeDiagnosticRecoveredCallContractIdOffset],
              previous_wifi_call_latch.runtime_contract_id);
    payload[csm::kRuntimeDiagnosticWifiCallPhaseOffset] =
        static_cast<uint8_t>(previous_wifi_call_latch.operation);
    uint8_t call_flags = csm::kRuntimeDiagnosticWifiCallFlagRecovered;
    if (previous_wifi_call_latch.in_progress) {
      call_flags |= csm::kRuntimeDiagnosticWifiCallFlagInProgress;
    }
    if (previous_wifi_call_latch.completed) {
      call_flags |= csm::kRuntimeDiagnosticWifiCallFlagCompleted;
    }
    if (previous_wifi_call_latch.contract_changed) {
      call_flags |= csm::kRuntimeDiagnosticWifiCallFlagContractChanged;
    }
    payload[csm::kRuntimeDiagnosticWifiCallFlagsOffset] = call_flags;
    wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiCallSequenceOffset],
              previous_wifi_call_latch.call_sequence);
    wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiCallStartedMsOffset],
              previous_wifi_call_latch.started_uptime_ms);
    wr_u32_le(&payload[csm::kRuntimeDiagnosticWifiCallDurationUsOffset],
              previous_wifi_call_latch.duration_us);
    wr_i32_le(&payload[csm::kRuntimeDiagnosticWifiCallResultOffset],
              previous_wifi_call_latch.result);
    wr_u64_le(&payload[csm::kRuntimeDiagnosticBootSessionOffset],
              canonical_publisher.bootSessionId());
    wr_u32_le(&payload[csm::kRuntimeDiagnosticFirmwareBuildIdOffset],
              CSM_FW_BUILD_ID);
    if (emit_record(RecordType::RuntimeDiagnostic, payload, sizeof(payload),
                    UplinkPriority::Critical)) {
      previous_wifi_call_latch_pending = false;
    }
    return;
  }
#endif

  if (runtime_recovery_event_replay_next >=
      runtime_recovery_event_replay_count) {
    return;
  }
  const uint32_t now_ms = millis();
  if (static_cast<uint32_t>(now_ms - runtime_recovery_replay_last_ms) < 20u) {
    return;
  }
  const csm::board::diagnostics::BootRecoveryEvent& event =
      runtime_recovery_event_replay[runtime_recovery_event_replay_next];
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen] = {};
  wr_u64_le(&payload[csm::kRuntimeDiagnosticMonoUsOffset], mono64_us());
  payload[csm::kRuntimeDiagnosticSchemaOffset] = csm::kRuntimeDiagnosticSchema;
  payload[csm::kRuntimeDiagnosticPhaseOffset] =
      csm::kRuntimeDiagnosticPhaseRecoveryEvent;
  payload[csm::kRuntimeDiagnosticRecoveryEventTypeOffset] =
      static_cast<uint8_t>(event.type);
  payload[csm::kRuntimeDiagnosticFlagsOffset] =
      csm::kRuntimeDiagnosticFlagRecovered;
  wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveryEventSequenceOffset],
            event.sequence);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveryEventBootSequenceOffset],
            event.boot_sequence);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveryEventUptimeMsOffset],
            event.uptime_ms);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveryEventValueOffset],
            event.value);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveryEventCodeOffset],
            event.code);
  wr_u32_le(&payload[csm::kRuntimeDiagnosticRecoveryEventIdentityTagOffset],
            event.firmware_identity_tag);
  wr_u64_le(&payload[csm::kRuntimeDiagnosticBootSessionOffset],
            canonical_publisher.bootSessionId());
  wr_u32_le(&payload[csm::kRuntimeDiagnosticFirmwareBuildIdOffset],
            CSM_FW_BUILD_ID);
  if (emit_record(RecordType::RuntimeDiagnostic, payload, sizeof(payload),
                  UplinkPriority::Diagnostic)) {
    ++runtime_recovery_event_replay_next;
    runtime_recovery_replay_last_ms = now_ms;
  }
}

static void service_runtime_diagnostic_recovered() {
  if (!runtime_diagnostic_previous_pending || !uplink_host_session_open()) return;
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  memcpy(payload, runtime_diagnostic_previous_payload, sizeof(payload));
  payload[csm::kRuntimeDiagnosticPhaseOffset] =
      csm::kRuntimeDiagnosticPhaseRecovered;
  payload[csm::kRuntimeDiagnosticFlagsOffset] |=
      csm::kRuntimeDiagnosticFlagRecovered;
  if (runtime_diagnostic_previous_different_build) {
    payload[csm::kRuntimeDiagnosticFlagsOffset] |=
        csm::kRuntimeDiagnosticFlagDifferentBuild;
  }
  if (emit_record(RecordType::RuntimeDiagnostic, payload, sizeof(payload),
                  UplinkPriority::Critical)) {
    runtime_diagnostic_previous_pending = false;
  }
}

static void service_runtime_diagnostic_boot_history() {
  if (runtime_diagnostic_boot_history_next >=
      runtime_diagnostic_boot_history_count || !uplink_host_session_open()) {
    return;
  }
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  memcpy(payload, runtime_diagnostic_boot_history[runtime_diagnostic_boot_history_next],
         sizeof(payload));
  wr_u64_le(&payload[csm::kRuntimeDiagnosticBootSessionOffset],
            canonical_publisher.bootSessionId());
  if (emit_record(RecordType::RuntimeDiagnostic, payload, sizeof(payload),
                  UplinkPriority::Diagnostic)) {
    ++runtime_diagnostic_boot_history_next;
  }
}

static void service_runtime_diagnostics() {
  service_runtime_diagnostic_recovery_replay();
  service_runtime_diagnostic_recovered();
  service_runtime_diagnostic_boot_history();

  const uint32_t now_ms = millis();
  if (static_cast<uint32_t>(now_ms - runtime_diagnostic_last_periodic_ms) <
      BOARD_RUNTIME_DIAGNOSTIC_PERIOD_MS) {
    return;
  }
  runtime_diagnostic_last_periodic_ms = now_ms;
  const csm::board::can::BuiltinFdcanSnapshot current =
      builtin_fdcan_diagnostics.snapshot();
  uint8_t payload[csm::kRuntimeDiagnosticPayloadLen];
  runtime_diagnostic_make_payload(
      payload, csm::kRuntimeDiagnosticPhasePeriodic,
      runtime_diagnostic_attempt_sequence, runtime_diagnostic_last_can_id_flags,
      runtime_diagnostic_last_write_duration_us,
      runtime_diagnostic_last_write_result,
      runtime_diagnostic_write_in_progress, current, current);
  runtime_diagnostic_commit_payload(payload);
  emit_record(RecordType::RuntimeDiagnostic, payload, sizeof(payload),
              UplinkPriority::Diagnostic);
}
#endif

#if BOARD_ENABLE_FEEDER_UART
static bool feeder_source_operational(uint64_t now_mono_us) {
  if (!feeder_uart_ready || !feeder_session_announced ||
      !feeder_uart_ingress.initialized() ||
      feeder_uart_ingress.stale(now_mono_us) ||
      !feeder_uart_ingress.statusFresh(now_mono_us)) {
    return false;
  }
  const FeederUartIngressStats ingress =
      feeder_uart_ingress.ingressStats();
  return ingress.dma_cursor_faults == 0U &&
         csm::board::feeder::feederSourceHealthy(
             feeder_uart_ingress.feederStatus());
}
#endif

#if BOARD_ENABLE_STATUS_LED
static void init_status_led() {
  status_led.begin();
}

static bool required_can_lanes_ok() {
  bool any_required = false;
  bool all_ready = true;
#if BOARD_ENABLE_MCP2515_INIT
  any_required = true;
  all_ready = all_ready && can_backend_ok;
#endif
#if BOARD_ENABLE_FEEDER_UART
  any_required = true;
  all_ready = all_ready && feeder_source_operational(mono64_us());
#endif
#if BOARD_ENABLE_CONTROL_ISLAND
  any_required = true;
  all_ready = all_ready && control_island_runtime_ready(millis());
#endif
  return any_required && all_ready;
}

static void service_status_led() {
  status_led.service(required_can_lanes_ok());
}
#else
static void init_status_led() {}
static void service_status_led() {}
#endif

static void record_boot_progress(csm::board::diagnostics::BootProgress progress,
                                 uint32_t detail = 0) {
  runtime_supervisor.recordProgress(progress, detail, millis());
}

#if BOARD_ENABLE_WIFI_UPLINK
static csm::board::uplink::WifiRuntimeMode to_wifi_runtime_mode(
    csm::board::diagnostics::ResetExperimentWifiRuntimeMode selected) {
  using ExperimentMode =
      csm::board::diagnostics::ResetExperimentWifiRuntimeMode;
  using RuntimeMode = csm::board::uplink::WifiRuntimeMode;
  switch (selected) {
    case ExperimentMode::Off:
      return RuntimeMode::Disabled;
    case ExperimentMode::ApOnly:
      return RuntimeMode::AccessPointOnly;
    case ExperimentMode::Full:
      return RuntimeMode::FullTcp;
  }
  return RuntimeMode::Disabled;
}
#endif

#if BOARD_ENABLE_WIFI_UPLINK
static constexpr uint16_t kRetainedCallOwnerWifiVendor = 1u;

static void persist_wifi_call_enter(void* context, uint8_t operation,
                                    uint32_t sequence,
                                    uint32_t started_ms) {
  if (context == nullptr) return;
  static_cast<csm::board::diagnostics::RetainedCallLatch*>(context)->enter(
      kRetainedCallOwnerWifiVendor, operation, sequence, started_ms);
}

static void persist_wifi_call_leave(void* context, int32_t result,
                                    uint32_t duration_us,
                                    uint32_t completed_ms) {
  if (context == nullptr) return;
  static_cast<csm::board::diagnostics::RetainedCallLatch*>(context)->leave(
      result, duration_us, completed_ms);
}
#endif

static void service_boot_recovery() {
  const uint32_t now_ms = millis();
  csm::board::diagnostics::RuntimeSupervisorObservation observation;
  observation.now_ms = now_ms;
  observation.watchdog_effective = runtime_watchdog_effective;
  observation.watchdog_start_succeeded = runtime_watchdog_start_succeeded;
  observation.watchdog_timeout_matches = runtime_watchdog_timeout_matches;

#if BOARD_ENABLE_WIFI_UPLINK
  const csm::board::uplink::WifiWorkerCallSnapshot wifi_call =
      wifi_tcp_sink.workerCallSnapshot();
  observation.wifi_present = true;
  observation.wifi_call_phase = static_cast<uint8_t>(wifi_call.phase);
  observation.wifi_call_in_progress =
      wifi_call.coherent && wifi_call.in_progress;
  observation.wifi_call_slow =
      wifi_call.coherent && wifi_call.in_progress &&
      csm::board::uplink::wifiObservedAgeMs(
          now_ms, wifi_call.started_ms) >=
          BOARD_WIFI_CALL_STALL_TIMEOUT_MS;
  observation.wifi_call_sequence = wifi_call.sequence;
  observation.wifi_worker_heartbeat_age_ms =
      wifi_tcp_sink.workerHeartbeatAgeMs(now_ms);
#endif
  runtime_supervisor.service(observation);
}

static void init_runtime_watchdog() {
  runtime_watchdog_start_called = false;
  runtime_watchdog_start_succeeded = false;
  runtime_watchdog_effective = false;
  runtime_watchdog_timeout_matches = false;
  runtime_watchdog_observed_timeout_ms = 0;
#if BOARD_ENABLE_RUNTIME_WATCHDOG
  mbed::Watchdog& watchdog = mbed::Watchdog::get_instance();
  if (runtime_watchdog_requested) {
    runtime_watchdog_start_called = true;
    runtime_watchdog_start_succeeded =
        watchdog.start(BOARD_RUNTIME_WATCHDOG_TIMEOUT_MS);
  }
  runtime_watchdog_effective = watchdog.is_running();
  if (runtime_watchdog_effective) {
    runtime_watchdog_observed_timeout_ms = watchdog.get_timeout();
    runtime_watchdog_timeout_matches =
        runtime_watchdog_requested &&
        runtime_watchdog_observed_timeout_ms ==
            BOARD_RUNTIME_WATCHDOG_TIMEOUT_MS;
  }
#else
  runtime_watchdog_requested = false;
#endif
}

static void capture_boot_reset_cause() {
#if DEVICE_RESET_REASON
  const reset_reason_t reason = mbed::ResetReason::get();
  boot_reset_status_raw = mbed::ResetReason::get_raw();
  switch (reason) {
    case RESET_REASON_POWER_ON:
      boot_reset_cause_bits |= csm::kResetCausePowerOn;
      break;
    case RESET_REASON_PIN_RESET:
      boot_reset_cause_bits |= csm::kResetCausePin;
      break;
    case RESET_REASON_BROWN_OUT:
      boot_reset_cause_bits |= csm::kResetCauseBrownout;
      break;
    case RESET_REASON_SOFTWARE:
      boot_reset_cause_bits |= csm::kResetCauseSoftware;
      break;
    case RESET_REASON_WATCHDOG:
      boot_reset_cause_bits |= csm::kResetCauseIndependentWatchdog;
      break;
    case RESET_REASON_WAKE_LOW_POWER:
      boot_reset_cause_bits |= csm::kResetCauseLowPower;
      break;
    case RESET_REASON_UNKNOWN:
      boot_reset_cause_bits |= csm::kResetCauseUnknown;
      break;
    default:
      break;
  }
#elif defined(RCC)
  boot_reset_status_raw = RCC->RSR;
#endif

#if defined(RCC_RSR_PORRSTF)
  if ((boot_reset_status_raw & RCC_RSR_PORRSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCausePowerOn;
  }
#endif
#if defined(RCC_RSR_BORRSTF)
  if ((boot_reset_status_raw & RCC_RSR_BORRSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseBrownout;
  }
#endif
#if defined(RCC_RSR_PINRSTF)
  if ((boot_reset_status_raw & RCC_RSR_PINRSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCausePin;
  }
#endif
#if defined(RCC_RSR_SFTRSTF)
  if ((boot_reset_status_raw & RCC_RSR_SFTRSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseSoftware;
  }
#endif
#if defined(RCC_RSR_IWDG1RSTF)
  if ((boot_reset_status_raw & RCC_RSR_IWDG1RSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseIndependentWatchdog;
  }
#endif
#if defined(RCC_RSR_WWDG1RSTF)
  if ((boot_reset_status_raw & RCC_RSR_WWDG1RSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseWindowWatchdog;
  }
#endif
#if defined(RCC_RSR_CPURSTF)
  if ((boot_reset_status_raw & RCC_RSR_CPURSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseCpu;
  }
#endif
#if defined(RCC_RSR_D1RSTF)
  if ((boot_reset_status_raw & RCC_RSR_D1RSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseDomain1;
  }
#endif
#if defined(RCC_RSR_D2RSTF)
  if ((boot_reset_status_raw & RCC_RSR_D2RSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseDomain2;
  }
#endif
#if defined(RCC_RSR_LPWR1RSTF)
  if ((boot_reset_status_raw & RCC_RSR_LPWR1RSTF) != 0) {
    boot_reset_cause_bits |= csm::kResetCauseLowPower;
  }
#endif
  if (boot_reset_status_raw != 0 && boot_reset_cause_bits == 0) {
    boot_reset_cause_bits = csm::kResetCauseUnknown;
  }
#if !DEVICE_RESET_REASON && defined(RCC_RSR_RMVF)
  __HAL_RCC_CLEAR_RESET_FLAGS();
#endif
}

static void kick_runtime_watchdog() {
#if BOARD_ENABLE_RUNTIME_WATCHDOG
  if (runtime_watchdog_effective) mbed::Watchdog::get_instance().kick();
#endif
}

static bool emit_board_event_with_priority(uint16_t code, uint16_t detail, uint32_t counter,
                                           UplinkPriority priority) {
  uint8_t payload[16];
  wr_u64_le(&payload[csm::kBoardEventMonoUsOffset], mono64_us());
  wr_u16_le(&payload[csm::kBoardEventCodeOffset], code);
  wr_u16_le(&payload[csm::kBoardEventDetailOffset], detail);
  wr_u32_le(&payload[csm::kBoardEventCounterOffset], counter);
  return emit_record(RecordType::BoardEvent, payload, sizeof(payload), priority);
}

static bool emit_board_event(uint16_t code, uint16_t detail, uint32_t counter) {
  return emit_board_event_with_priority(
      code, detail, counter, csm::board::uplink::priority_for_board_event(code));
}

static void service_recovered_runtime_breadcrumb() {
  if (!previous_runtime_breadcrumb_pending || !uplink_host_session_open()) return;
  const uint16_t detail =
      static_cast<uint16_t>(previous_runtime_breadcrumb_stage) |
      static_cast<uint16_t>(previous_runtime_breadcrumb_detail << 8);
  if (emit_board_event_with_priority(EventRuntimeBreadcrumbRecovered, detail,
                                     previous_runtime_breadcrumb_uptime_ms,
                                     UplinkPriority::Critical)) {
    previous_runtime_breadcrumb_pending = false;
  }
}

static void note_pending_can_segment_enqueue_fail(uint32_t frames) {
  const uint32_t remaining = UINT32_MAX - pending_can_segment_enqueue_fail_frames;
  pending_can_segment_enqueue_fail_frames += frames > remaining ? remaining : frames;
}

static void service_deferred_loss_events() {
  if (pending_can_segment_enqueue_fail_frames == 0) {
    return;
  }
  if (!canonical_publisher.hasQueueSpace(UplinkPriority::Critical)) {
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
using csm::ControlReasonRateLimited;
using csm::ControlReasonTxBusy;
using csm::ControlReasonSafetyLockout;
using csm::ControlReasonAuthorityDenied;
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

#if BOARD_HW_PROFILE_MID_MCP2515 || BOARD_HW_PROFILE_MID_FEEDER_UART || \
    BOARD_HW_PROFILE_MID_TJA1051_DUAL
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
#elif BOARD_HW_PROFILE_MID_FEEDER_UART
  config.profile_major = 4;   // Mid Carrier + isolated RP2040 CAN feeder
#elif BOARD_HW_PROFILE_MID_TJA1051_DUAL
  config.profile_major = 2;   // Mid Carrier + TJA1051 target
#else
  config.profile_major = 1;
#endif
  config.profile_minor = 1;
  config.can_queue_size = kCanQueueSize;
#if BOARD_ENABLE_ENCODER_IO
  config.encoder_ppr = 2048;
  config.encoder_frequency_limit_hz = 300000;
#else
  config.encoder_ppr = 0;
  config.encoder_frequency_limit_hz = 0;
#endif
  config.adc_sample_supported = BOARD_ENABLE_VOLTAGE_ADC;
  config.adc_channel_count = BOARD_ENABLE_VOLTAGE_ADC ? kVoltageChannelCount : 0;
  config.adc_resolution_bits = BOARD_ENABLE_VOLTAGE_ADC ? kVoltageAdcBits : 0;
  config.adc_sample_period_ms = static_cast<uint8_t>(BOARD_VOLTAGE_SAMPLE_PERIOD_MS & 0xFF);

  uint8_t lane_flags = 0;
#if BOARD_ENABLE_MCP2515
  lane_flags |= (1u << 0);
  lane_flags |= (BOARD_CAN_IRQ_MODE != 0) ? (1u << 4) : 0;
#endif
#if BOARD_ENABLE_FEEDER_UART
  lane_flags |= (1u << 0);
#endif
#if BOARD_ENABLE_CONTROL_ISLAND
  lane_flags |= (1u << 1);
#endif
#if BOARD_ENABLE_CONTROL_ISLAND
  lane_flags |= (1u << 2);
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
#if BOARD_ENABLE_CONTROL_ISLAND
  limitation_flags |= (1u << 0);
#endif
#if BOARD_ENABLE_CONTROL_ISLAND
  limitation_flags |= (1u << 1);
#endif
#if BOARD_ENABLE_BUILTIN_CAN_TX_TEST || BOARD_ENABLE_MCP2515_TX_TEST
  limitation_flags |= (1u << 2);
#endif
  config.limitation_flags = limitation_flags;

#if BOARD_HW_PROFILE_MID_MCP2515 || BOARD_HW_PROFILE_MID_FEEDER_UART || \
    BOARD_HW_PROFILE_MID_TJA1051_DUAL
  config.include_v2 = true;
  config.include_v3 = true;
#if BOARD_HW_PROFILE_MID_MCP2515 || BOARD_HW_PROFILE_MID_FEEDER_UART
  config.bus_count = BOARD_ENABLE_CONTROL_ISLAND ? 2 : 1;
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
      (1u << static_cast<uint8_t>(RecordType::CanRxSegment)) |
      (1u << static_cast<uint8_t>(RecordType::StreamSession));
#if BOARD_ENABLE_REMOTE_CONTROL
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::RemoteControlState));
#endif
#if BOARD_ENABLE_CONTROL_ISLAND
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::ControlIslandHealth)) |
      (1u << static_cast<uint8_t>(RecordType::CanTxRaw)) |
      (1u << static_cast<uint8_t>(RecordType::ControlAck));
#endif
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::RuntimeDiagnostic));
#endif
#if BOARD_ENABLE_WIFI_UPLINK
#if BOARD_ENABLE_WIFI_DEEP_DIAGNOSTICS
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::TransportDiagnostic));
#endif
#endif
#if BOARD_ENABLE_ENCODER_IO
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::EncEdgeRaw)) |
      (1u << static_cast<uint8_t>(RecordType::EncDerived));
#endif
#if BOARD_ENABLE_HOST_CAN_TX_ANY || BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_BUILTIN_CAN_TX_TEST
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::CanTxRaw)) |
      (1u << static_cast<uint8_t>(RecordType::ControlAck)) |
      (1u << static_cast<uint8_t>(RecordType::ControlTxEvidence));
#endif
#if BOARD_REMOTE_SEMANTIC_CONTROL_ENABLED
  config.supported_uplink_records |=
      (1u << static_cast<uint8_t>(RecordType::CanTxRaw));
#endif
#if BOARD_ENABLE_HOST_DOWNLINK
  config.supported_downlink_records =
      (1u << static_cast<uint8_t>(RecordType::HostControlStateV2)) |
      (1u << static_cast<uint8_t>(RecordType::HostControlNShot)) |
      (1u << static_cast<uint8_t>(RecordType::HostHeartbeat)) |
      (1u << static_cast<uint8_t>(RecordType::HostControlSession)) |
      (1u << static_cast<uint8_t>(RecordType::HostQueryCapability)) |
      (1u << static_cast<uint8_t>(RecordType::HostClearFaultLockout));
#else
  config.supported_downlink_records = 0;
#endif
  config.safety_feature_flags = 0x0000000Fu;
  // Legacy field now advertises Host software retention only. Physical
  // FDCAN capacity is separately versioned in CAPABILITY v7.
  config.host_tx_queue_size = 0;
  config.capability_v3_flags =
      csm::kCapabilityV3FlagCanonicalFanout |
      csm::kCapabilityV3FlagCompactCanRxSegment;
  config.can_rx_segment_schema = csm::kCanRxSegmentSchema;
  config.can_rx_segment_header_len =
      static_cast<uint8_t>(csm::kCanRxSegmentHeaderLen);
  config.can_rx_segment_entry_len =
      static_cast<uint8_t>(csm::kCanRxSegmentEntryLen);
  config.can_rx_segment_max_frames = csm::kCanRxSegmentMaxFrames;
  config.include_v4 = true;
  config.include_v5 = true;
  config.include_v6 = true;
  config.include_v7 = true;
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
  config.control_path = BOARD_REMOTE_SEMANTIC_CONTROL_ENABLED ? 3 :
      (BOARD_ENABLE_HOST_CAN_TX_ANY ? 1 : 0);
  if (!(BOARD_AUTONOMY_RELEASE_PROVIDER_AVAILABLE ||
        BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH)) {
    config.control_path = 0;
  }
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
  config.control_schema = csm::kHostControlSchema;
  config.terminal_evidence_schema = csm::kControlIslandHealthSchema;
  config.threshold_qualification =
      host_command_freshness.timingQualified() &&
              BOARD_CONTROL_ISLAND_HEALTH_TIMEOUT_MS != 0 &&
              BOARD_USB_TRANSIENT_COVERAGE_MS != 0 &&
              BOARD_WIFI_TRANSIENT_COVERAGE_MS != 0
          ? csm::kThresholdQualificationFrozen
          : csm::kThresholdQualificationExploratory;
  config.hardware_tx_slots = csm::board::control_island::kLaneCount;
  config.host_software_retention = 0;
  config.hw_pending_stale_us = 0;
  config.heartbeat_lag_ms = BOARD_HOST_HEARTBEAT_MAX_EXTRA_LAG_MS;
  config.command_age_ms = BOARD_HOST_CAN_TX_MAX_AGE_MS;
  config.future_tolerance_ms = BOARD_HOST_CLOCK_FUTURE_TOLERANCE_MS;
  config.observed_heartbeat_lag_ms =
      host_command_freshness.observedHeartbeatExtraLagMs();
  config.observed_command_age_ms =
      host_command_freshness.observedCommandAgeMs();
  config.observed_future_lead_ms =
      host_command_freshness.observedCommandFutureLeadMs();
  config.admission_reject_total =
      host_can_tx_rejected_total - host_can_tx_transient_rejected_total;
  config.transient_reject_total = host_can_tx_transient_rejected_total;
  config.intentional_cancel_total = 0u;
  config.hardware_failure_total = control_island_health.bus_off_count;
  config.tracking_failure_total = 0u;
  config.tx_complete_total = 0u;
  for (uint8_t lane = 0;
       lane < csm::board::control_island::kLaneCount; ++lane) {
    config.intentional_cancel_total +=
        control_island_health.lanes[lane].cancel_count;
    config.tracking_failure_total +=
        control_island_health.lanes[lane].tracking_fault;
    config.tx_complete_total += control_island_health.lanes[lane].tx_success;
  }
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
      BOARD_MCP2515_BUS_ROLE,
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
#if BOARD_ENABLE_CONTROL_ISLAND
  config.buses[1] = make_capability_bus_descriptor(
      BOARD_BUILTIN_CAN_BUS_ID,
      BOARD_BUILTIN_CAN_BUS_ROLE,
      2,
      3,
      control_island_health_valid ? 1 : 0,
      1,
      control_island_runtime_ready(millis()) ? 1 : 0,
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
#elif BOARD_HW_PROFILE_MID_FEEDER_UART
  const bool feeder_operational =
      feeder_source_operational(mono64_us());
  config.buses[0] = make_capability_bus_descriptor(
      BOARD_FEEDER_CAN_BUS_ID,
      BOARD_FEEDER_CAN_BUS_ROLE,
      5,  // RP2040 feeder over internal UART
      4,  // MCP25625 integrated transceiver/controller
      feeder_operational ? 1 : 0,
      0,
      0,
      0,
      0);
  config.bus_mode[0] = kBusModeNormal;
  config.bus_ack_capability[0] = feeder_operational ? 1 : 0;
  config.bus_error_frame_capability[0] = config.bus_ack_capability[0];
  config.bus_transceiver_reset_safe[0] = 1;
#if BOARD_ENABLE_CONTROL_ISLAND
  config.buses[1] = make_capability_bus_descriptor(
      BOARD_BUILTIN_CAN_BUS_ID,
      BOARD_BUILTIN_CAN_BUS_ROLE,
      2,
      3,
      control_island_health_valid ? 1 : 0,
      1,
      control_island_runtime_ready(millis()) ? 1 : 0,
      0,
      0);
  config.bus_mode[1] = kBusModeNormal;
  config.bus_ack_capability[1] = 1;
  config.bus_error_frame_capability[1] = 1;
  config.bus_transceiver_reset_safe[1] =
      BOARD_PASSIVE_TRANSCEIVER_RESET_SAFE ? 1 : 0;
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

  uint8_t payload[kCapabilityV7PayloadLen];
  const uint16_t payload_len = csm::board::build_capability_payload(config, payload, sizeof(payload));
  if (payload_len > 0) {
    emit_record(RecordType::Capability, payload, payload_len);
  }
}

static void service_capability_advertisement() {
  const uint32_t now_ms = millis();
#if !BOARD_ENABLE_PERIODIC_CAPABILITY
  last_capability_ms = now_ms;
  return;
#endif
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
#if BOARD_ENABLE_FEEDER_UART
  if (bus == BOARD_FEEDER_CAN_BUS_ID) {
    return 0;
  }
#endif
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
  const uint32_t tail0 = can_q_tail[0];
  const uint32_t tail1 = can_q_tail[1];
  const bool bus0_ready = tail0 != can_q_head[0];
  const bool bus1_ready = tail1 != can_q_head[1];
  const uint8_t index = select_can_rx_queue_index(
      bus0_ready,
      bus0_ready ? can_queue_bus0[tail0].capture_seq : 0,
      bus1_ready,
      bus1_ready ? can_queue_bus1[tail1].capture_seq : 0);
  if (index == kNoReadyCanRxQueue) {
    return false;
  }

  volatile uint32_t& tail_ref = can_q_tail[index];
  const uint32_t tail = tail_ref;
  CanRxItem* queue = (index == 0) ? can_queue_bus0 : can_queue_bus1;
  out = queue[tail];
  tail_ref = (tail + 1) & kCanQueueMask;
  note_can_queue_pop(out.bus);
  return true;
}

#if BOARD_ENABLE_FEEDER_UART
static bool accept_feeder_can_frame(void*, const FeederCanFrame& frame) {
  CanRxItem item;
  item.capture_seq = can_capture_seq_next++;
  item.mono_us = frame.estimated_mono_us;
  item.can_id_flags = frame.can_id_flags;
  item.dlc_flags = frame.dlc_flags & 0x0F;
  item.bus = BOARD_FEEDER_CAN_BUS_ID;
  memcpy(item.data, frame.data, sizeof(item.data));
  return can_queue_push(item);
}

static bool emit_feeder_changed_event(uint32_t current, uint32_t* observed,
                                      BoardEventCode code, uint16_t detail) {
  if (observed == nullptr || current == *observed) {
    return true;
  }
  if (!emit_board_event(code, detail, current)) {
    return false;
  }
  *observed = current;
  return true;
}

static void service_feeder_link_events(uint64_t now_mono_us) {
  const FeederWireStats wire = feeder_uart_ingress.wireStats();
  const FeederUartIngressStats ingress = feeder_uart_ingress.ingressStats();

  if (wire.packets_ok > 0 &&
      (!feeder_session_announced ||
       wire.current_boot_id != feeder_last_wire_stats.current_boot_id)) {
    const bool first = !feeder_session_announced;
    if (!emit_board_event(first ? EventFeederLinkStarted
                                : EventFeederSessionChanged,
                          csm::board::feeder::kFeederWireVersion,
                          wire.current_boot_id)) {
      // The session anchor must precede all evidence from the new epoch.
      return;
    }
    feeder_session_announced = true;
    feeder_stale_latched = false;
    feeder_stale_event_pending = false;
    feeder_recovery_event_pending = false;
    feeder_last_wire_stats.current_boot_id = wire.current_boot_id;
    feeder_last_source_status = {};
    feeder_operational_last =
        feeder_source_operational(now_mono_us);
    emit_capability();
    last_capability_ms = millis();
  }

  emit_feeder_changed_event(
      wire.packet_sequence_gaps,
      &feeder_last_wire_stats.packet_sequence_gaps,
      EventFeederSequenceGap, 1);
  emit_feeder_changed_event(
      wire.frame_sequence_gaps,
      &feeder_last_wire_stats.frame_sequence_gaps,
      EventFeederSequenceGap, 2);
  emit_feeder_changed_event(
      wire.packet_duplicates_or_reorders,
      &feeder_last_wire_stats.packet_duplicates_or_reorders,
      EventFeederSequenceGap, 3);
  emit_feeder_changed_event(
      wire.frame_duplicates_or_reorders,
      &feeder_last_wire_stats.frame_duplicates_or_reorders,
      EventFeederSequenceGap, 4);
  emit_feeder_changed_event(
      wire.crc_failures, &feeder_last_wire_stats.crc_failures,
      EventFeederTransportError, 1);
  const uint32_t parser_failures =
      wire.cobs_failures + wire.contract_failures + wire.length_failures +
      wire.encoded_overflow;
  emit_feeder_changed_event(
      parser_failures, &feeder_last_parser_failures,
      EventFeederTransportError, 2);
  emit_feeder_changed_event(
      ingress.dma_overrun_bytes,
      &feeder_last_ingress_stats.dma_overrun_bytes,
      EventFeederTransportError, 3);
  emit_feeder_changed_event(
      ingress.uart_error_events,
      &feeder_last_ingress_stats.uart_error_events,
      EventFeederTransportError, 4);
  emit_feeder_changed_event(
      ingress.dma_transfer_errors,
      &feeder_last_ingress_stats.dma_transfer_errors,
      EventFeederTransportError, 5);
  emit_feeder_changed_event(
      wire.callback_rejects,
      &feeder_last_wire_stats.callback_rejects,
      EventFeederTransportError, 7);
  emit_feeder_changed_event(
      ingress.dma_cursor_faults,
      &feeder_last_ingress_stats.dma_cursor_faults,
      EventFeederTransportError, 8);

  if (feeder_uart_ingress.statusValid()) {
    const FeederStatus source = feeder_uart_ingress.feederStatus();
    emit_feeder_changed_event(
        source.ring_overflow, &feeder_last_source_status.ring_overflow,
        EventFeederSourceFault, 1);
    emit_feeder_changed_event(
        source.mcp_overflow_events,
        &feeder_last_source_status.mcp_overflow_events,
        EventFeederSourceFault, 2);
    emit_feeder_changed_event(
        source.mcp_error_irq_events,
        &feeder_last_source_status.mcp_error_irq_events,
        EventFeederSourceFault, 3);
    emit_feeder_changed_event(
        source.mcp_bus_off_events,
        &feeder_last_source_status.mcp_bus_off_events,
        EventFeederSourceFault, 4);
    emit_feeder_changed_event(
        source.eflg_or, &feeder_last_source_status.eflg_or,
        EventFeederSourceFault, 5);
  }

  if (feeder_session_announced) {
    const bool stale = feeder_uart_ingress.stale(now_mono_us);
    if (stale && !feeder_stale_latched) {
      feeder_stale_event_pending = true;
    } else if (!stale && feeder_stale_latched) {
      feeder_recovery_event_pending = true;
    }
    if (feeder_stale_event_pending && !feeder_stale_latched) {
      const uint32_t next_stale_total = feeder_stale_total + 1U;
      if (emit_board_event(EventFeederLinkStale, 1, next_stale_total)) {
        feeder_stale_event_pending = false;
        feeder_stale_latched = true;
        feeder_stale_total = next_stale_total;
        feeder_recovery_event_pending = !stale;
      }
    }
    if (feeder_recovery_event_pending && feeder_stale_latched) {
      if (emit_board_event(EventFeederLinkStarted, 2,
                           wire.current_boot_id)) {
        feeder_recovery_event_pending = false;
        feeder_stale_latched = false;
        feeder_stale_event_pending = stale;
      }
    }
  }

  const bool feeder_operational =
      feeder_source_operational(now_mono_us);
  if (feeder_operational != feeder_operational_last) {
    feeder_operational_last = feeder_operational;
    emit_capability();
    last_capability_ms = millis();
  }
}

static void service_feeder_uart_to_queue(size_t byte_budget) {
  if (!feeder_uart_ready) {
    return;
  }
  const uint64_t now_mono_us = mono64_us();
  feeder_uart_ingress.service(byte_budget, now_mono_us,
                              accept_feeder_can_frame, nullptr);
  service_feeder_link_events(now_mono_us);
}
#endif

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

  uint8_t payload[kCanRxSegmentHeaderLen +
                  kCanRxSegmentEntryLen * kCanRxSegmentMaxFrames];
  const uint16_t payload_len = encode_can_rx_segment_payload(
      items, count, segment_seq, can_rx_dropped_total,
      can_fifo_overflow_total, payload, sizeof(payload));
  if (payload_len == 0) {
    can_segment_enqueue_fail_total += count;
    note_pending_can_segment_enqueue_fail(count);
    return false;
  }
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

static void encode_fdcan_snapshot(
    uint8_t* payload, uint16_t offset,
    const csm::board::control_island::FdcanRawSnapshot& snapshot) {
  const uint32_t values[10] = {
      snapshot.cccr, snapshot.psr, snapshot.ecr, snapshot.txfqs,
      snapshot.txbrp, snapshot.txbto, snapshot.txbcf, snapshot.ir,
      snapshot.hal_state, snapshot.hal_error};
  for (uint8_t index = 0; index < 10u; ++index) {
    wr_u32_le(&payload[offset + index * 4u], values[index]);
  }
}

static void emit_control_island_health() {
  uint8_t payload[csm::kControlIslandHealthPayloadLen] = {};
  wr_u64_le(&payload[csm::kControlIslandHealthMonoUsOffset], mono64_us());
  payload[csm::kControlIslandHealthSchemaOffset] =
      csm::kControlIslandHealthSchema;
  wr_u16_le(&payload[csm::kControlIslandHealthPayloadLenOffset],
            csm::kControlIslandHealthPayloadLen);
  wr_u32_le(&payload[csm::kControlIslandHealthSchemaIdOffset],
            control_island_health.schema_id);
  wr_u32_le(&payload[csm::kControlIslandHealthWireContractIdOffset],
            control_island_health.wire_contract_id);
  wr_u32_le(&payload[csm::kControlIslandHealthMemoryLayoutIdOffset],
            control_island_health.memory_layout_id);
  wr_u32_le(&payload[csm::kControlIslandHealthM4BootIdOffset],
            control_island_health.m4_boot_id);
  wr_u32_le(&payload[csm::kControlIslandHealthSequenceOffset],
            control_island_health.health_sequence);
  wr_u32_le(&payload[csm::kControlIslandHealthFlagsOffset],
            control_island_health.flags);
  wr_u32_le(&payload[csm::kControlIslandHealthM7PublishSeenOffset],
            control_island_health.m7_publish_sequence_seen);
  wr_u32_le(&payload[csm::kControlIslandHealthM7PublishAgeOffset],
            control_island_health.m7_publish_age_local_ms);
  wr_u32_le(&payload[csm::kControlIslandHealthAuthorityEpochOffset],
            control_island_health.authority_epoch_seen);
  wr_u32_le(&payload[csm::kControlIslandHealthActiveSourceOffset],
            control_island_health.active_source_seen);
  wr_u32_le(&payload[csm::kControlIslandHealthIpcIntegrityMissOffset],
            control_island_health.ipc_integrity_miss);
  wr_u32_le(&payload[csm::kControlIslandHealthM7StaleCountOffset],
            control_island_health.m7_stale_count);
  wr_u32_le(&payload[csm::kControlIslandHealthErrorWarningOffset],
            control_island_health.error_warning_count);
  wr_u32_le(&payload[csm::kControlIslandHealthErrorPassiveOffset],
            control_island_health.error_passive_count);
  wr_u32_le(&payload[csm::kControlIslandHealthBusOffOffset],
            control_island_health.bus_off_count);
  wr_u32_le(&payload[csm::kControlIslandHealthRawFillOffset],
            control_island_health.raw_ring_fill);
  wr_u32_le(&payload[csm::kControlIslandHealthRawHighWaterOffset],
            control_island_health.raw_ring_high_water);
  wr_u32_le(&payload[csm::kControlIslandHealthRawDropOffset],
            control_island_health.raw_ring_drop);
  wr_u32_le(&payload[csm::kControlIslandHealthTransactionIdOffset],
            control_island_health.transaction_id);
  wr_u16_le(&payload[csm::kControlIslandHealthTransactionRequestedOffset],
            control_island_health.transaction_requested);
  wr_u16_le(&payload[csm::kControlIslandHealthTransactionCompletedOffset],
            control_island_health.transaction_completed);
  payload[csm::kControlIslandHealthTransactionStateOffset] =
      control_island_health.transaction_state;
  payload[csm::kControlIslandHealthHardInhibitOffset] =
      control_island_health.hard_inhibit_state;
  payload[csm::kControlIslandHealthFdcanStateOffset] =
      control_island_health.fdcan_state;
  payload[csm::kControlIslandHealthHardInputsOffset] =
      control_island_health.hard_input_bits;
  for (uint8_t lane = 0;
       lane < csm::board::control_island::kLaneCount; ++lane) {
    const uint16_t offset = csm::kControlIslandHealthLanesOffset +
        lane * csm::kControlIslandHealthLaneLen;
    const auto& health = control_island_health.lanes[lane];
    const uint32_t values[8] = {
        health.release_due, health.tx_success, health.deadline_miss,
        health.cancel_count, health.cancel_race_count, health.suppressed,
        health.tracking_fault, health.last_value_generation};
    for (uint8_t index = 0; index < 8u; ++index) {
      wr_u32_le(&payload[offset + index * 4u], values[index]);
    }
  }
  encode_fdcan_snapshot(payload, csm::kControlIslandHealthCurrentFdcanOffset,
                        control_island_health.current);
  encode_fdcan_snapshot(
      payload, csm::kControlIslandHealthFirstFaultFdcanOffset,
      control_island_health.first_fault);
  encode_fdcan_snapshot(payload, csm::kControlIslandHealthLastFaultFdcanOffset,
                        control_island_health.last_fault);
  wr_u32_le(&payload[csm::kControlIslandHealthM7PublishTotalOffset],
            control_snapshot_publish_total);
  wr_u32_le(&payload[csm::kControlIslandHealthM7PublishFailedOffset],
            control_snapshot_publish_failed_total);
  emit_record(RecordType::ControlIslandHealth, payload, sizeof(payload),
              UplinkPriority::Critical);
}

#if BOARD_ENABLE_REMOTE_CONTROL
static void emit_remote_control_state() {
  const auto& status = remote_control_runtime.status();
  const auto& config = remote_control_runtime.config();
  const auto& snapshot = remote_control_runtime.mailboxSnapshot();
  const auto& diag = status.frontend_diagnostics;
  uint8_t payload[csm::kRemoteControlStatePayloadLen];
  memset(payload, 0, sizeof(payload));
  wr_u64_le(&payload[csm::kRemoteControlStateMonoUsOffset], mono64_us());
  payload[csm::kRemoteControlStateSchemaOffset] = csm::kRemoteControlStateSchema;
  payload[csm::kRemoteControlStateLinkStateOffset] = static_cast<uint8_t>(status.link_state);
  payload[csm::kRemoteControlStateAuthorityStateOffset] =
      static_cast<uint8_t>(status.authority_state);
  payload[csm::kRemoteControlStateActiveSourceOffset] =
      static_cast<uint8_t>(status.active_source);
  payload[csm::kRemoteControlStateFlagsOffset] =
      (status.configured ? 0x01u : 0u) |
      (status.frontend_alive ? 0x02u : 0u) |
      (status.remote_reserved ? 0x04u : 0u) |
      (status.remote_valid ? 0x08u : 0u) |
      (status.neutral_now ? 0x10u : 0u) |
      (status.handoff_qualified ? 0x20u : 0u) |
      (status.release_qualified ? 0x40u : 0u) |
      (status.host_control_allowed ? 0x80u : 0u);
  payload[csm::kRemoteControlStateLinkQualityOffset] = status.link_quality;
  payload[csm::kRemoteControlStateRssiOffset] = status.rssi_magnitude;
  payload[csm::kRemoteControlStateLastCrsfTypeOffset] = diag.last_type;
  wr_u32_le(&payload[csm::kRemoteControlStateM4BootIdOffset], status.m4_boot_id);
  wr_u32_le(&payload[csm::kRemoteControlStateSharedSequenceOffset], status.shared_sequence);
  wr_u32_le(&payload[csm::kRemoteControlStateSampleAgeOffset], status.sample_age_ms);
  wr_u16_le(&payload[csm::kRemoteControlStateDriveOffset],
            static_cast<uint16_t>(status.drive_permille));
  wr_u16_le(&payload[csm::kRemoteControlStateSteeringOffset],
            static_cast<uint16_t>(status.steering_permille));
  wr_u16_le(&payload[csm::kRemoteControlStateRawCh2Offset], status.raw_ch2);
  wr_u16_le(&payload[csm::kRemoteControlStateRawCh4Offset], status.raw_ch4);
  wr_u32_le(&payload[csm::kRemoteControlStateUartBaudOffset], diag.uart_baud);
  wr_u32_le(&payload[csm::kRemoteControlStateRxBytesOffset], diag.rx_bytes);
  wr_u32_le(&payload[csm::kRemoteControlStateValidFramesOffset], diag.valid_frames);
  wr_u32_le(&payload[csm::kRemoteControlStateRcFramesOffset], diag.rc_frames);
  wr_u32_le(&payload[csm::kRemoteControlStateLinkFramesOffset], diag.link_frames);
  wr_u32_le(&payload[csm::kRemoteControlStateRejectedLengthOffset], diag.rejected_length);
  wr_u32_le(&payload[csm::kRemoteControlStateRejectedCrcOffset], diag.rejected_crc);
  wr_u32_le(&payload[csm::kRemoteControlStateInterByteResetOffset], diag.inter_byte_resets);
  wr_u32_le(&payload[csm::kRemoteControlStateMailboxPublishOffset], diag.mailbox_publishes);
  wr_u32_le(&payload[csm::kRemoteControlStateTelemetryFramesOffset], diag.telemetry_tx_frames);
  wr_u32_le(&payload[csm::kRemoteControlStateTelemetryBytesOffset], diag.telemetry_tx_bytes);
  wr_u32_le(&payload[csm::kRemoteControlStateSerialWriteFailuresOffset],
            diag.serial_write_failures);
  wr_u32_le(&payload[csm::kRemoteControlStateControlCyclesOffset],
            status.semantic_updates);
  wr_u32_le(&payload[csm::kRemoteControlStateNeutralCyclesOffset], 0u);
  uint32_t deadline_misses = 0u;
  uint32_t tx_success = 0u;
  uint32_t tx_failures = 0u;
  for (uint8_t lane = 0;
       lane < csm::board::control_island::kLaneCount; ++lane) {
    deadline_misses += control_island_health.lanes[lane].deadline_miss;
    tx_success += control_island_health.lanes[lane].tx_success;
    tx_failures += control_island_health.lanes[lane].tracking_fault;
  }
  wr_u32_le(&payload[csm::kRemoteControlStateDeadlineMissesOffset],
            deadline_misses);
  wr_u32_le(&payload[csm::kRemoteControlStateCanTxSuccessOffset], tx_success);
  wr_u32_le(&payload[csm::kRemoteControlStateCanTxFailedOffset], tx_failures);
  wr_u32_le(&payload[csm::kRemoteControlStateIpcRejectsOffset], status.ipc_rejects);
  payload[csm::kRemoteControlStateDecisionOffset] = static_cast<uint8_t>(status.last_decision);
  payload[csm::kRemoteControlStateLastAddressOffset] = diag.last_address;
  payload[csm::kRemoteControlStateSampleStateOffset] =
      static_cast<uint8_t>(snapshot.sample.sample_state);
  payload[csm::kRemoteControlStateLastIpcRejectDetailOffset] =
      status.last_ipc_reject_detail;
  wr_u16_le(&payload[csm::kRemoteControlStateCyclePeriodOffset],
            config.semantic_update_period_ms);
  wr_u16_le(&payload[csm::kRemoteControlStateFrameGapOffset], 0u);
  wr_u16_le(&payload[csm::kRemoteControlStateNeutralQualificationOffset],
            config.neutral_qualification_ms);
  wr_u16_le(&payload[csm::kRemoteControlStateReleaseQualificationOffset],
            config.release_qualification_ms);
  wr_u16_le(&payload[csm::kRemoteControlStateMaxForwardRpmOffset], config.max_forward_rpm);
  wr_u16_le(&payload[csm::kRemoteControlStateMaxReverseRpmOffset], config.max_reverse_rpm);
  wr_u16_le(&payload[csm::kRemoteControlStateMaxSteeringOffset],
            config.max_steering_deci_degree);
  wr_u16_le(&payload[csm::kRemoteControlStatePolicyIdOffset], config.policy_id);
  for (uint8_t index = 0; index < csm::board::remote::kRcChannelCount; ++index) {
    wr_u16_le(&payload[csm::kRemoteControlStateChannelsOffset + index * 2u],
              static_cast<uint16_t>(snapshot.sample.ch[index]));
  }
  payload[csm::kRemoteControlStateLinkStatisticsValidOffset] =
      diag.link_statistics_valid;
  payload[csm::kRemoteControlStateUplinkRssiAnt1Offset] = diag.uplink_rssi_ant1;
  payload[csm::kRemoteControlStateUplinkRssiAnt2Offset] = diag.uplink_rssi_ant2;
  payload[csm::kRemoteControlStateUplinkSnrOffset] =
      static_cast<uint8_t>(diag.uplink_snr);
  payload[csm::kRemoteControlStateActiveAntennaOffset] = diag.active_antenna;
  payload[csm::kRemoteControlStateRfProfileOffset] = diag.rf_profile;
  payload[csm::kRemoteControlStateUplinkRfPowerOffset] = diag.uplink_rf_power;
  payload[csm::kRemoteControlStateDownlinkRssiOffset] = diag.downlink_rssi;
  payload[csm::kRemoteControlStateDownlinkLinkQualityOffset] =
      diag.downlink_link_quality;
  payload[csm::kRemoteControlStateDownlinkSnrOffset] =
      static_cast<uint8_t>(diag.downlink_snr);
  wr_u32_le(&payload[csm::kRemoteControlStateSharedPublishFailuresOffset],
            diag.shared_publish_failures);
  for (uint8_t index = 0; index < csm::board::remote::kRcChannelCount; ++index) {
    wr_u16_le(&payload[csm::kRemoteControlStateRawChannelsOffset + index * 2u],
              diag.raw_channels[index]);
  }
  wr_u32_le(&payload[csm::kRemoteControlStateAcceptedRcFramesOffset],
            diag.accepted_rc_frames);
  wr_u32_le(&payload[csm::kRemoteControlStateNormalizationRejectsOffset],
            diag.normalization_rejects);
  wr_u32_le(&payload[csm::kRemoteControlStateLastRcAgeOffset],
            diag.last_rc_age_ms);
  wr_u32_le(&payload[csm::kRemoteControlStateLastLinkStatisticsAgeOffset],
            diag.last_link_statistics_age_ms);
  wr_u16_le(&payload[csm::kRemoteControlStateLastNormalizeRejectDetailOffset],
            diag.last_normalize_reject_detail);
  emit_record(RecordType::RemoteControlState, payload, sizeof(payload));
}
#endif

static void emit_board_health(const EncoderSnapshot& snap) {
  const uint32_t queued0 = (can_q_head[0] - can_q_tail[0]) & kCanQueueMask;
  const uint32_t queued1 = (can_q_head[1] - can_q_tail[1]) & kCanQueueMask;
  const uint32_t queued = queued0 + queued1;
  const csm::board::uplink::AdmissionCounters& admission_counters =
      canonical_publisher.admissionCounters();
  const csm::board::uplink::PublisherCounters& publisher_counters =
      canonical_publisher.counters();
  const csm::board::uplink::UsbCdcSinkCounters& usb_counters =
      usb_cdc_sink.counters();
#if BOARD_ENABLE_WIFI_UPLINK
  const csm::board::uplink::WifiTcpSinkCounters& wifi_counters = wifi_tcp_sink.counters();
#else
  const csm::board::uplink::WifiTcpSinkCounters wifi_counters = {};
#endif

  uint8_t inputs = 0;
  inputs |= (control_island_health.hard_input_bits &
             csm::board::control_island::kHardInputEstop)
                ? (1u << 0)
                : 0u;
  inputs |= (control_island_health.hard_input_bits &
             csm::board::control_island::kHardInputFieldPowerLost)
                ? 0u
                : (1u << 1);
  inputs |= (control_island_health.hard_input_bits &
             csm::board::control_island::kHardInputEncoderFault)
                ? (1u << 2)
                : 0u;

  uint8_t payload[csm::kBoardHealthV13PayloadLen];
  memset(payload, 0, sizeof(payload));
  wr_u64_le(&payload[0], mono64_us());
  wr_u32_le(&payload[8], can_rx_count_total);
  wr_u32_le(&payload[12], can_rx_dropped_total);
  wr_u32_le(&payload[16], can_fifo_overflow_total);
  wr_u32_le(&payload[20], publisher_counters.record_publish_total);
  wr_u32_le(&payload[24], queued);
  wr_u32_le(&payload[28], encoder_fault_events);
  wr_u32_le(&payload[32], encoder_wrap_events);
  wr_i64_le(&payload[36], snap.position);
  payload[44] = static_cast<uint8_t>(safety_state);
  payload[45] = inputs;
  // A disabled encoder lane has no timer requirement and is therefore healthy.
  payload[46] = (!BOARD_ENABLE_TIM3_ENCODER || encoder_timer_ok) ? 1 : 0;
  payload[47] = 0;
  payload[47] |= kTestMode ? (1u << 0) : 0;
  payload[47] |= can_backend_ok ? (1u << 1) : 0;
#if BOARD_ENABLE_FEEDER_UART
  payload[47] |= feeder_source_operational(mono64_us())
                     ? (1u << 1)
                     : 0;
#endif
#if BOARD_ENABLE_CONTROL_ISLAND
  payload[47] |=
      control_island_runtime_ready(millis()) ? (1u << 2) : 0;
#endif
#if BOARD_ENABLE_MCP2515
  payload[47] |= (BOARD_CAN_IRQ_MODE != 0) ? (1u << 3) : 0;
  payload[47] |= (BOARD_CAN_IRQ_MODE == 2) ? (1u << 4) : 0;
#endif
#if BOARD_ENABLE_VOLTAGE_ADC
  payload[47] |= voltage_adc_ok ? (1u << 5) : 0;
#endif
  uint32_t sink_queued_bytes = usb_cdc_sink.queuedBytes();
#if BOARD_ENABLE_WIFI_UPLINK
  sink_queued_bytes += wifi_tcp_sink.queuedBytes();
#endif
  payload[47] |= (sink_queued_bytes + canonical_publisher.queuedPayloadBytes()) > 0
                     ? (1u << 6)
                     : 0;
  payload[47] |= admission_counters.record_drop_total > 0 ? (1u << 7) : 0;
  wr_u32_le(&payload[48], snap.fault_flags);
  payload[52] = 13;  // BOARD_HEALTH payload version.
  payload[53] = static_cast<uint8_t>(sizeof(payload));
  payload[54] = static_cast<uint8_t>(safety_supervisor.state());
  payload[55] = safety_supervisor.faultBits();
  wr_u32_le(&payload[56], safety_supervisor.heartbeatAgeMs(millis()));
  wr_u32_le(&payload[60], safety_supervisor.leaseRemainingMs(millis()));
  wr_u32_le(&payload[64], host_frame_crc_failed_total);
  wr_u32_le(&payload[68], host_control_state_request_total);
  wr_u32_le(&payload[72], host_can_tx_accepted_total);
  wr_u32_le(&payload[76], host_can_tx_rejected_total);
#if BOARD_ENABLE_MCP2515 && (BOARD_ENABLE_MCP2515_TX_TEST || BOARD_ENABLE_HOST_CAN_TX_MCP2515)
  wr_u32_le(&payload[80], mcp2515_tx_total);
  wr_u32_le(&payload[84], mcp2515_tx_failed_total);
#endif
  uint32_t control_tx_success_total = 0u;
  uint32_t control_tx_failure_total = 0u;
  for (uint8_t lane = 0;
       lane < csm::board::control_island::kLaneCount; ++lane) {
    control_tx_success_total += control_island_health.lanes[lane].tx_success;
    control_tx_failure_total +=
        control_island_health.lanes[lane].tracking_fault +
        control_island_health.lanes[lane].deadline_miss;
  }
  wr_u32_le(&payload[88], control_tx_success_total);
  wr_u32_le(&payload[92], control_tx_failure_total);
#if BOARD_ENABLE_MCP2515
  wr_u32_le(&payload[csm::kBoardHealthMcpSpiErrorOffset], mcp_service.spi_error_total);
  wr_u32_le(&payload[csm::kBoardHealthMcpErrorFlagOffset], mcp_service.error_flag_total);
  payload[csm::kBoardHealthMcpLastCanintfOffset] = mcp_service.last_canintf;
  payload[csm::kBoardHealthMcpLastEflgOffset] = mcp_service.last_eflg;
  payload[csm::kBoardHealthMcpLastCanctrlOffset] = mcp_service.last_canctrl;
  payload[csm::kBoardHealthMcpLastIntLowOffset] = mcp_service.last_int_low ? 1 : 0;
#endif
  wr_u32_le(&payload[108], queued);
  wr_u32_le(&payload[112], safety_supervisor.transitionCounter());
  uint32_t backend_flags = 0;
  backend_flags |= can_backend_ok ? (1u << 0) : 0;
#if BOARD_ENABLE_FEEDER_UART
  backend_flags |=
      feeder_source_operational(mono64_us())
          ? (1u << 0)
          : 0;
#endif
#if BOARD_ENABLE_CONTROL_ISLAND
  backend_flags |=
      control_island_runtime_ready(millis()) ? (1u << 1) : 0;
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
  wr_u32_le(&payload[csm::kBoardHealthSerialEnqueueFailOffset], usb_counters.offer_overflow_total);
  wr_u32_le(&payload[csm::kBoardHealthSerialRingClearOffset], 0);
  wr_u32_le(&payload[csm::kBoardHealthSerialRingClearedBytesOffset], 0);
  wr_u32_le(&payload[csm::kBoardHealthSerialBackpressureOffset], usb_counters.backpressure_total);
  uint32_t uplink_high_water = usb_counters.queue_high_water_bytes;
  if (wifi_counters.queue_high_water_bytes > uplink_high_water) {
    uplink_high_water = wifi_counters.queue_high_water_bytes;
  }
  if (canonical_publisher.queuedPayloadHighWaterBytes() > uplink_high_water) {
    uplink_high_water = canonical_publisher.queuedPayloadHighWaterBytes();
  }
  wr_u32_le(&payload[csm::kBoardHealthUplinkHighWaterOffset], uplink_high_water);
  wr_u32_le(&payload[csm::kBoardHealthCanQueueHighWaterOffset], can_queue_high_water);
#if BOARD_ENABLE_MCP2515
  wr_u32_le(&payload[csm::kBoardHealthMcpDrainBudgetHitOffset],
            mcp_service.drain_time_budget_hit_total);
#endif
  wr_u32_le(&payload[csm::kBoardHealthCanSegmentEnqueueFailOffset],
            can_segment_enqueue_fail_total);
  wr_u32_le(&payload[csm::kBoardHealthPoolLargeUsedOffset], canonical_publisher.poolLargeUsed());
  wr_u32_le(&payload[csm::kBoardHealthPoolLargeCapacityOffset], BOARD_UPLINK_POOL_LARGE_BLOCKS);
  wr_u32_le(&payload[csm::kBoardHealthPoolLargeCanReserveUsedOffset],
            canonical_publisher.poolLargeCanReserveUsed());
  wr_u32_le(&payload[csm::kBoardHealthCanTruthQueueHighWaterOffset],
            admission_counters.can_truth_queue_high_water);
  wr_u32_le(&payload[csm::kBoardHealthPoolAllocFailOffset],
            admission_counters.pool_alloc_fail_total);
  wr_u32_le(&payload[csm::kBoardHealthCanTruthPoolAllocFailOffset],
            admission_counters.can_truth_pool_alloc_fail_total);
  wr_u32_le(&payload[csm::kBoardHealthDescriptorHighWaterOffset],
            admission_counters.descriptor_high_water_total);
  wr_u32_le(&payload[csm::kBoardHealthDiagnosticSuppressedOffset],
            admission_counters.diagnostic_suppressed_total);
  const uint32_t pool_high_bytes =
      admission_counters.pool_large_used_high_water * csm::kMaxPayloadLen +
      admission_counters.pool_medium_used_high_water * 128u +
      admission_counters.pool_small_used_high_water * 64u;
  if (pool_high_bytes > uplink_pool_high_water_bytes) {
    uplink_pool_high_water_bytes = pool_high_bytes;
  }
  wr_u32_le(&payload[csm::kBoardHealthFirmwareProfileOffset], firmware_profile_id());
  wr_u32_le(&payload[csm::kBoardHealthVehicleImpactOffset], vehicle_impact_state());
  wr_u32_le(&payload[csm::kBoardHealthCanRxTaskMaxUsOffset], can_rx_task_max_us);
  wr_u32_le(&payload[csm::kBoardHealthUplinkPoolHighWaterOffset], uplink_pool_high_water_bytes);
  wr_u32_le(&payload[csm::kBoardHealthUplinkDescriptorHighWaterOffset],
            admission_counters.descriptor_high_water_total);
  wr_u32_le(&payload[csm::kBoardHealthUsbReconnectOffset], usb_reconnect_count);
  wr_u32_le(&payload[csm::kBoardHealthUsbForcedResetOffset], usb_forced_reset_count);
  wr_u32_le(&payload[csm::kBoardHealthPassiveViolationOffset], passive_violation_latch);
  wr_u32_le(&payload[csm::kBoardHealthCaptureInvalidReasonOffset], capture_invalid_reason);
  wr_u32_le(&payload[csm::kBoardHealthHostAbsentBus0DiscardOffset],
            host_absent_rx_discard_total[0]);
  wr_u32_le(&payload[csm::kBoardHealthHostAbsentBus1DiscardOffset],
            host_absent_rx_discard_total[1]);
  wr_u32_le(&payload[csm::kBoardHealthHostAbsentFifoOverflowOffset],
            host_absent_fifo_overflow_total);
  wr_u32_le(&payload[csm::kBoardHealthHostAbsentMcpErrorOffset], host_absent_mcp_error_total);
  wr_u32_le(&payload[csm::kBoardHealthHostAbsentDurationMsOffset], host_absent_duration_ms_total);
  wr_u32_le(&payload[csm::kBoardHealthPassiveReadbackOffset], passive_readback_total);
  wr_u32_le(&payload[csm::kBoardHealthPassiveReadbackViolationOffset],
            passive_readback_violation_total);
  wr_u32_le(&payload[csm::kBoardHealthTxreqViolationOffset], txreq_violation_total);
  wr_u32_le(&payload[csm::kBoardHealthUsbDtrChangeOffset], usb_cdc_dtr_change_total);
  wr_u64_le(&payload[csm::kBoardHealthPublishNextOffset], canonical_publisher.nextPublishSeq());
  wr_u64_le(&payload[csm::kBoardHealthBootSessionOffset], canonical_publisher.bootSessionId());
  wr_u32_le(&payload[312], usb_counters.connection_epoch);
  wr_u32_le(&payload[316], usb_counters.queue_high_water_bytes);
  wr_u32_le(&payload[320], usb_counters.offer_overflow_total);
  wr_u32_le(&payload[324], usb_counters.frame_sent_total);
  wr_u32_le(&payload[328], wifi_counters.connection_epoch);
  wr_u32_le(&payload[332], wifi_counters.queue_high_water_bytes);
  wr_u32_le(&payload[336], wifi_counters.offer_overflow_total);
  wr_u32_le(&payload[340], wifi_counters.frame_sent_total);
  wr_u32_le(&payload[344], wifi_counters.connect_total);
  wr_u32_le(&payload[348], wifi_counters.disconnect_total);
  wr_u32_le(&payload[352], wifi_counters.stall_close_total);
  wr_u32_le(&payload[356], publisher_counters.record_no_sink_drop_total);
  wr_u32_le(&payload[csm::kBoardHealthWifiSocketErrorOffset],
            wifi_counters.socket_error_total);
  wr_u32_le(&payload[csm::kBoardHealthWifiSendBudgetOverrunOffset],
            wifi_counters.send_budget_overrun_total);
  wr_u32_le(&payload[csm::kBoardHealthWifiSendCallMaxUsOffset],
            wifi_counters.send_call_max_us);
  wr_u32_le(&payload[csm::kBoardHealthWifiRecvCallMaxUsOffset],
            wifi_counters.recv_call_max_us);
  wr_u32_le(&payload[csm::kBoardHealthWifiCloseCallMaxUsOffset],
            wifi_counters.close_call_max_us);
  wr_u32_le(&payload[csm::kBoardHealthMainLoopMaxGapUsOffset], main_loop_max_gap_us);
  wr_u32_le(&payload[csm::kBoardHealthResetCauseBitsOffset], boot_reset_cause_bits);
  wr_u32_le(&payload[csm::kBoardHealthResetStatusRawOffset], boot_reset_status_raw);
  wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbValidOffset],
            previous_runtime_breadcrumb_valid ? 1u : 0u);
  wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbStageOffset],
            static_cast<uint32_t>(previous_runtime_breadcrumb_stage) |
                (static_cast<uint32_t>(previous_runtime_breadcrumb_detail) << 8));
  wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbUptimeMsOffset],
            previous_runtime_breadcrumb_uptime_ms);
  wr_u32_le(&payload[csm::kBoardHealthPreviousBreadcrumbSequenceOffset],
            previous_runtime_breadcrumb_sequence);

  const csm::board::diagnostics::ProductRecoverySnapshot recovery =
      runtime_supervisor.recoverySnapshot();
  uint32_t recovery_flags = 0;
  recovery_flags |= recovery.ready ? csm::kBoardHealthRecoveryFlagReady : 0u;
  recovery_flags |= recovery.previous_boot_valid
      ? csm::kBoardHealthRecoveryFlagPreviousValid : 0u;
  recovery_flags |= recovery.previous_boot_stable
      ? csm::kBoardHealthRecoveryFlagPreviousStable : 0u;
  recovery_flags |= recovery.current_boot_stable
      ? csm::kBoardHealthRecoveryFlagCurrentStable : 0u;
  recovery_flags |= recovery.wifi_quarantined
      ? csm::kBoardHealthRecoveryFlagWifiQuarantined : 0u;
  recovery_flags |= recovery.wifi_start_allowed
      ? csm::kBoardHealthRecoveryFlagWifiStartAllowed : 0u;
  recovery_flags |= recovery.recovered_from_fallback
      ? csm::kBoardHealthRecoveryFlagFallbackRecovered : 0u;
  recovery_flags |= recovery.firmware_source_changed
      ? csm::kBoardHealthRecoveryFlagSourceChanged : 0u;
  recovery_flags |= recovery.firmware_build_changed
      ? csm::kBoardHealthRecoveryFlagBuildChanged : 0u;
  recovery_flags |= recovery.wifi_retry_active
      ? csm::kBoardHealthRecoveryFlagRetryActive : 0u;
  wr_u32_le(&payload[csm::kBoardHealthRecoveryFlagsOffset], recovery_flags);
  wr_u32_le(&payload[csm::kBoardHealthFirmwareSourceId32Offset],
            static_cast<uint32_t>(CSM_FW_SOURCE_ID32));
  wr_u32_le(&payload[csm::kBoardHealthBootSequenceOffset],
            recovery.boot_sequence);
  wr_u32_le(&payload[csm::kBoardHealthConsecutiveEarlyResetsOffset],
            recovery.consecutive_early_resets);
  wr_u32_le(&payload[csm::kBoardHealthEarlyResetTotalOffset],
            recovery.early_reset_total);
  wr_u32_le(&payload[csm::kBoardHealthWifiQuarantineTotalOffset],
            recovery.wifi_quarantine_total);
  wr_u32_le(&payload[csm::kBoardHealthPreviousBootSequenceOffset],
            recovery.previous_boot_sequence);
  wr_u32_le(&payload[csm::kBoardHealthPreviousLastProgressIdOffset],
            recovery.previous_last_progress_id);
  wr_u32_le(&payload[csm::kBoardHealthPreviousLastProgressDetailOffset],
            recovery.previous_last_progress_detail);
  wr_u32_le(&payload[csm::kBoardHealthPreviousLastProgressUptimeMsOffset],
            recovery.previous_last_progress_uptime_ms);
  wr_u32_le(&payload[csm::kBoardHealthCurrentLastProgressIdOffset],
            recovery.last_progress_id);
  wr_u32_le(&payload[csm::kBoardHealthCurrentLastProgressDetailOffset],
            recovery.last_progress_detail);
  wr_u32_le(&payload[csm::kBoardHealthCurrentLastProgressUptimeMsOffset],
            recovery.last_progress_uptime_ms);
  wr_u32_le(&payload[csm::kBoardHealthRetainedEventSequenceOffset],
            recovery.retained_event_sequence);

  const csm::board::diagnostics::RuntimeSupervisorDecision& runtime_decision =
      runtime_supervisor.decision();
  uint8_t requested_mode = 0;
  uint8_t effective_mode = 0;
#if BOARD_ENABLE_WIFI_UPLINK
  requested_mode = static_cast<uint8_t>(runtime_decision.requested_wifi_mode);
  effective_mode = static_cast<uint8_t>(runtime_decision.effective_wifi_mode);
#endif
  uint8_t experiment_flags = 0;
  experiment_flags |= runtime_watchdog_effective
      ? csm::kBoardHealthResetExperimentFlagWatchdogEffective : 0u;
  experiment_flags |= runtime_watchdog_requested
      ? csm::kBoardHealthResetExperimentFlagWatchdogRequested : 0u;
  experiment_flags |= runtime_watchdog_start_called
      ? csm::kBoardHealthResetExperimentFlagWatchdogStartCalled : 0u;
  experiment_flags |= runtime_watchdog_start_succeeded
      ? csm::kBoardHealthResetExperimentFlagWatchdogStartSucceeded : 0u;
  experiment_flags |= runtime_watchdog_timeout_matches
      ? csm::kBoardHealthResetExperimentFlagWatchdogTimeoutMatches : 0u;
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  experiment_flags |= csm::kBoardHealthResetExperimentFlagRuntimeDiagnostics;
#endif
#if !BOARD_APPLICATION_CAN_DATA_TX_ENABLED
  experiment_flags |=
      csm::kBoardHealthResetExperimentFlagApplicationCanTxSuppressed;
#endif
  const uint32_t profile_word =
      static_cast<uint32_t>(
          csm::board::diagnostics::kResetExperimentProfile.selector) |
      (static_cast<uint32_t>(requested_mode) << 8) |
      (static_cast<uint32_t>(effective_mode) << 16) |
      (static_cast<uint32_t>(experiment_flags) << 24);
  wr_u32_le(&payload[csm::kBoardHealthResetExperimentProfileWordOffset],
            profile_word);
  const uint32_t valid_events = recovery.valid_retained_events > 0xFFFFu
      ? 0xFFFFu : recovery.valid_retained_events;
  const uint32_t corrupt_metadata = recovery.corrupt_metadata_slots > 0xFFu
      ? 0xFFu : recovery.corrupt_metadata_slots;
  const uint32_t corrupt_events = recovery.corrupt_event_slots > 0xFFu
      ? 0xFFu : recovery.corrupt_event_slots;
  const uint32_t integrity_word = valid_events |
      (corrupt_metadata << 16) | (corrupt_events << 24);
  wr_u32_le(&payload[csm::kBoardHealthRetainedIntegrityWordOffset],
            integrity_word);
  wr_u32_le(&payload[csm::kBoardHealthRuntimeContractId32Offset],
            static_cast<uint32_t>(CSM_FW_RUNTIME_CONTRACT_ID32));
  wr_u32_le(&payload[csm::kBoardHealthRecoveryIdentityId32Offset],
            static_cast<uint32_t>(recovery.firmware_source_id));
  wr_u32_le(&payload[csm::kBoardHealthWatchdogObservedTimeoutMsOffset],
            runtime_watchdog_observed_timeout_ms);
#if BOARD_ENABLE_WIFI_UPLINK
  uint32_t previous_wifi_call_flags = previous_wifi_call_latch.valid
      ? csm::kBoardHealthPreviousWifiCallFlagValid : 0u;
  previous_wifi_call_flags |= previous_wifi_call_latch.in_progress
      ? csm::kBoardHealthPreviousWifiCallFlagInProgress : 0u;
  previous_wifi_call_flags |= previous_wifi_call_latch.completed
      ? csm::kBoardHealthPreviousWifiCallFlagCompleted : 0u;
  previous_wifi_call_flags |= previous_wifi_call_latch.contract_changed
      ? csm::kBoardHealthPreviousWifiCallFlagContractChanged : 0u;
  previous_wifi_call_flags |=
      static_cast<uint32_t>(previous_wifi_call_latch.owner & 0xFFu)
      << csm::kBoardHealthPreviousWifiCallOwnerShift;
  previous_wifi_call_flags |=
      static_cast<uint32_t>(previous_wifi_call_latch.operation & 0xFFu)
      << csm::kBoardHealthPreviousWifiCallOperationShift;
  wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallFlagsOffset],
            previous_wifi_call_flags);
  wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallBootSequenceOffset],
            previous_wifi_call_latch.boot_sequence);
  wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallSequenceOffset],
            previous_wifi_call_latch.call_sequence);
  wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallStartedMsOffset],
            previous_wifi_call_latch.started_uptime_ms);
  wr_u32_le(&payload[csm::kBoardHealthPreviousWifiCallDurationUsOffset],
            previous_wifi_call_latch.duration_us);
  wr_i32_le(&payload[csm::kBoardHealthPreviousWifiCallResultOffset],
            previous_wifi_call_latch.result);
#endif
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

static void __attribute__((unused)) enter_passive_can_frontend_fault_hold(
    uint16_t detail, uint32_t counter) {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  can_frontend_session_ready = false;
  can_frontend_session_arm_pending = false;
  ack_observe_enabled = false;

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
    builtin_can_ref().monitor(true);
  }
#endif

  emit_board_event(EventCanFrontendFaultHold, detail, counter);
#else
  (void)detail;
  (void)counter;
#endif
}

static bool __attribute__((unused)) ensure_passive_can_frontend_session_ready(uint32_t now_ms) {
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
  // REV.B assigns hard-safety pins and the external watchdog exclusively to
  // M4. M7 consumes the coherent hard_input_bits projection from M4 health.
}

static void poll_control_island_health(uint32_t now_ms) {
#if BOARD_ENABLE_CONTROL_ISLAND
  const csm::board::control_island::HealthReadResult result =
      csm::board::control_island::readControlHealth(
          control_island_health_sequence);
  if (result.accepted && result.new_snapshot) {
    control_island_health = result.payload;
    control_island_health_sequence = result.sequence;
    control_island_health_seen_ms = now_ms;
    control_island_health_valid = true;
    if (static_cast<uint32_t>(now_ms - last_control_health_emit_ms) >= 100u) {
      emit_control_island_health();
      last_control_health_emit_ms = now_ms;
    }
  }
#else
  (void)now_ms;
#endif
}

static bool control_island_runtime_ready(uint32_t now_ms) {
#if BOARD_ENABLE_CONTROL_ISLAND
  if (!control_island_health_valid ||
      BOARD_CONTROL_ISLAND_HEALTH_TIMEOUT_MS == 0UL ||
      static_cast<uint32_t>(now_ms - control_island_health_seen_ms) >
          BOARD_CONTROL_ISLAND_HEALTH_TIMEOUT_MS) {
    return false;
  }
  const uint32_t flags = control_island_health.flags;
  const uint32_t required =
      csm::board::control_island::kHealthFlagReady |
      csm::board::control_island::kHealthFlagClockContractOk;
  const uint32_t forbidden =
      csm::board::control_island::kHealthFlagHardInhibit |
      csm::board::control_island::kHealthFlagBusOff |
      csm::board::control_island::kHealthFlagErrorPassive |
      csm::board::control_island::kHealthFlagTrackingFault;
  return (flags & required) == required && (flags & forbidden) == 0u;
#else
  (void)now_ms;
  return false;
#endif
}

static bool __attribute__((unused)) control_backend_ready_for_bus(uint8_t bus) {
  return bus == BOARD_BUILTIN_CAN_BUS_ID &&
         control_island_runtime_ready(millis());
}

static bool any_control_backend_ready() {
  return control_island_runtime_ready(millis());
}

static bool __attribute__((unused)) host_control_authority_allowed() {
#if BOARD_ENABLE_REMOTE_AUTHORITY
  return remote_control_runtime_ok &&
      remote_control_runtime.status().host_control_allowed;
#else
  return true;
#endif
}

static void close_host_control_epoch(
    csm::board::control::HostControlCloseReason reason, uint32_t now_ms) {
  host_authority_gate.beginClose(reason, 0u);
  host_command_freshness.reset();
  safety_supervisor.invalidateHostSession(now_ms);
  safety_state = safety_supervisor.state();
  control_source_manager.clearHost();
  host_authority_gate.observeHostSlots(0u);
}

static void service_host_authority_boundary(uint32_t now_ms) {
  if (!host_authority_gate.admissionOpen()) return;
  if (!host_control_authority_allowed()) {
    close_host_control_epoch(
        csm::board::control::HostControlCloseReason::AuthorityPreempted,
        now_ms);
  } else if (!safety_supervisor.leaseAlive(now_ms)) {
    close_host_control_epoch(
        csm::board::control::HostControlCloseReason::LeaseExpired, now_ms);
  }
}

static csm::board::SafetyInputs read_safety_inputs() {
  csm::board::SafetyInputs inputs;
  if (!control_island_health_valid) {
    inputs.estop_asserted = true;
    inputs.field_power_ok = false;
    inputs.encoder_fault = true;
    inputs.control_backend_ready = false;
    return inputs;
  }
  const uint8_t hard = control_island_health.hard_input_bits;
  inputs.estop_asserted =
      (hard & csm::board::control_island::kHardInputEstop) != 0u;
  inputs.field_power_ok =
      (hard & csm::board::control_island::kHardInputFieldPowerLost) == 0u;
  inputs.encoder_fault =
      (hard & csm::board::control_island::kHardInputEncoderFault) != 0u;
  inputs.control_backend_ready = control_island_runtime_ready(millis());
  return inputs;
}

static void update_safety_state() {
  const uint32_t now_ms = millis();
  const csm::board::SafetyInputs inputs = read_safety_inputs();
  const SafetyState before = safety_supervisor.state();
  safety_supervisor.update(now_ms, inputs);
  safety_state = safety_supervisor.state();
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
  if (safety_state == SafetyState::Estop ||
      safety_state == SafetyState::FaultLockout) {
    close_host_control_epoch(
        csm::board::control::HostControlCloseReason::HardSafety, now_ms);
  } else {
    service_host_authority_boundary(now_ms);
  }
}

#if BOARD_ENABLE_REMOTE_CONTROL
static void service_remote_control() {
  if (!remote_control_runtime_ok) return;

  const uint32_t now_ms = millis();
  poll_control_island_health(now_ms);
  service_host_authority_boundary(now_ms);
  const csm::board::SafetyInputs safety_inputs = read_safety_inputs();
  csm::board::control::RemoteControlRuntimeInputs inputs;
  inputs.estop_asserted = safety_inputs.estop_asserted;
  inputs.fault_lockout = safety_state == SafetyState::FaultLockout;
  inputs.hard_safety_allows = !inputs.estop_asserted &&
      safety_inputs.field_power_ok && !safety_inputs.encoder_fault &&
      !inputs.fault_lockout;
  inputs.host_output_reserved = !host_authority_gate.rcAllowed();
#if BOARD_AUTONOMY_RELEASE_PROVIDER_AVAILABLE
  // The provider owns freshness and positive inactive evidence. Until the
  // final vehicle adapter is bound this branch is intentionally unavailable.
  inputs.local_tx_inhibit_latched = false;
  inputs.autonomy_state =
      csm::board::authority::AutonomyAuthorityState::InactiveConfirmed;
#elif BOARD_ALLOW_VIRTUAL_CONTROL_EVIDENCE_BENCH
  // Explicit engineering bench only; never valid as production evidence.
  inputs.local_tx_inhibit_latched = false;
  inputs.autonomy_state =
      csm::board::authority::AutonomyAuthorityState::InactiveConfirmed;
#else
  inputs.local_tx_inhibit_latched = true;
  inputs.autonomy_state = csm::board::authority::AutonomyAuthorityState::Unknown;
#endif
  inputs.backend_state.ready = control_island_runtime_ready(now_ms);
  inputs.backend_state.error_passive =
      (control_island_health.flags &
       csm::board::control_island::kHealthFlagErrorPassive) != 0u;
  inputs.backend_state.bus_off =
      (control_island_health.flags &
       csm::board::control_island::kHealthFlagBusOff) != 0u;

  const csm::board::control::RemoteControlRuntimeOutput output =
      remote_control_runtime.service(now_ms, inputs);
  control_source_manager.updateRemote(
      output.image_generation, output.lease_sequence, output.lanes,
      output.source_valid);

  using csm::board::control_island::ControlSource;
  ControlSource selected = ControlSource::None;
  if (host_authority_gate.admissionOpen() &&
      control_source_manager.host().valid) {
    selected = ControlSource::Host;
  } else if (host_authority_gate.rcAllowed() && output.source_valid) {
    selected = ControlSource::Remote;
  }
  control_source_manager.select(selected);

  uint32_t permit_mask = 0u;
  if (selected != ControlSource::None && control_island_runtime_ready(now_ms)) {
    if (selected == ControlSource::Remote && inputs.hard_safety_allows &&
        !inputs.local_tx_inhibit_latched) {
      permit_mask = csm::board::control_island::kAllLanePermitMask;
    } else if (selected == ControlSource::Host) {
      uint8_t reason = ControlReasonOk;
      if (safety_supervisor.canAcceptTx(now_ms, true, &reason)) {
        permit_mask = csm::board::control_island::kAllLanePermitMask;
      }
    }
  }
  if (static_cast<uint32_t>(now_ms - last_control_snapshot_publish_ms) >= 20u) {
    const csm::board::control_island::FinalControlSnapshotPayload snapshot =
        control_source_manager.snapshot(
            permit_mask, safety_supervisor.transitionCounter());
    if (csm::board::control_island::publishFinalControlSnapshot(snapshot)) {
      ++control_snapshot_publish_total;
    } else {
      ++control_snapshot_publish_failed_total;
    }
    last_control_snapshot_publish_ms = now_ms;
  }

  const auto& status = remote_control_runtime.status();
  const uint16_t state_signature =
      static_cast<uint16_t>(static_cast<uint8_t>(status.link_state)) |
      (static_cast<uint16_t>(static_cast<uint8_t>(status.authority_state)) << 4u) |
      (status.frontend_alive ? (1u << 8) : 0u) |
      (status.remote_valid ? (1u << 9) : 0u) |
      (status.handoff_qualified ? (1u << 10) : 0u) |
      (status.release_qualified ? (1u << 11) : 0u);
  if (state_signature != last_remote_state_signature) {
    last_remote_state_signature = state_signature;
    ++remote_state_transition_total;
    emit_board_event(EventRemoteControlStateChanged, state_signature,
                     remote_state_transition_total);
  }
  if (now_ms - last_remote_state_emit_ms >= 100u) {
    emit_remote_control_state();
    last_remote_state_emit_ms = now_ms;
  }
}
#endif

static void toggle_safety_watchdog_if_needed() {
  // M4 is the sole hard-safety/watchdog owner.
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

static void set_can_observe_mode_for_session(bool enabled, bool force) {
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  const bool target_ack_observe = enabled && (BOARD_PRODUCT_ACK_OBSERVE_MODE != 0);
  if (!force && ack_observe_enabled == target_ack_observe) {
    return;
  }

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

  ack_observe_enabled = target_ack_observe;

  emit_board_event(EventTransceiverSafeStateChanged,
                   ack_observe_enabled ? 1u : 0u,
                   usb_attach_quarantine_total);
  service_mcp2515_passive_readback_guard(true);
#else
  (void)enabled;
#endif
}

static void service_builtin_can_rx_to_queue(int budget) {
#if BOARD_ENABLE_CONTROL_ISLAND
  csm::board::control_island::RawCanEntry raw;
  while (budget-- > 0 &&
         csm::board::control_island::popRawCanForM7(&raw)) {
    CanRxItem item;
    item.capture_seq = can_capture_seq_next++;
    // M4 and M7 timers are separate clock domains. Canonical live truth uses
    // the M7 observation timestamp and never subtracts an M4 timestamp.
    item.mono_us = mono64_us();
    item.can_id_flags = raw.can_id_flags;
    item.dlc_flags = raw.dlc_flags;
    item.bus = raw.bus;
    memcpy(item.data, raw.data, sizeof(item.data));
    can_queue_push(item);
  }
#else
  (void)budget;
#endif
}

static void __attribute__((unused)) service_builtin_can_rx_host_absent_drain(int budget) {
#if BOARD_ENABLE_CONTROL_ISLAND
  csm::board::control_island::RawCanEntry raw;
  while (budget-- > 0 &&
         csm::board::control_island::popRawCanForM7(&raw)) {
    host_absent_rx_discard_total[BOARD_BUILTIN_CAN_BUS_ID & 0x01u]++;
  }
#else
  (void)budget;
#endif
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
static void handle_host_control_state(const uint8_t* payload, uint16_t len) {
  uint32_t command_id = 0;
  ++host_control_state_request_total;
  if (len != csm::kHostControlStateV2PayloadLen) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength,
                     BOARD_BUILTIN_CAN_BUS_ID, 0u, 0u,
                     host_control_state_request_total);
    return;
  }
  command_id = rd_u32_le(&payload[csm::kHostControlStateCommandIdOffset]);
  const uint32_t state_generation =
      rd_u32_le(&payload[csm::kHostControlStateGenerationOffset]);
  const uint32_t contract_id =
      rd_u32_le(&payload[csm::kHostControlStateContractIdOffset]);
  const uint8_t valid_mask =
      payload[csm::kHostControlStateValidMaskOffset];
  const uint32_t now_ms = millis();
  service_host_authority_boundary(now_ms);
  if (contract_id != csm::kHostControlStateContractId ||
      !host_authority_gate.admissionOpen()) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected,
                     contract_id == csm::kHostControlStateContractId
                         ? ControlReasonAuthorityDenied
                         : csm::ControlReasonBadProtocol,
                     BOARD_BUILTIN_CAN_BUS_ID, 0u, 0u,
                     host_control_state_request_total);
    return;
  }
  const bool accepted = control_source_manager.acceptHostState(
      state_generation, valid_mask,
      &payload[csm::kHostControlStateData005Offset],
      &payload[csm::kHostControlStateData007Offset],
      &payload[csm::kHostControlStateData364Offset],
      host_control_lease_sequence);
  if (!accepted) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected,
                     csm::ControlReasonStaleCommand,
                     BOARD_BUILTIN_CAN_BUS_ID, 0u, 0u,
                     host_control_state_request_total);
    return;
  }
  ++host_can_tx_accepted_total;
  emit_control_ack(command_id, ControlAckAccepted, ControlReasonOk,
                   BOARD_BUILTIN_CAN_BUS_ID, 0u, 0u,
                   host_can_tx_accepted_total);
}

static void handle_host_control_nshot(const uint8_t* payload, uint16_t len) {
  uint32_t command_id = 0u;
  if (len != csm::kHostControlNShotPayloadLen) {
    ++host_can_tx_rejected_total;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength,
                     BOARD_BUILTIN_CAN_BUS_ID, 0u, 0u,
                     host_can_tx_rejected_total);
    return;
  }
  command_id = rd_u32_le(&payload[csm::kHostControlNShotCommandIdOffset]);
  const uint32_t contract_id =
      rd_u32_le(&payload[csm::kHostControlNShotContractIdOffset]);
  const uint8_t lane = payload[csm::kHostControlNShotLaneOffset];
  const bool accepted =
      contract_id == csm::kHostControlStateContractId &&
      host_authority_gate.admissionOpen() &&
      control_source_manager.acceptHostNShot(
          rd_u32_le(&payload[csm::kHostControlNShotTransactionIdOffset]),
          lane,
          rd_u16_le(&payload[csm::kHostControlNShotCountOffset]),
          rd_u32_le(&payload[csm::kHostControlNShotPayloadGenerationOffset]),
          &payload[csm::kHostControlNShotDataOffset]);
  if (!accepted) ++host_can_tx_rejected_total;
  emit_control_ack(command_id,
                   accepted ? ControlAckAccepted : ControlAckRejected,
                   accepted ? ControlReasonOk : csm::ControlReasonBadProtocol,
                   BOARD_BUILTIN_CAN_BUS_ID,
                   lane < csm::board::control_island::kLaneCount
                       ? csm::board::control_island::kLaneIds[lane]
                       : 0u,
                   8u, accepted ? host_control_state_request_total
                                : host_can_tx_rejected_total);
}

static void handle_host_heartbeat(uint16_t seq, const uint8_t* payload, uint16_t len) {
  uint32_t command_id = seq;
  if (len >= 4) {
    command_id = rd_u32_le(&payload[csm::kHostHeartbeatCommandIdOffset]);
  }
  if (len != csm::kHostHeartbeatPayloadLen) {
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength, 0xFF, 0, 0,
                     host_heartbeat_total);
    return;
  }

  host_heartbeat_total++;
  const uint32_t now_ms = millis();
  const uint32_t host_mono_ms =
      rd_u32_le(&payload[csm::kHostHeartbeatMonoMsOffset]);
  const csm::board::control::HostFreshnessResult freshness =
      host_command_freshness.acceptHeartbeat(
          command_id, host_mono_ms, now_ms);
  if (freshness == csm::board::control::HostFreshnessResult::Accepted) {
    safety_supervisor.heartbeat(now_ms);
  } else if (freshness !=
             csm::board::control::HostFreshnessResult::AnchorEstablished) {
    close_host_control_epoch(
        csm::board::control::HostControlCloseReason::FreshnessFault,
        now_ms);
    emit_control_ack(command_id, ControlAckRejected,
                     csm::ControlReasonStaleCommand, 0xFF, 0, 0,
                     host_heartbeat_total);
  }
  if ((host_heartbeat_total & 0x3Fu) == 1) {
    emit_board_event(
        EventHostHeartbeat,
        freshness == csm::board::control::HostFreshnessResult::Accepted
            ? 0u
            : static_cast<uint16_t>(freshness),
        host_heartbeat_total);
  }
}

static void handle_host_control_session(uint16_t seq, const uint8_t* payload, uint16_t len) {
  uint32_t command_id = seq;
  uint8_t action = 0xFF;
  uint8_t requested_bus = 0xFF;
  uint16_t lease_ms = 0;
  uint32_t host_mono_ms = 0;

  host_control_session_total++;

  if (len >= 4) {
    command_id =
        rd_u32_le(&payload[csm::kHostControlSessionCommandIdOffset]);
  }
  if (len != csm::kHostControlSessionPayloadLen) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected, ControlReasonBadLength, requested_bus, 0, 0,
                     host_control_session_total);
    emit_board_event(EventHostControlSession, ControlReasonBadLength, host_control_session_total);
    return;
  }

  action = payload[csm::kHostControlSessionActionOffset];
  requested_bus = payload[csm::kHostControlSessionBusOffset];
  lease_ms = rd_u16_le(&payload[csm::kHostControlSessionLeaseMsOffset]);
  host_mono_ms =
      rd_u32_le(&payload[csm::kHostControlSessionMonoMsOffset]);
  const uint8_t control_schema =
      payload[csm::kHostControlSessionSchemaOffset];

  const uint32_t now_ms = millis();
  const csm::board::SafetyInputs inputs = read_safety_inputs();
  safety_supervisor.update(now_ms, inputs);
  safety_state = safety_supervisor.state();

  uint8_t reason = ControlReasonOk;
  uint8_t status = ControlAckAccepted;
  if (action != csm::HostControlDisarm &&
      control_schema != csm::kHostControlSchema) {
    host_can_tx_rejected_total++;
    emit_control_ack(command_id, ControlAckRejected,
                     csm::ControlReasonBadProtocol, requested_bus, 0, 0,
                     host_control_session_total);
    emit_board_event(
        EventHostControlSession,
        (static_cast<uint16_t>(action) << 8) |
            csm::ControlReasonBadProtocol,
        host_control_session_total);
    return;
  }
  if (action != csm::HostControlDisarm) {
    const csm::board::control::HostFreshnessResult freshness =
        host_command_freshness.acceptCommand(
            command_id, host_mono_ms, now_ms);
    if (freshness != csm::board::control::HostFreshnessResult::Accepted) {
      if (freshness ==
          csm::board::control::HostFreshnessResult::FaultLatched) {
        close_host_control_epoch(
            csm::board::control::HostControlCloseReason::FreshnessFault,
            now_ms);
      }
      host_can_tx_rejected_total++;
      emit_control_ack(command_id, ControlAckRejected,
                       csm::ControlReasonStaleCommand, requested_bus, 0, 0,
                       host_control_session_total);
      emit_board_event(
          EventHostControlSession,
          (static_cast<uint16_t>(action) << 8) |
              csm::ControlReasonStaleCommand,
          host_control_session_total);
      return;
    }
  }
  switch (action) {
    case csm::HostControlDisarm:
      close_host_control_epoch(
          csm::board::control::HostControlCloseReason::HostDisarm, now_ms);
      safety_supervisor.disarm(now_ms);
      break;
    case csm::HostControlArm:
      if (!host_control_authority_allowed()) {
        close_host_control_epoch(
            csm::board::control::HostControlCloseReason::AuthorityPreempted,
            now_ms);
        reason = ControlReasonAuthorityDenied;
      } else {
        const bool backend_ready =
            (requested_bus == 0xFF ? any_control_backend_ready() :
             control_backend_ready_for_bus(requested_bus));
        reason = safety_supervisor.arm(now_ms, lease_ms, backend_ready);
        if (reason == ControlReasonOk &&
            !host_authority_gate.activate(
                safety_supervisor.leaseAlive(now_ms),
                host_control_authority_allowed(), 0u)) {
          safety_supervisor.disarm(now_ms);
          reason = ControlReasonTxBusy;
        } else if (reason == ControlReasonOk) {
          ++host_control_lease_sequence;
          if (host_control_lease_sequence == 0u) {
            host_control_lease_sequence = 1u;
          }
          control_source_manager.renewHostLease(host_control_lease_sequence);
        }
      }
      break;
    case csm::HostControlRenewLease:
      if (!host_control_authority_allowed() ||
          !host_authority_gate.admissionOpen()) {
        close_host_control_epoch(
            csm::board::control::HostControlCloseReason::AuthorityPreempted,
            now_ms);
        reason = ControlReasonAuthorityDenied;
      } else {
        reason = safety_supervisor.renewLease(now_ms, lease_ms);
        if (reason == ControlReasonOk) {
          ++host_control_lease_sequence;
          if (host_control_lease_sequence == 0u) {
            host_control_lease_sequence = 1u;
          }
          control_source_manager.renewHostLease(host_control_lease_sequence);
        }
      }
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
  canonical_publisher.requestSessionAnnouncement(
      SessionAnnouncementReason::SinkEpochChanged, kWifiSessionSinkMask);
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
    emit_control_ack(seq, ControlAckRejected, ControlReasonBadProtocol, 0, 0, 0,
                     host_control_state_request_total);
    return;
  }

  if (record_type == static_cast<uint8_t>(RecordType::HostControlStateV2)) {
    handle_host_control_state(payload, len);
  } else if (record_type ==
             static_cast<uint8_t>(RecordType::HostControlNShot)) {
    handle_host_control_nshot(payload, len);
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
                     host_control_state_request_total);
    emit_board_event(EventHostCommandUnsupported, record_type,
                     host_control_state_request_total);
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
#if BOARD_HOST_DOWNLINK_TRANSPORT_WIFI
  static uint32_t last_wifi_epoch = 0;
  const uint32_t wifi_epoch = wifi_tcp_sink.counters().connection_epoch;
  if (wifi_epoch != last_wifi_epoch) {
    host_downlink_parser.reset();
    // A transport epoch is also a control-authority epoch. A disconnected or
    // newly accepted Wi-Fi client must establish a fresh heartbeat and arm;
    // it cannot renew the prior client's lease.
    close_host_control_epoch(
        csm::board::control::HostControlCloseReason::TransportEpochClosed,
        millis());
    last_wifi_epoch = wifi_epoch;
  }
  Stream* stream = wifi_tcp_sink.downlinkStream();
  if (stream != nullptr) host_downlink_parser.service(*stream, budget);
#else
  host_downlink_parser.service(Serial, budget);
#endif
}
#else
static void service_host_downlink(int budget) {
  while (budget-- > 0 && Serial.available() > 0) {
    (void)Serial.read();
  }
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
  const bool product_memory_profile_ok =
      csm::board::memory::configureProductMemoryProfile();
  // Capture what the current Arduino/bootloader layer still exposes before
  // any risky peripheral starts. Exact reset-latch preservation below the
  // bootloader remains a separate hardware/boot-chain gate.
  capture_boot_reset_cause();
  csm::board::diagnostics::RuntimeSupervisorBootInfo boot_start;
  boot_start.firmware.build_id = static_cast<uint64_t>(CSM_FW_BUILD_ID);
  boot_start.firmware.source_id = static_cast<uint64_t>(CSM_FW_SOURCE_ID64);
  boot_start.firmware.runtime_contract_id =
      static_cast<uint64_t>(CSM_FW_RUNTIME_CONTRACT_ID64);
  boot_start.reset_cause_bits = boot_reset_cause_bits;
  boot_start.uptime_ms = millis();
  boot_start.watchdog_compiled = BOARD_ENABLE_RUNTIME_WATCHDOG != 0;
  const csm::board::diagnostics::RuntimeSupervisorDecision runtime_decision =
      runtime_supervisor.begin(boot_start);
  runtime_watchdog_requested = runtime_decision.watchdog_requested;
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_recovery_event_replay_count = static_cast<uint8_t>(
      runtime_supervisor.readRecentEvents(
          runtime_recovery_event_replay,
          csm::board::diagnostics::BootRecovery::kEventSlotCount));
  runtime_recovery_event_replay_next = 0;
#endif
#if BOARD_ENABLE_WIFI_UPLINK
  const csm::board::diagnostics::ProductRecoverySnapshot recovery_boot =
      runtime_supervisor.recoverySnapshot();
  const uint64_t call_latch_contract_id =
      recovery_boot.firmware_source_id != 0
          ? recovery_boot.firmware_source_id
          : static_cast<uint64_t>(CSM_FW_RUNTIME_CONTRACT_ID64);
  if (wifi_call_latch.beginSession(call_latch_contract_id,
                                   recovery_boot.boot_sequence, millis())) {
    previous_wifi_call_latch = wifi_call_latch.previousSnapshot();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
    previous_wifi_call_latch_pending = previous_wifi_call_latch.valid;
#endif
  }
#endif
  if (!runtime_decision.recovery_ready) {
    // Keep legacy/deep diagnostic recovery available even if the product
    // black-box metadata itself was unavailable.
    enable_runtime_retained_storage();
  }
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  recover_runtime_diagnostic();
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootSetupEnter);
#endif
  recover_runtime_breadcrumb();
  record_runtime_breadcrumb(RuntimeStageSetup);
  record_boot_progress(csm::board::diagnostics::BootProgress::SetupEntered);
  uplink_boot_ms = millis();
  record_boot_progress(
      csm::board::diagnostics::BootProgress::ResetEvidenceCaptured,
      boot_reset_cause_bits);
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootResetCaptured);
#endif
  init_safety_pins();
  init_status_led();
  init_runtime_watchdog();
  record_boot_progress(
      csm::board::diagnostics::BootProgress::WatchdogConfigured,
      runtime_watchdog_observed_timeout_ms);
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootSafetyWatchdogReady);
#endif
  Serial.begin(115200);
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootUsbReady);
#endif

  csm::board::uplink::UsbCdcSinkConfig usb_sink_config;
  usb_sink_config.drain_time_budget_us = BOARD_SERIAL_TX_DRAIN_TIME_BUDGET_US;
  usb_sink_config.max_writes_per_pump = BOARD_SERIAL_TX_MAX_WRITES_PER_PUMP;
  usb_sink_config.max_bytes_per_pump = BOARD_SERIAL_TX_MAX_BYTES_PER_PUMP;
  usb_cdc_sink.begin(usb_sink_config);
  record_boot_progress(csm::board::diagnostics::BootProgress::UsbSinkReady);
  const uint64_t boot_session_id =
      (static_cast<uint64_t>(static_cast<uint32_t>(random(0x7FFFFFFF))) << 33) ^
      (static_cast<uint64_t>(static_cast<uint32_t>(random(0x7FFFFFFF))) << 2) ^
      static_cast<uint64_t>(static_cast<uint32_t>(random(4)));
#if BOARD_ENABLE_WIFI_UPLINK
  csm::board::uplink::WifiTcpSinkConfig wifi_sink_config;
  requested_wifi_runtime_mode =
      to_wifi_runtime_mode(runtime_decision.requested_wifi_mode);
  effective_wifi_runtime_mode =
      to_wifi_runtime_mode(runtime_decision.effective_wifi_mode);
  if (!product_memory_profile_ok) {
    effective_wifi_runtime_mode =
        csm::board::uplink::WifiRuntimeMode::Disabled;
  }
  wifi_sink_config.runtime_mode = effective_wifi_runtime_mode;
  wifi_sink_config.ap_ssid = BOARD_WIFI_AP_SSID;
  wifi_sink_config.ap_passphrase = BOARD_WIFI_AP_PASSPHRASE;
  wifi_sink_config.port = BOARD_WIFI_TCP_PORT;
  wifi_sink_config.channel = BOARD_WIFI_AP_CHANNEL;
  wifi_sink_config.boot_session_id = boot_session_id;
  wifi_sink_config.drain_time_budget_us = BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US;
  wifi_sink_config.max_writes_per_pump = BOARD_WIFI_TX_MAX_WRITES_PER_PUMP;
  wifi_sink_config.max_bytes_per_pump = BOARD_WIFI_TX_MAX_BYTES_PER_PUMP;
  wifi_sink_config.startup_attempt_limit = BOARD_WIFI_STARTUP_ATTEMPT_LIMIT;
  wifi_sink_config.startup_retry_ms = BOARD_WIFI_STARTUP_RETRY_MS;
  wifi_sink_config.call_persistence.context = &wifi_call_latch;
  wifi_sink_config.call_persistence.enter = persist_wifi_call_enter;
  wifi_sink_config.call_persistence.leave = persist_wifi_call_leave;
  const uint32_t wifi_mode_detail =
      static_cast<uint32_t>(requested_wifi_runtime_mode) |
      (static_cast<uint32_t>(effective_wifi_runtime_mode) << 8) |
      (runtime_supervisor.wifiQuarantined() ? (1u << 16) : 0u);
#endif
#if BOARD_ENABLE_WIFI_UPLINK
  canonical_publisher.begin(boot_session_id, &usb_cdc_sink, &wifi_tcp_sink);
#else
  canonical_publisher.begin(boot_session_id, &usb_cdc_sink);
#endif
  record_boot_progress(
      csm::board::diagnostics::BootProgress::CanonicalPublisherReady);
  if (!product_memory_profile_ok) {
    emit_board_event(EventDataLinkIntegrityFault, 0x4D50u, 1u);
  }
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootPublisherReady);
#endif
  can_rx_segment_builder.begin(emit_can_rx_segment_callback, nullptr, BOARD_CAN_RX_SEGMENT_FLUSH_US);
#if BOARD_ENABLE_FEEDER_UART
  FeederUartIngressConfig feeder_config;
  feeder_config.baud = BOARD_FEEDER_UART_BAUD;
  feeder_config.stale_timeout_ms = BOARD_FEEDER_UART_STALE_MS;
  feeder_config.service_time_budget_us =
      BOARD_FEEDER_UART_SERVICE_TIME_BUDGET_US;
  feeder_uart_ready = feeder_uart_ingress.begin(feeder_config);
  if (!feeder_uart_ready) {
    emit_board_event(EventFeederTransportError, 6, 1);
  }
#endif

  safety_supervisor.begin(millis());
  host_authority_gate.reset();
  csm::board::control_island::initializeControlIpcForM7(
      static_cast<uint32_t>(boot_session_id));
  control_source_manager.begin(static_cast<uint32_t>(boot_session_id));
  safety_state = safety_supervisor.state();
  csm::board::control::HostCommandFreshnessConfig freshness_config;
  freshness_config.heartbeat_max_extra_lag_ms =
      BOARD_HOST_HEARTBEAT_MAX_EXTRA_LAG_MS;
  freshness_config.command_max_age_ms = BOARD_HOST_CAN_TX_MAX_AGE_MS;
  freshness_config.clock_future_tolerance_ms =
      BOARD_HOST_CLOCK_FUTURE_TOLERANCE_MS;
  host_command_freshness_ok =
      host_command_freshness.begin(freshness_config);
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

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootMcpInitEnter);
#endif
  record_boot_progress(
      csm::board::diagnostics::BootProgress::McpFrontendInitEntered);
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
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootMcpInitReturn);
#endif
  record_boot_progress(
      csm::board::diagnostics::BootProgress::McpFrontendInitReturned,
      can_backend_ok ? 1u : 0u);

#if BOARD_ENABLE_REMOTE_CONTROL
  record_boot_progress(
      csm::board::diagnostics::BootProgress::RemoteRuntimeInitEntered);
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootRemoteRuntimeEnter);
#endif
  csm::board::control::RemoteControlRuntimeConfig remote_config;
  remote_config.configured = true;
  remote_config.semantic_output_enabled =
      BOARD_REMOTE_SEMANTIC_CONTROL_ENABLED != 0;
#if BOARD_ENABLE_PRODUCT_VEHICLE_COMMAND_MAPPING || \
    BOARD_ENABLE_SERVICE_HIL_VEHICLE_COMMAND_MAPPING
  remote_config.mapping =
      csm::board::control::VehicleCommandMapping::Vehicle0x005And0x007;
#elif BOARD_ENABLE_MDPS_BENCH_MAPPING
  remote_config.mapping =
      csm::board::control::VehicleCommandMapping::VehicleMdps0x007Only;
#else
  remote_config.mapping = csm::board::control::VehicleCommandMapping::None;
#endif
  remote_config.bus = BOARD_BUILTIN_CAN_BUS_ID;
  remote_config.policy_id = 0x5243u;
  remote_config.semantic_update_period_ms = 5;
  remote_config.m4_heartbeat_timeout_ms = 100;
  remote_config.neutral_qualification_ms = 500;
  remote_config.release_qualification_ms = 1000;
  remote_config.neutral_deadband_permille = 50;
  remote_config.drive_deadband_permille =
      csm::board::control::kRemoteDriveDeadbandPermille;
  remote_config.steering_deadband_permille = 20;
  remote_config.auxiliary_threshold_permille = 500;
  remote_config.steering_step_permille_per_20ms = 30;
  remote_config.steering_return_step_permille_per_20ms = 50;
  remote_config.max_forward_rpm = 500;
  remote_config.max_reverse_rpm = 500;
  remote_config.max_steering_deci_degree = 450;
  remote_config.drive_channel_index = BOARD_REMOTE_DRIVE_CHANNEL_INDEX;
  remote_config.steering_channel_index = BOARD_REMOTE_STEERING_CHANNEL_INDEX;
  remote_control_runtime_ok = remote_control_runtime.begin(
      millis(), static_cast<uint32_t>(boot_session_id), remote_config);
  record_boot_progress(
      csm::board::diagnostics::BootProgress::RemoteRuntimeInitReturned,
      remote_control_runtime_ok ? 1u : 0u);
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootRemoteRuntimeReturn);
#endif
  if (remote_control_runtime_ok) {
    bootM4();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
    runtime_diagnostic_boot_checkpoint(RuntimeDiagBootM4Issued);
#endif
  } else {
    emit_board_event(EventRemoteControlInitFailed, 1, 1);
  }
#endif

#if BOARD_ENABLE_WIFI_UPLINK
  // Safety, CAN ownership and RC runtime are ready before the lower-priority
  // network worker can enter an opaque WHD call. A Wi-Fi startup failure
  // therefore cannot prevent control readiness.
  record_boot_progress(
      csm::board::diagnostics::BootProgress::WifiStartRequested,
      wifi_mode_detail);
  const bool wifi_sink_started = wifi_tcp_sink.begin(wifi_sink_config);
  record_boot_progress(
      csm::board::diagnostics::BootProgress::WifiStartReturned,
      wifi_mode_detail | (wifi_sink_started ? (1u << 24) : 0u));
#endif
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootWifiReady);
#endif

  emit_capability();
  last_capability_ms = millis();

  last_health_ms = millis();
#if BOARD_ENABLE_WIFI_UPLINK
  last_wifi_transport_diagnostic_ms = last_health_ms;
#endif
  last_encoder_derived_ms = millis();
  last_watchdog_toggle_ms = millis();
  record_runtime_breadcrumb(RuntimeStageIdle);
  record_boot_progress(
      csm::board::diagnostics::BootProgress::SetupCompleted);
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  runtime_diagnostic_boot_checkpoint(RuntimeDiagBootSetupComplete);
#endif
}

void loop() {
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  if (!runtime_diagnostic_first_loop_recorded) {
    runtime_diagnostic_first_loop_recorded = true;
    runtime_diagnostic_boot_checkpoint(RuntimeDiagBootFirstLoop);
  }
#endif
  const uint32_t loop_entry_us = micros();
  if (main_loop_last_entry_us != 0) {
    const uint32_t loop_gap_us = loop_entry_us - main_loop_last_entry_us;
    if (loop_gap_us > main_loop_max_gap_us) main_loop_max_gap_us = loop_gap_us;
  }
  main_loop_last_entry_us = loop_entry_us;
  kick_runtime_watchdog();
  service_usb_cdc_reconnect_watchdog();
  mono64_us();
#if BOARD_ENABLE_REMOTE_CONTROL
  // The first bounded control poll precedes feeder, host, publisher, and
  // Wi-Fi work. Later bounded polls reduce release latency without changing
  // the runtime's absolute timeline or producing catch-up bursts.
  update_safety_state();
  service_remote_control();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  service_runtime_diagnostics();
#endif
#endif
  poll_uplink_connections(millis());
  service_uplink_session_state();
  update_safety_state();
  record_runtime_breadcrumb(RuntimeStageHostDownlink);
  service_host_downlink(BOARD_HOST_DOWNLINK_SERVICE_BYTE_BUDGET);
  record_runtime_breadcrumb(RuntimeStageIdle);
  service_recovered_runtime_breadcrumb();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  service_runtime_diagnostics();
#endif
#if BOARD_ENABLE_FEEDER_UART
  service_feeder_uart_to_queue(BOARD_FEEDER_UART_SERVICE_BYTE_BUDGET);
#endif
#if BOARD_CSM_PROFILE_PASSIVE_PRODUCT
  if (uplink_host_session_open() && !ensure_passive_can_frontend_session_ready(millis())) {
    update_safety_state();
    toggle_safety_watchdog_if_needed();
    service_status_led();
    service_capability_advertisement();
    service_uplink(1024);
    service_boot_recovery();
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
    service_boot_recovery();
    kick_runtime_watchdog();
    return;
  }
#endif
  if (!kTestMode && can_backend_ok) {
    record_runtime_breadcrumb(RuntimeStageMcpEntryDrain);
    pump_can_rx_to_queue(BOARD_MCP2515_LOOP_ENTRY_DRAIN_BUDGET);
    record_runtime_breadcrumb(RuntimeStageIdle);
  }
  service_uplink(1024);
  service_deferred_loss_events();
#if BOARD_ENABLE_REMOTE_CONTROL
  service_remote_control();
#endif
  update_safety_state();
  toggle_safety_watchdog_if_needed();
#if BOARD_ENABLE_REMOTE_CONTROL
  service_remote_control();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  service_runtime_diagnostics();
#endif
#endif

  if (kTestMode) {
    test_push_fake_can_if_needed();
  } else if (can_backend_ok) {
    record_runtime_breadcrumb(RuntimeStageMcpMainDrain, 1);
    pump_can_rx_to_queue(512);
    record_runtime_breadcrumb(RuntimeStageIdle);
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

  record_runtime_breadcrumb(RuntimeStageBuiltinCanDrain);
  service_builtin_can_rx_to_queue(128);
  record_runtime_breadcrumb(RuntimeStageIdle);
#if BOARD_ENABLE_REMOTE_CONTROL
  service_remote_control();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  service_runtime_diagnostics();
#endif
#endif
#if BOARD_ENABLE_MCP2515
  record_runtime_breadcrumb(RuntimeStageMcpMainDrain, 2);
  service_mcp2515_tx_audit();
  record_runtime_breadcrumb(RuntimeStageIdle);
#endif
#if BOARD_ENABLE_MCP2515 && BOARD_ENABLE_MCP2515_TX_TEST
  service_mcp2515_tx_test();
#endif
  record_runtime_breadcrumb(RuntimeStageStatusAndSensors);
  service_status_led();
  service_capability_advertisement();
  service_encoder();
  record_runtime_breadcrumb(RuntimeStageIdle);
  record_runtime_breadcrumb(RuntimeStageCanRecordDrain);
  drain_can_records(BOARD_CAN_SERIAL_DRAIN_BUDGET);
  record_runtime_breadcrumb(RuntimeStageIdle);
  if (!kTestMode && can_backend_ok) {
    record_runtime_breadcrumb(RuntimeStageMcpMainDrain, 3);
    pump_can_rx_to_queue(512);
    record_runtime_breadcrumb(RuntimeStageIdle);
  }
  record_runtime_breadcrumb(RuntimeStageStatusAndSensors, 1);
  service_voltage_adc_lane();
  record_runtime_breadcrumb(RuntimeStageIdle);
#if BOARD_ENABLE_REMOTE_CONTROL
  service_remote_control();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  service_runtime_diagnostics();
#endif
#endif

  const uint32_t now_ms = millis();
  if (!uplink_host_session_open()) {
    last_health_ms = now_ms;
#if BOARD_ENABLE_WIFI_UPLINK
    last_wifi_transport_diagnostic_ms = now_ms;
#endif
  } else if (now_ms - last_health_ms >= 1000) {
    record_runtime_breadcrumb(RuntimeStageHealthPublish);
    const EncoderSnapshot snap = poll_encoder();
    emit_board_health(snap);
    last_health_ms = now_ms;
    record_runtime_breadcrumb(RuntimeStageIdle);
  }
#if BOARD_ENABLE_WIFI_UPLINK
  if (uplink_host_session_open() &&
      now_ms - last_wifi_transport_diagnostic_ms >=
          BOARD_WIFI_TRANSPORT_DIAGNOSTIC_PERIOD_MS) {
    emit_wifi_transport_diagnostic(now_ms);
    last_wifi_transport_diagnostic_ms = now_ms;
  }
#endif
  service_uplink(1024);
  service_deferred_loss_events();
#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
  service_runtime_diagnostics();
#endif
  service_usb_cdc_reconnect_watchdog();
  service_boot_recovery();
  kick_runtime_watchdog();
  record_runtime_breadcrumb(RuntimeStageIdle);
#if BOARD_ENABLE_WIFI_UPLINK
  // Yield without imposing a fixed control-loop delay. The lower-priority
  // socket worker remains independently bounded by its own pump contract.
  rtos::ThisThread::yield();
#endif
}
