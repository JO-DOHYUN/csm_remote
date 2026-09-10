#pragma once

#include <stdint.h>

#include <Arduino.h>

#ifndef BOARD_ENABLE_WIFI_UPLINK
#define BOARD_ENABLE_WIFI_UPLINK 0
#endif

#ifndef BOARD_WIFI_SINK_QUEUE_RECORDS
#define BOARD_WIFI_SINK_QUEUE_RECORDS 256
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS 4
#endif

#include "board/uplink/WifiWorkerContract.h"
#include "board/uplink/WifiControlPlaneMailbox.h"
#include "board/uplink/WifiRealtimeMailbox.h"
#include "board/uplink/WifiWorkerMailbox.h"
#include "board/uplink/WifiTransportDiagnostic.h"

static_assert(BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS < BOARD_WIFI_SINK_QUEUE_RECORDS,
              "Wi-Fi critical reserve must leave normal queue capacity");
static_assert(BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES < BOARD_WIFI_SINK_QUEUE_BYTES,
              "Wi-Fi critical byte reserve must leave normal byte capacity");
static_assert(BOARD_WIFI_STALL_TIMEOUT_MS > 0,
              "Wi-Fi backpressure timeout must be non-zero");
static_assert(BOARD_WIFI_CALL_STALL_TIMEOUT_MS > 0,
              "Wi-Fi socket call isolation timeout must be non-zero");

namespace csm::board::uplink {

class WifiSocketWorker;

struct WifiTcpSinkCounters {
  uint32_t worker_start_total = 0;
  uint32_t worker_start_fail_total = 0;
  uint32_t startup_attempt_total = 0;
  uint32_t startup_exhausted_total = 0;
  uint32_t ap_start_total = 0;
  uint32_t ap_start_fail_total = 0;
  uint32_t server_start_total = 0;
  uint32_t server_start_fail_total = 0;
  uint32_t offer_total = 0;
  uint32_t offer_bytes_total = 0;
  uint64_t offer_bytes_total64 = 0;
  uint32_t offer_accept_total = 0;
  uint32_t offer_accept_bytes_total = 0;
  uint64_t offer_accept_bytes_total64 = 0;
  uint32_t offer_disconnected_total = 0;
  uint32_t offer_overflow_total = 0;
  uint32_t offer_busy_total = 0;
  uint32_t offer_reserved_total = 0;
  uint32_t offer_full_total = 0;
  uint32_t offer_invalid_total = 0;
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
  uint32_t queue_aborted_bytes_total = 0;
  uint32_t queue_aborted_records_total = 0;
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
  uint32_t queue_high_water_bytes = 0;
  uint32_t queue_high_water_records = 0;
  uint32_t tx_worker_start_fail_total = 0;
  uint32_t tx_worker_stall_total = 0;
  uint32_t tx_late_result_total = 0;
  uint32_t rx_overflow_total = 0;
  uint32_t worker_returned_slow_call_total = 0;
  uint32_t wake_total = 0;
  uint32_t wake_tx_data_total = 0;
  uint32_t wake_socket_state_total = 0;
  uint32_t wake_control_total = 0;
  uint32_t wake_startup_total = 0;
  uint32_t wake_fallback_total = 0;
  uint32_t sigio_total = 0;
  uint32_t empty_to_nonempty_wake_total = 0;
  uint32_t latency_wake_total = 0;
  uint32_t positive_write_total = 0;
  uint32_t bytes_per_wake_max = 0;
  uint32_t writes_per_wake_max = 0;
  uint64_t first_accepted_publish_seq = 0;
  uint64_t last_accepted_publish_seq = 0;
  uint64_t last_sent_publish_seq = 0;
  uint64_t first_lost_publish_seq = 0;
  uint64_t last_lost_publish_seq = 0;
  uint32_t live_fifo_loss_total = 0;
  uint32_t session_anchor_miss_total = 0;
  uint64_t last_session_anchor_miss_publish_seq = 0;
  bool first_accepted_valid = false;
  bool loss_range_valid = false;
};

class WifiTcpSink final : public IFrameSink, public Stream {
 public:
  using TxStorage = WifiWorkerMailbox::TxStorage;

  explicit WifiTcpSink(TxStorage& storage) : mailbox_(storage) {}

  bool begin(const WifiTcpSinkConfig& config);
  bool enabled() const override;
  bool connected() const override;
  bool socketConnected() const;
  SinkOfferResult offer(const PublishedFrameView& frame) override;
  SinkServiceResult service(uint32_t byte_budget, uint32_t now_ms,
                            uint32_t now_us);
  bool isolateMissedSessionAnchor(uint64_t publish_seq);
  void abortQueuedFrames();
  Stream* downlinkStream();
  Stream* controlDownlinkStream();
  bool offerControlAck(const uint8_t* payload, uint16_t length);
  bool controlConnected() const { return control_mailbox_.connected(); }
  ControlTransportEvidence controlEvidence() const {
    return control_mailbox_.evidence();
  }
  uint32_t controlConnectionEpoch() const {
    return control_mailbox_.connectionEpoch();
  }
  bool takeRealtimeDatagram(WifiRealtimeDatagram* datagram) {
    return realtime_mailbox_.takeLatestRx(datagram);
  }
  bool offerRealtimeProof(const uint8_t* bytes, uint16_t length,
                          const WifiRealtimeDatagram& request, uint32_t proof_sequence,
                          uint32_t now_ms) {
    return realtime_mailbox_.stageProof(bytes, length, request,
                                        proof_sequence, now_ms);
  }
  WifiRealtimeEvidence realtimeEvidence() const {
    return realtime_mailbox_.evidence();
  }

  int available() override;
  int read() override;
  int peek() override;
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t) override { return 0; }

  bool hasPendingFrames() const {
    return mailbox_.queueSnapshot().unsent_records != 0;
  }
  bool sessionAnchorQueued() const { return session_anchor_queued_; }
  bool backpressureActive() const { return backpressure_active_; }
  uint32_t queuedBytes() const { return mailbox_.queueSnapshot().queued_bytes; }
  const WifiTcpSinkCounters& counters() const { return counters_; }
  WifiWorkerCallSnapshot workerCallSnapshot() const {
    return mailbox_.callSnapshot();
  }
  WifiWorkerStateSnapshot workerStateSnapshot() const { return worker_state_; }
  WifiCloseReason lastCloseReason() const {
    return worker_state_.last_close_reason;
  }
  uint32_t workerHeartbeatAgeMs(uint32_t now_ms) const;
  WifiTransportDiagnosticSnapshot diagnosticSnapshot(
      uint64_t mono_us, uint32_t now_ms) const;
 private:
  friend struct ObservationOwnerTest;
  WifiWorkerMailbox mailbox_;
  WifiControlPlaneMailbox control_mailbox_;
  WifiRealtimeMailbox realtime_mailbox_;
  WifiSocketWorker* worker_ = nullptr;
  WifiTcpSinkConfig config_;
  WifiTcpSinkCounters counters_;
  WifiWorkerStateSnapshot worker_state_;
  bool enabled_ = false;
  bool connected_ = false;
  bool backpressure_active_ = false;
  bool isolation_latched_ = false;
  bool isolation_pending_worker_epoch_ = false;
  uint32_t isolated_call_sequence_ = 0;
  uint32_t observed_worker_epoch_ = 0;
  uint32_t effective_connection_epoch_ = 0;
  uint32_t logical_call_stall_total_ = 0;
  uint32_t service_reported_bytes_sent_total_ = 0;
  uint32_t service_reported_frames_sent_total_ = 0;
  uint32_t service_reported_stall_event_sequence_ = 0;
  uint32_t service_reported_queue_pressure_close_total_ = 0;
  uint32_t acknowledged_queue_pressure_disconnect_sequence_ = 0;
  uint32_t observed_queue_aborted_records_total_ = 0;
  bool session_anchor_queued_ = false;

  void noteLiveLossRange(uint64_t first_publish_seq,
                         uint64_t last_publish_seq, uint32_t records);
  void syncWorkerState(const WifiWorkerStateSnapshot& state,
                       SinkServiceResult& result);
  void isolateStalledCall(const WifiWorkerCallSnapshot& call,
                          uint32_t now_ms, SinkServiceResult& result);
};

}  // namespace csm::board::uplink
