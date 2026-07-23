#pragma once

#include <stdint.h>

#ifndef BOARD_WIFI_STALL_TIMEOUT_MS
#define BOARD_WIFI_STALL_TIMEOUT_MS 5000
#endif

#ifndef BOARD_WIFI_CALL_STALL_TIMEOUT_MS
#define BOARD_WIFI_CALL_STALL_TIMEOUT_MS 250
#endif

#ifndef BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS
#define BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS 75
#endif

#ifndef BOARD_WIFI_TX_BATCH_MIN_RECORDS
#define BOARD_WIFI_TX_BATCH_MIN_RECORDS 2
#endif

#ifndef BOARD_WIFI_ACCEPT_POLL_MS
#define BOARD_WIFI_ACCEPT_POLL_MS 25
#endif

#ifndef BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT
#define BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT 75
#endif

static_assert(BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT > 0 &&
                  BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT < 100,
              "Wi-Fi isolation high-water percent must be in (0, 100)");

namespace csm::board::uplink {

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
  uint8_t channel = 6;
  uint8_t ip[4] = {192, 168, 4, 1};
  uint32_t drain_time_budget_us = 0;
  uint32_t max_writes_per_pump = 0;
  uint32_t max_bytes_per_pump = 0;
  uint32_t stall_timeout_ms = BOARD_WIFI_STALL_TIMEOUT_MS;
  uint32_t call_stall_timeout_ms = BOARD_WIFI_CALL_STALL_TIMEOUT_MS;
  uint32_t batch_max_latency_ms = BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS;
  uint8_t batch_min_records = BOARD_WIFI_TX_BATCH_MIN_RECORDS;
  uint8_t isolate_high_water_percent = BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT;
  // Diagnostic profiles make one observable startup attempt by default. A
  // larger value permits only that many bounded retries; zero is invalid.
  uint8_t startup_attempt_limit = 1;
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

// Socket no-progress is tracked independently from queue pressure. The worker
// may isolate a client before the bounded SPSC queue reaches Full; neither
// condition is allowed to block the main/RC/CAN producer.
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
  bool in_progress = false;
  uint32_t sequence = 0;
  uint32_t started_ms = 0;
  uint32_t duration_us = 0;
  int32_t result = 0;
  uint32_t heartbeat_ms = 0;
};

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
  uint32_t frame_sent_total = 0;
  uint32_t write_attempt_total = 0;
  uint32_t partial_write_total = 0;
  uint32_t zero_write_total = 0;
  uint32_t would_block_total = 0;
  uint32_t backpressure_total = 0;
  uint32_t backpressure_max_duration_ms = 0;
  uint32_t queue_abort_total = 0;
  uint32_t queue_aborted_bytes_total = 0;
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
  uint64_t last_sent_publish_seq = 0;
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
  uint32_t backpressure_duration_ms = 0;
  uint32_t stall_event_sequence = 0;
  uint32_t stall_event_duration_ms = 0;
  uint32_t heartbeat_ms = 0;
  uint32_t stack_free_bytes = 0;
  uint32_t stack_max_used_bytes = 0;
  int32_t last_network_error = 0;
  WifiCloseReason last_close_reason = WifiCloseReason::None;
  WifiWorkerCounters counters;
};

}  // namespace csm::board::uplink
