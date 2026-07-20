#pragma once

#include <stdint.h>

#include <Arduino.h>

#include "board/uplink/FixedFrameQueue.h"

#if BOARD_ENABLE_WIFI_UPLINK
#include <TCPSocket.h>
#endif

#ifndef BOARD_ENABLE_WIFI_UPLINK
#define BOARD_ENABLE_WIFI_UPLINK 0
#endif

#ifndef BOARD_WIFI_SINK_QUEUE_RECORDS
#define BOARD_WIFI_SINK_QUEUE_RECORDS 8
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS 4
#endif

#ifndef BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS
#define BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS 75
#endif

#ifndef BOARD_WIFI_TX_BATCH_MIN_RECORDS
#define BOARD_WIFI_TX_BATCH_MIN_RECORDS 2
#endif

#ifndef BOARD_WIFI_STALL_TIMEOUT_MS
#define BOARD_WIFI_STALL_TIMEOUT_MS 5000
#endif

#ifndef BOARD_WIFI_TX_CHUNK_BYTES
#define BOARD_WIFI_TX_CHUNK_BYTES 512
#endif

static_assert(BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS < BOARD_WIFI_SINK_QUEUE_RECORDS,
              "Wi-Fi critical reserve must leave normal queue capacity");
static_assert(BOARD_WIFI_STALL_TIMEOUT_MS > 0,
              "Wi-Fi stalled-client timeout must be non-zero");

namespace csm::board::uplink {

struct WifiTcpSinkConfig {
  const char* ap_ssid = nullptr;
  const char* ap_passphrase = nullptr;
  uint16_t port = 3333;
  uint8_t channel = 6;
  uint8_t ip[4] = {192, 168, 4, 1};
  uint32_t drain_time_budget_us = 0;
  uint32_t max_writes_per_pump = 0;
  uint32_t max_bytes_per_pump = 0;
  uint32_t stall_timeout_ms = BOARD_WIFI_STALL_TIMEOUT_MS;
  uint32_t batch_max_latency_ms = BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS;
  uint8_t batch_min_records = BOARD_WIFI_TX_BATCH_MIN_RECORDS;
};

struct WifiTcpSinkCounters {
  uint32_t ap_start_total = 0;
  uint32_t ap_start_fail_total = 0;
  uint32_t offer_accept_total = 0;
  uint32_t offer_disconnected_total = 0;
  uint32_t offer_overflow_total = 0;
  uint32_t bytes_sent_total = 0;
  uint32_t frame_sent_total = 0;
  uint32_t write_attempt_total = 0;
  uint32_t partial_write_total = 0;
  uint32_t zero_write_total = 0;
  uint32_t backpressure_total = 0;
  uint32_t backpressure_max_duration_ms = 0;
  uint32_t queue_abort_total = 0;
  uint32_t queue_aborted_bytes_total = 0;
  uint32_t connection_epoch = 0;
  uint32_t connect_total = 0;
  uint32_t disconnect_total = 0;
  uint32_t extra_client_reject_total = 0;
  uint32_t stall_close_total = 0;
  uint32_t socket_error_total = 0;
  uint32_t send_budget_overrun_total = 0;
  uint32_t send_call_max_us = 0;
  uint32_t recv_call_max_us = 0;
  uint32_t close_call_max_us = 0;
  uint32_t queue_high_water_bytes = 0;
  uint32_t queue_high_water_records = 0;
  uint64_t first_accepted_publish_seq = 0;
  uint64_t last_accepted_publish_seq = 0;
  uint64_t last_sent_publish_seq = 0;
  bool first_accepted_valid = false;
};

class WifiTcpSink final : public IFrameSink, public Stream {
 public:
  bool begin(const WifiTcpSinkConfig& config);
  bool enabled() const override;
  bool connected() const override;
  SinkOfferResult offer(const PublishedFrameView& frame) override;
  SinkServiceResult service(uint32_t byte_budget, uint32_t now_ms, uint32_t now_us);
  void abortQueuedFrames();
  Stream* downlinkStream();

  int available() override;
  int read() override;
  int peek() override;
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t) override { return 0; }

  bool hasPendingFrames() const { return !queue_.empty(); }
  bool backpressureActive() const { return blocked_since_ms_ != 0; }
  uint32_t queuedBytes() const { return queue_.queuedBytes(); }
  const WifiTcpSinkCounters& counters() const { return counters_; }

 private:
  FixedFrameQueue<BOARD_WIFI_SINK_QUEUE_RECORDS> queue_;
  WifiTcpSinkConfig config_;
  WifiTcpSinkCounters counters_;
  uint32_t blocked_since_ms_ = 0;
  uint32_t batch_started_ms_ = 0;
  bool urgent_flush_ = false;
  bool enabled_ = false;

  bool client_active_ = false;

#if BOARD_ENABLE_WIFI_UPLINK
  TCPSocket* client_socket_ = nullptr;
  uint8_t rx_buffer_[256] = {};
  uint16_t rx_offset_ = 0;
  uint16_t rx_length_ = 0;
  bool socket_closed_ = false;

  bool acceptClient();
  void disconnectClient(bool stalled);
  void rejectExtraClient();
  void noteSocketError(nsapi_error_t error);
#endif
  void noteBackpressure(uint32_t now_ms, SinkServiceResult& result);
};

}  // namespace csm::board::uplink
