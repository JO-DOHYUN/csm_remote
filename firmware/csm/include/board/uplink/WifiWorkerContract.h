#pragma once

#include <stdint.h>

#include "board/uplink/ProductUplinkEnvelope.h"

#ifndef BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
#define BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY 0
#endif

#ifndef BOARD_WIFI_STALL_TIMEOUT_MS
#define BOARD_WIFI_STALL_TIMEOUT_MS 5000
#endif

#ifndef BOARD_WIFI_CALL_STALL_TIMEOUT_MS
#define BOARD_WIFI_CALL_STALL_TIMEOUT_MS 5000
#endif

#ifndef BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS
#define BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS 20
#endif

#ifndef BOARD_WIFI_TX_LATENCY_BOUND_MAX_MS
#define BOARD_WIFI_TX_LATENCY_BOUND_MAX_MS 2
#endif

#ifndef BOARD_WIFI_TX_BATCH_TARGET_BYTES
#define BOARD_WIFI_TX_BATCH_TARGET_BYTES 1460
#endif

#ifndef BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US
#define BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US 2000
#endif

#ifndef BOARD_WIFI_TX_MAX_WRITES_PER_PUMP
#define BOARD_WIFI_TX_MAX_WRITES_PER_PUMP 4
#endif

#ifndef BOARD_WIFI_TX_MAX_BYTES_PER_PUMP
#define BOARD_WIFI_TX_MAX_BYTES_PER_PUMP 11680
#endif

#ifndef BOARD_WIFI_TX_CHUNK_BYTES
#define BOARD_WIFI_TX_CHUNK_BYTES 2920
#endif

#ifndef BOARD_WIFI_CONNECTED_FALLBACK_MS
#define BOARD_WIFI_CONNECTED_FALLBACK_MS 5
#endif

#ifndef BOARD_WIFI_STATE_PUBLISH_PERIOD_MS
#define BOARD_WIFI_STATE_PUBLISH_PERIOD_MS 100
#endif

#ifndef BOARD_WIFI_ACCEPT_POLL_MS
#define BOARD_WIFI_ACCEPT_POLL_MS 25
#endif

#ifndef BOARD_WIFI_AP_STA_CONCUR
#define BOARD_WIFI_AP_STA_CONCUR 1
#endif

#ifndef BOARD_WIFI_STARTUP_ATTEMPT_LIMIT
// Zero means continuous bounded retries. Diagnostic profiles may override this
// with a finite attempt count.
#define BOARD_WIFI_STARTUP_ATTEMPT_LIMIT 0
#endif

#ifndef BOARD_WIFI_STARTUP_RETRY_MS
#define BOARD_WIFI_STARTUP_RETRY_MS 2000
#endif

static_assert(BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US > 0,
              "Wi-Fi worker drain time budget must be non-zero");
static_assert(BOARD_WIFI_TX_MAX_WRITES_PER_PUMP > 0,
              "Wi-Fi worker write budget must be non-zero");
static_assert(BOARD_WIFI_TX_MAX_BYTES_PER_PUMP > 0,
              "Wi-Fi worker byte budget must be non-zero");
static_assert(BOARD_WIFI_TX_MAX_WRITES_PER_PUMP * BOARD_WIFI_TX_CHUNK_BYTES >=
                  BOARD_WIFI_TX_MAX_BYTES_PER_PUMP,
              "Wi-Fi worker write count cannot cover its byte budget");
static_assert(BOARD_WIFI_CONNECTED_FALLBACK_MS > 0,
              "Wi-Fi connected fallback must be non-zero");
static_assert(BOARD_WIFI_STATE_PUBLISH_PERIOD_MS > 0,
              "Wi-Fi state publication period must be non-zero");
static_assert(BOARD_WIFI_AP_STA_CONCUR == 0 ||
                  BOARD_WIFI_AP_STA_CONCUR == 1,
              "Wi-Fi WHD compatibility mode must be boolean");
static_assert(BOARD_WIFI_STARTUP_RETRY_MS > 0,
              "Wi-Fi startup retry interval must be non-zero");

#ifndef BOARD_WIFI_TRANSIENT_COVERAGE_MS
#define BOARD_WIFI_TRANSIENT_COVERAGE_MS 0
#endif

#ifndef BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES
#define BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES 32768
#endif

#ifndef BOARD_WIFI_PRESSURE_LOW_WATER_BYTES
#define BOARD_WIFI_PRESSURE_LOW_WATER_BYTES 8192
#endif

#ifndef BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS
#define BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS 192
#endif

#ifndef BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS
#define BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS 64
#endif

static_assert(BOARD_WIFI_TRANSIENT_COVERAGE_MS < 0x80000000u,
              "Wi-Fi transient coverage must remain wrap-safe");
static_assert(BOARD_WIFI_PRESSURE_LOW_WATER_BYTES <
                  BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
              "Wi-Fi byte pressure hysteresis is invalid");
static_assert(BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS <
                  BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS,
              "Wi-Fi record pressure hysteresis is invalid");

static constexpr uint32_t kWifiEnabledIngressBytesPerFallback =
    (static_cast<uint64_t>(
         csm::board::uplink::kProductEnabledWireBytesPerSecond) *
         BOARD_WIFI_CONNECTED_FALLBACK_MS +
     999u) /
    1000u;
static_assert(BOARD_WIFI_TX_MAX_BYTES_PER_PUMP >=
                  kWifiEnabledIngressBytesPerFallback,
              "Wi-Fi worker pump cannot service the enabled wire envelope");
static_assert(BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US <
                  BOARD_WIFI_CONNECTED_FALLBACK_MS * 1000u,
              "Wi-Fi worker pump may monopolize its fallback interval");

namespace csm::board::uplink {

enum WifiWorkerWakeBits : uint32_t {
  WifiWakeNone = 0,
  WifiWakeTxData = 1u << 0,
  WifiWakeSocketState = 1u << 1,
  WifiWakeControl = 1u << 2,
  WifiWakeStartup = 1u << 3,
};

// Framework-free producer-to-worker notification. The callback may only
// coalesce wake bits; it must not call sockets, allocate, log, or block.
struct WifiWorkerNotifier {
  void* context = nullptr;
  void (*notify)(void* context, uint32_t bits) = nullptr;
};

enum class WifiRuntimeMode : uint8_t {
  Disabled = 0,
  AccessPointOnly = 1,
  FullTcp = 2,
};

constexpr bool wifiRuntimeModeStartsWorker(WifiRuntimeMode mode) {
  return mode != WifiRuntimeMode::Disabled;
}

constexpr bool wifiRuntimeModeEnablesTcp(WifiRuntimeMode mode) {
  return mode == WifiRuntimeMode::FullTcp;
}

constexpr bool wifiStartupAttemptsExhausted(uint32_t attempts,
                                            uint8_t attempt_limit) {
  return attempt_limit != 0 && attempts >= attempt_limit;
}

enum class WifiStartupFailureBoundary : uint8_t {
  BeforeApStart = 0,
  OpaqueApStart = 1,
  AfterApStarted = 2,
};

// WhdSoftAPInterface::start() does not expose which internal resources were
// acquired before an error and its stop() is not safe at every partial stage.
// Retry is therefore permitted only before that opaque call, or after a
// successful start when reverse-order cleanup was positively confirmed.
constexpr bool wifiStartupRetryAllowed(
    WifiStartupFailureBoundary boundary, bool cleanup_confirmed) {
  return boundary == WifiStartupFailureBoundary::BeforeApStart ||
      (boundary == WifiStartupFailureBoundary::AfterApStarted &&
       cleanup_confirmed);
}

// A facade may sample now_ms just before the worker publishes a newer
// timestamp. Treat that small future observation as age zero; unsigned
// subtraction would otherwise look like a multi-week stall. Signed modular
// comparison remains wrap-safe for the bounded intervals used here.
constexpr uint32_t wifiObservedAgeMs(uint32_t now_ms, uint32_t observed_ms) {
  const int32_t delta = static_cast<int32_t>(now_ms - observed_ms);
  return delta < 0 ? 0u : static_cast<uint32_t>(delta);
}

// Optional synchronous crash boundary owned by the Wi-Fi worker. The enter
// callback runs after the mailbox phase is published and before the vendor API
// is invoked; leave runs immediately after the API returns. Implementations
// must be bounded, non-blocking, and safe from the worker thread.
struct WifiWorkerCallPersistence {
  void* context = nullptr;
  void (*enter)(void* context, uint8_t operation, uint32_t sequence,
                uint32_t started_ms) = nullptr;
  void (*leave)(void* context, int32_t result, uint32_t duration_us,
                uint32_t completed_ms) = nullptr;
};

struct WifiTcpSinkConfig {
  WifiRuntimeMode runtime_mode = WifiRuntimeMode::FullTcp;
  const char* ap_ssid = nullptr;
  const char* ap_passphrase = nullptr;
  uint16_t port = 3333;
  uint16_t control_port = 3334;
  uint8_t channel = 6;
  uint8_t ip[4] = {192, 168, 4, 1};
  uint64_t boot_session_id = 0;
  bool ap_sta_concur = BOARD_WIFI_AP_STA_CONCUR != 0;
  uint32_t drain_time_budget_us = BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US;
  uint32_t max_writes_per_pump = 0;
  uint32_t max_bytes_per_pump = 0;
  uint32_t stall_timeout_ms = BOARD_WIFI_STALL_TIMEOUT_MS;
  uint32_t call_stall_timeout_ms = BOARD_WIFI_CALL_STALL_TIMEOUT_MS;
  uint32_t batch_max_latency_ms = BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS;
  uint32_t latency_bound_max_ms = BOARD_WIFI_TX_LATENCY_BOUND_MAX_MS;
  uint16_t batch_target_bytes = BOARD_WIFI_TX_BATCH_TARGET_BYTES;
  uint32_t pressure_high_water_bytes =
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES;
  uint32_t pressure_low_water_bytes =
      BOARD_WIFI_PRESSURE_LOW_WATER_BYTES;
  uint16_t pressure_high_water_records =
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS;
  uint16_t pressure_low_water_records =
      BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS;
  // Zero continuously retries at a bounded interval. Diagnostic profiles may
  // select a finite non-zero attempt count.
  uint8_t startup_attempt_limit = BOARD_WIFI_STARTUP_ATTEMPT_LIMIT;
  uint32_t startup_retry_ms = BOARD_WIFI_STARTUP_RETRY_MS;
  WifiWorkerCallPersistence call_persistence{};
};

enum class WifiWorkerCallPhase : uint8_t {
  Idle = 0,
  ConfigureIp = 1,
  BeginAccessPoint = 2,
  BeginServer = 3,
  AcceptClient = 4,
  ConfigureClient = 5,
  Send = 6,
  Receive = 7,
  CloseClient = 8,
  // Reserved for retained evidence produced before the accepted-socket
  // close-only ownership fix. Do not reuse or renumber.
  DeleteClient = 9,
  AcceptExtraClient = 10,
  CloseExtraClient = 11,
  // Reserved with DeleteClient for historical retained evidence decoding.
  DeleteExtraClient = 12,
  StopServer = 13,
  StopAccessPoint = 14,
  BeginControlServer = 15,
  AcceptControlClient = 16,
  ConfigureControlClient = 17,
  SendControl = 18,
  ReceiveControl = 19,
  CloseControlClient = 20,
  StopControlServer = 21,
  OpenTelemetryServer = 22,
  ConfigureTelemetryServer = 23,
  BindTelemetryServer = 24,
  ListenTelemetryServer = 25,
  OpenControlServer = 26,
  ConfigureControlServer = 27,
  BindControlServer = 28,
  ListenControlServer = 29,
};

enum class WifiCloseReason : uint8_t {
  None = 0,
  PeerClosed = 1,
  SocketError = 2,
  ReceiveOverflow = 3,
  TransmitNoProgress = 4,
  IsolationRequest = 5,
  QueuePressure = 6,
};

struct WifiTxProgressObservation {
  bool started = false;
  bool recovered = false;
  bool close_no_progress = false;
  uint32_t duration_ms = 0;
};

// A pump is bounded by both socket call count and successfully transferred
// bytes. Zero keeps the legacy one-call/one-chunk default bounded; production
// profiles provide explicit limits.
class WifiTxPumpBudget {
 public:
  constexpr WifiTxPumpBudget(uint32_t configured_max_writes,
                             uint32_t configured_max_bytes,
                             uint16_t write_chunk_bytes)
      : max_writes_(configured_max_writes == 0 ? 1 : configured_max_writes),
        max_bytes_(configured_max_bytes == 0 ? write_chunk_bytes
                                             : configured_max_bytes),
        write_chunk_bytes_(write_chunk_bytes) {}

  constexpr bool canAttempt() const {
    return write_chunk_bytes_ > 0 && writes_attempted_ < max_writes_ &&
           bytes_progressed_ < max_bytes_;
  }

  constexpr uint16_t nextWriteCapacity() const {
    if (!canAttempt()) return 0;
    const uint32_t remaining = max_bytes_ - bytes_progressed_;
    return static_cast<uint16_t>(
        remaining < write_chunk_bytes_ ? remaining : write_chunk_bytes_);
  }

  void noteAttempt(uint32_t progressed_bytes) {
    const uint32_t permitted = nextWriteCapacity();
    if (progressed_bytes > permitted) progressed_bytes = permitted;
    ++writes_attempted_;
    bytes_progressed_ += progressed_bytes;
  }

  constexpr uint32_t writesAttempted() const { return writes_attempted_; }
  constexpr uint32_t bytesProgressed() const { return bytes_progressed_; }

 private:
  uint32_t max_writes_ = 0;
  uint32_t max_bytes_ = 0;
  uint16_t write_chunk_bytes_ = 0;
  uint32_t writes_attempted_ = 0;
  uint32_t bytes_progressed_ = 0;
};

struct WifiTransmitPumpResult {
  uint32_t writes_attempted = 0;
  uint32_t bytes_progressed = 0;
  uint32_t no_progress_duration_ms = 0;
  bool would_block = false;
  bool zero_write = false;

  constexpr bool attempted() const { return writes_attempted != 0; }
  constexpr bool progressed() const { return bytes_progressed != 0; }
  constexpr bool blockedWithoutProgress() const {
    return attempted() && !progressed() && (would_block || zero_write);
  }
};

constexpr bool wifiSessionAnchorCompleted(uint16_t prior_offset,
                                          uint16_t progressed,
                                          uint16_t anchor_length) {
  return anchor_length != 0 && prior_offset < anchor_length &&
         static_cast<uint32_t>(prior_offset) + progressed >= anchor_length;
}

constexpr bool wifiQueuePressureReached(uint32_t queued_bytes,
                                        uint32_t queued_records,
                                        uint32_t high_water_bytes,
                                        uint32_t high_water_records) {
  return (high_water_bytes > 0 && queued_bytes >= high_water_bytes) ||
         (high_water_records > 0 && queued_records >= high_water_records);
}

constexpr bool wifiQueuePressureRecovered(uint32_t queued_bytes,
                                          uint32_t queued_records,
                                          uint32_t low_water_bytes,
                                          uint32_t low_water_records) {
  return queued_bytes <= low_water_bytes &&
         queued_records <= low_water_records;
}

struct WifiQueuePressureObservation {
  bool active = false;
  bool entered = false;
  bool recovered = false;
  uint32_t duration_ms = 0;
};

// High-water is an observable/recoverable state, never a TCP close reason.
// Actual admission loss and the independent no-positive-progress timeout own
// epoch termination.
class WifiQueuePressureTracker {
 public:
  WifiQueuePressureObservation observe(
      uint32_t now_ms, uint32_t queued_bytes, uint32_t queued_records,
      uint32_t high_water_bytes, uint32_t high_water_records,
      uint32_t low_water_bytes, uint32_t low_water_records) {
    WifiQueuePressureObservation result;
    if (!active_) {
      if (wifiQueuePressureReached(queued_bytes, queued_records,
                                   high_water_bytes, high_water_records)) {
        active_ = true;
        started_ms_ = now_ms;
        result.entered = true;
      }
    } else if (wifiQueuePressureRecovered(
                   queued_bytes, queued_records, low_water_bytes,
                   low_water_records)) {
      result.recovered = true;
      result.duration_ms = now_ms - started_ms_;
      active_ = false;
    }
    result.active = active_;
    if (active_) result.duration_ms = now_ms - started_ms_;
    return result;
  }

  void reset() {
    active_ = false;
    started_ms_ = 0;
  }

 private:
  bool active_ = false;
  uint32_t started_ms_ = 0;
};

class WifiTxProgressTracker {
 public:
  WifiTxProgressObservation observe(uint32_t now_ms, bool progressed,
                                    uint32_t timeout_ms) {
    WifiTxProgressObservation result;
    if (progressed) {
      if (active_) {
        result.recovered = true;
        result.duration_ms = now_ms - started_ms_;
      }
      reset();
      return result;
    }
    if (!active_) {
      active_ = true;
      started_ms_ = now_ms;
      result.started = true;
    }
    result.duration_ms = now_ms - started_ms_;
    result.close_no_progress =
        timeout_ms > 0 && result.duration_ms >= timeout_ms;
    return result;
  }

  void reset() {
    active_ = false;
    started_ms_ = 0;
  }

  bool active() const { return active_; }

 private:
  bool active_ = false;
  uint32_t started_ms_ = 0;
};

struct WifiWorkerCallSnapshot {
  WifiWorkerCallPhase phase = WifiWorkerCallPhase::Idle;
  bool coherent = false;
  bool in_progress = false;
  uint32_t sequence = 0;
  uint32_t started_ms = 0;
  uint32_t duration_us = 0;
  int32_t result = 0;
  uint32_t heartbeat_ms = 0;
};

#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
// SERVICE_HIL_OBSERVABILITY: a normal nonblocking accept poll must not erase
// the last actionable vendor/API failure before the 1 Hz record is sampled.
class WifiWorkerFailureLatch {
 public:
  bool observe(WifiWorkerCallPhase phase, int32_t result,
               int32_t would_block_result) {
    if (result == 0 || result == would_block_result) return false;
    phase_ = phase;
    result_ = result;
    return true;
  }

  WifiWorkerCallPhase phase() const { return phase_; }
  int32_t result() const { return result_; }

 private:
  WifiWorkerCallPhase phase_ = WifiWorkerCallPhase::Idle;
  int32_t result_ = 0;
};
#endif

constexpr bool wifiSendResultHasPositiveProgress(int32_t result) {
  return result > 0;
}

enum class WifiPostSendIsolation : uint8_t {
  None = 0,
  QueuePressure,
  IsolationRequest,
};

constexpr WifiPostSendIsolation wifiPostSendIsolationDecision(
    uint32_t queue_pressure_sequence, uint32_t handled_queue_pressure_sequence,
    uint32_t disconnect_sequence, uint32_t handled_disconnect_sequence) {
  if (queue_pressure_sequence != handled_queue_pressure_sequence) {
    return WifiPostSendIsolation::QueuePressure;
  }
  if (disconnect_sequence != handled_disconnect_sequence) {
    return WifiPostSendIsolation::IsolationRequest;
  }
  return WifiPostSendIsolation::None;
}

constexpr bool wifiSocketSendPermitted(
    bool queue_pressure_latched, uint32_t queue_pressure_sequence,
    uint32_t handled_queue_pressure_sequence, uint32_t disconnect_sequence,
    uint32_t handled_disconnect_sequence) {
  return !queue_pressure_latched &&
         wifiPostSendIsolationDecision(
             queue_pressure_sequence, handled_queue_pressure_sequence,
             disconnect_sequence, handled_disconnect_sequence) ==
             WifiPostSendIsolation::None;
}

constexpr uint32_t wifiPendingAcceptedBytes(uint64_t accepted_bytes,
                                            uint64_t socket_sent_bytes,
                                            uint64_t aborted_bytes) {
  return static_cast<uint32_t>(
      accepted_bytes - socket_sent_bytes - aborted_bytes);
}

struct WifiWorkerCounters {
  uint32_t worker_start_total = 0;
  uint32_t worker_start_fail_total = 0;
  uint32_t startup_attempt_total = 0;
  uint32_t startup_exhausted_total = 0;
  uint32_t ap_start_total = 0;
  uint32_t ap_start_fail_total = 0;
  uint32_t server_start_total = 0;
  uint32_t server_start_fail_total = 0;
  uint32_t bytes_sent_total = 0;
  uint64_t socket_sent_bytes_total = 0;
  uint32_t frame_sent_total = 0;
  uint32_t write_attempt_total = 0;
  uint32_t send_request_bytes_total = 0;
  uint32_t partial_write_total = 0;
  uint32_t zero_write_total = 0;
  uint32_t would_block_total = 0;
  uint32_t backpressure_total = 0;
  uint32_t backpressure_max_duration_ms = 0;
  uint32_t pressure_enter_total = 0;
  uint32_t pressure_recover_total = 0;
  uint32_t pressure_max_duration_ms = 0;
  uint32_t queue_abort_total = 0;
  uint64_t queue_aborted_bytes_total = 0;
  uint32_t queue_aborted_records_total = 0;
  uint64_t first_queue_aborted_publish_seq = 0;
  uint64_t last_queue_aborted_publish_seq = 0;
  bool queue_abort_sequence_valid = false;
  uint32_t connection_epoch = 0;
  uint32_t connect_total = 0;
  uint32_t disconnect_total = 0;
  uint32_t extra_client_reject_total = 0;
  uint32_t stall_close_total = 0;
  uint32_t queue_pressure_close_total = 0;
  uint32_t socket_error_total = 0;
  uint32_t send_budget_overrun_total = 0;
  uint32_t send_call_max_us = 0;
  uint32_t recv_call_max_us = 0;
  uint32_t close_call_max_us = 0;
  uint32_t rx_overflow_total = 0;
  uint32_t late_send_result_total = 0;
  uint32_t worker_returned_slow_call_total = 0;
  uint32_t wake_total = 0;
  uint32_t wake_tx_data_total = 0;
  uint32_t wake_socket_state_total = 0;
  uint32_t wake_control_total = 0;
  uint32_t wake_startup_total = 0;
  uint32_t wake_fallback_total = 0;
  uint32_t sigio_total = 0;
  uint32_t positive_write_total = 0;
  uint32_t bytes_per_wake_max = 0;
  uint32_t writes_per_wake_max = 0;
  uint64_t last_sent_publish_seq = 0;
  uint32_t fresh_anchor_sent_total = 0;
};

struct WifiWorkerStateSnapshot {
  WifiRuntimeMode runtime_mode = WifiRuntimeMode::Disabled;
  bool worker_started = false;
  bool ap_ready = false;
  bool server_ready = false;
  bool tcp_enabled = false;
  bool startup_attempts_exhausted = false;
  bool running = false;
  bool network_ready = false;
  bool connected = false;
  bool backpressure_active = false;
  bool pressure_active = false;
  bool live_session_active = false;
  uint32_t backpressure_duration_ms = 0;
  uint32_t pressure_duration_ms = 0;
  uint32_t stall_event_sequence = 0;
  uint32_t stall_event_duration_ms = 0;
  uint32_t heartbeat_ms = 0;
  uint32_t stack_free_bytes = 0;
  uint32_t stack_max_used_bytes = 0;
  int32_t last_network_error = 0;
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  // SERVICE_HIL_OBSERVABILITY: persistent failure evidence is distinct from
  // the rapidly changing accept-poll call snapshot. It is never a control gate.
  WifiWorkerCallPhase last_failure_phase = WifiWorkerCallPhase::Idle;
  int32_t last_failure_result = 0;
#endif
  uint32_t queue_bytes = 0;
  uint32_t queue_high_water_bytes = 0;
  uint32_t queue_records = 0;
  uint32_t queue_high_water_records = 0;
  uint32_t fresh_anchor_pending_bytes = 0;
  uint32_t fresh_anchor_pending_records = 0;
  uint64_t accepted_bytes_total = 0;
  uint32_t accepted_records_total = 0;
  uint64_t last_accepted_publish_seq = 0;
  uint32_t rx_epoch_generation = 0;
  bool accepted_sequence_valid = false;
  uint64_t boot_session_id = 0;
  WifiCloseReason last_close_reason = WifiCloseReason::None;
  WifiWorkerCounters counters;
};

}  // namespace csm::board::uplink
