#include "board/uplink/WifiTcpSink.h"

#if BOARD_ENABLE_WIFI_UPLINK
#include "board/uplink/WifiSocketWorker.h"
#endif

namespace csm::board::uplink {

bool WifiTcpSink::begin(const WifiTcpSinkConfig& config) {
  config_ = config;
  counters_ = {};
  worker_state_ = {};
  worker_state_.runtime_mode = config_.runtime_mode;
  worker_state_.tcp_enabled = wifiRuntimeModeEnablesTcp(config_.runtime_mode);
  worker_ = nullptr;
  enabled_ = false;
  connected_ = false;
  backpressure_active_ = false;
  isolation_latched_ = false;
  isolation_pending_worker_epoch_ = false;
  isolated_call_sequence_ = 0;
  observed_worker_epoch_ = 0;
  effective_connection_epoch_ = 0;
  logical_call_stall_total_ = 0;
  service_reported_bytes_sent_total_ = 0;
  service_reported_frames_sent_total_ = 0;
  service_reported_stall_event_sequence_ = 0;
  service_reported_queue_pressure_close_total_ = 0;
  session_anchor_queued_ = false;
#if BOARD_ENABLE_WIFI_UPLINK
  if (!wifiRuntimeModeStartsWorker(config_.runtime_mode)) return false;
  static WifiSocketWorker socket_worker(mailbox_);
  worker_ = &socket_worker;
  enabled_ = worker_->start(config_);
  if (!enabled_) {
    counters_.worker_start_total = 1;
    counters_.worker_start_fail_total = 1;
    counters_.tx_worker_start_fail_total++;
    mailbox_.tryReadState(worker_state_);
  }
#endif
  return enabled_;
}

bool WifiTcpSink::enabled() const { return enabled_; }

bool WifiTcpSink::connected() const {
  return enabled_ && wifiRuntimeModeEnablesTcp(config_.runtime_mode) &&
         connected_;
}

SinkOfferResult WifiTcpSink::offer(const PublishedFrameView& frame) {
  if (!wifiRuntimeModeEnablesTcp(config_.runtime_mode)) {
    return SinkOfferResult::Disabled;
  }
  if (!enabled()) return SinkOfferResult::Disabled;
  if (!connected()) {
    counters_.offer_disconnected_total++;
    return SinkOfferResult::Disconnected;
  }
  const WifiMailboxOfferResult offered = mailbox_.tryOffer(frame, millis());
  switch (offered) {
    case WifiMailboxOfferResult::Accepted:
      break;
    case WifiMailboxOfferResult::Busy:
      counters_.offer_busy_total++;
      counters_.offer_overflow_total++;
      return SinkOfferResult::Overflow;
    case WifiMailboxOfferResult::Reserved:
      counters_.offer_reserved_total++;
      counters_.offer_overflow_total++;
      return SinkOfferResult::Overflow;
    case WifiMailboxOfferResult::Full:
      counters_.offer_full_total++;
      counters_.offer_overflow_total++;
      return SinkOfferResult::Overflow;
    case WifiMailboxOfferResult::Invalid:
      counters_.offer_invalid_total++;
      return SinkOfferResult::Invalid;
  }
  counters_.offer_accept_total++;
  if (frame.type == csm::RecordType::StreamSession) {
    session_anchor_queued_ = true;
  }
  if (!counters_.first_accepted_valid) {
    counters_.first_accepted_valid = true;
    counters_.first_accepted_publish_seq = frame.publish_seq;
  }
  counters_.last_accepted_publish_seq = frame.publish_seq;
  const WifiMailboxQueueSnapshot queued = mailbox_.queueSnapshot();
  counters_.queue_high_water_bytes = queued.high_water_bytes;
  counters_.queue_high_water_records = queued.high_water_records;
  return SinkOfferResult::Accepted;
}

SinkServiceResult WifiTcpSink::service(uint32_t byte_budget, uint32_t now_ms,
                                       uint32_t now_us) {
  (void)byte_budget;
  (void)now_us;
  SinkServiceResult result;
#if BOARD_ENABLE_WIFI_UPLINK
  if (!enabled_) return result;
  WifiWorkerStateSnapshot state;
  if (mailbox_.tryReadState(state)) syncWorkerState(state, result);
  isolateStalledCall(mailbox_.callSnapshot(), now_ms, result);

  result.actual_bytes =
      counters_.bytes_sent_total - service_reported_bytes_sent_total_;
  result.frames_completed =
      counters_.frame_sent_total - service_reported_frames_sent_total_;
  service_reported_bytes_sent_total_ = counters_.bytes_sent_total;
  service_reported_frames_sent_total_ = counters_.frame_sent_total;

  if (worker_state_.stall_event_sequence !=
      service_reported_stall_event_sequence_) {
    service_reported_stall_event_sequence_ = worker_state_.stall_event_sequence;
    result.backpressure_event = true;
    result.backpressure_duration_ms = worker_state_.stall_event_duration_ms;
  }
  if (counters_.queue_pressure_close_total !=
      service_reported_queue_pressure_close_total_) {
    service_reported_queue_pressure_close_total_ =
        counters_.queue_pressure_close_total;
    result.queue_pressure_event = true;
  }
#else
  (void)now_ms;
#endif
  return result;
}

void WifiTcpSink::abortQueuedFrames() { mailbox_.requestAbort(); }

Stream* WifiTcpSink::downlinkStream() { return connected() ? this : nullptr; }

int WifiTcpSink::available() {
  const uint32_t available = mailbox_.rxAvailable();
  return available > static_cast<uint32_t>(INT32_MAX)
             ? INT32_MAX
             : static_cast<int>(available);
}

int WifiTcpSink::read() { return mailbox_.readRx(); }

int WifiTcpSink::peek() { return mailbox_.peekRx(); }

uint32_t WifiTcpSink::workerHeartbeatAgeMs(uint32_t now_ms) const {
  if (!worker_state_.worker_started) return 0;
  const WifiWorkerCallSnapshot call = mailbox_.callSnapshot();
  return now_ms - call.heartbeat_ms;
}

void WifiTcpSink::syncWorkerState(const WifiWorkerStateSnapshot& state,
                                  SinkServiceResult& result) {
  const uint32_t worker_epoch_delta =
      state.counters.connection_epoch - observed_worker_epoch_;
  observed_worker_epoch_ = state.counters.connection_epoch;
  uint32_t reportable_epoch_delta = worker_epoch_delta;
  if (isolation_pending_worker_epoch_ && reportable_epoch_delta > 0) {
    --reportable_epoch_delta;
    isolation_pending_worker_epoch_ = false;
    isolation_latched_ = false;
  }
  if (reportable_epoch_delta > 0) {
    effective_connection_epoch_ += reportable_epoch_delta;
    result.epoch_changed = true;
    mailbox_.discardRx();
    session_anchor_queued_ = false;
  }

  worker_state_ = state;
  connected_ = state.tcp_enabled && state.connected && !isolation_latched_;
  backpressure_active_ = state.tcp_enabled && state.backpressure_active;
  const WifiWorkerCounters& worker = state.counters;
  counters_.worker_start_total = worker.worker_start_total;
  counters_.worker_start_fail_total = worker.worker_start_fail_total;
  counters_.startup_attempt_total = worker.startup_attempt_total;
  counters_.startup_exhausted_total = worker.startup_exhausted_total;
  counters_.ap_start_total = worker.ap_start_total;
  counters_.ap_start_fail_total = worker.ap_start_fail_total;
  counters_.server_start_total = worker.server_start_total;
  counters_.server_start_fail_total = worker.server_start_fail_total;
  counters_.bytes_sent_total = worker.bytes_sent_total;
  counters_.frame_sent_total = worker.frame_sent_total;
  counters_.write_attempt_total = worker.write_attempt_total;
  counters_.partial_write_total = worker.partial_write_total;
  counters_.zero_write_total = worker.zero_write_total;
  counters_.would_block_total = worker.would_block_total;
  counters_.backpressure_total = worker.backpressure_total;
  counters_.backpressure_max_duration_ms = worker.backpressure_max_duration_ms;
  counters_.queue_abort_total = worker.queue_abort_total;
  counters_.queue_aborted_bytes_total = worker.queue_aborted_bytes_total;
  counters_.connection_epoch = effective_connection_epoch_;
  counters_.connect_total = worker.connect_total;
  counters_.disconnect_total = worker.disconnect_total;
  counters_.extra_client_reject_total = worker.extra_client_reject_total;
  counters_.stall_close_total = worker.stall_close_total + logical_call_stall_total_;
  counters_.queue_pressure_close_total = worker.queue_pressure_close_total;
  counters_.socket_error_total = worker.socket_error_total;
  counters_.send_budget_overrun_total = worker.send_budget_overrun_total;
  counters_.send_call_max_us = worker.send_call_max_us;
  counters_.recv_call_max_us = worker.recv_call_max_us;
  counters_.close_call_max_us = worker.close_call_max_us;
  counters_.tx_worker_stall_total = logical_call_stall_total_;
  counters_.tx_late_result_total = worker.late_send_result_total;
  counters_.rx_overflow_total = worker.rx_overflow_total;
  counters_.worker_returned_slow_call_total =
      worker.worker_returned_slow_call_total;
  counters_.last_sent_publish_seq = worker.last_sent_publish_seq;
  const WifiMailboxQueueSnapshot queued = mailbox_.queueSnapshot();
  counters_.queue_high_water_bytes = queued.high_water_bytes;
  counters_.queue_high_water_records = queued.high_water_records;
}

void WifiTcpSink::isolateStalledCall(const WifiWorkerCallSnapshot& call,
                                     uint32_t now_ms,
                                     SinkServiceResult& result) {
  // This is only a logical sink quarantine. A worker thread cannot cancel a
  // vendor call already in progress or contain an MCU reset/power failure.
  // Startup phases remain visible through workerCallSnapshot(), but there is
  // no active TCP epoch to close until a client has connected.
  if (!connected_ || !call.in_progress || config_.call_stall_timeout_ms == 0 ||
      static_cast<uint32_t>(now_ms - call.started_ms) <
          config_.call_stall_timeout_ms ||
      call.sequence == isolated_call_sequence_) {
    return;
  }
  isolated_call_sequence_ = call.sequence;
  const bool was_connected = connected_;
  connected_ = false;
  isolation_latched_ = was_connected;
  isolation_pending_worker_epoch_ = was_connected;
  logical_call_stall_total_++;
  counters_.tx_worker_stall_total = logical_call_stall_total_;
  counters_.stall_close_total =
      worker_state_.counters.stall_close_total + logical_call_stall_total_;
  mailbox_.requestDisconnect();
  mailbox_.discardRx();
  result.backpressure_event = true;
  result.backpressure_duration_ms = now_ms - call.started_ms;
  if (was_connected) {
    effective_connection_epoch_++;
    counters_.connection_epoch = effective_connection_epoch_;
    result.epoch_changed = true;
    session_anchor_queued_ = false;
  }
}

}  // namespace csm::board::uplink
