#include "board/uplink/WifiSocketWorker.h"

#if BOARD_ENABLE_WIFI_UPLINK

#include <Arduino.h>
#include <WiFi.h>
#include <chrono>
#include <new>

namespace csm::board::uplink {

WifiSocketWorker::WifiSocketWorker(WifiWorkerMailbox& mailbox)
    : mailbox_(mailbox) {}

bool WifiSocketWorker::start(const WifiTcpSinkConfig& config) {
  if (thread_started_) {
    return false;
  }
  config_ = config;
  state_ = {};
  state_.runtime_mode = config_.runtime_mode;
  state_.tcp_enabled = wifiRuntimeModeEnablesTcp(config_.runtime_mode);
  state_.counters.worker_start_total = 1;
  startup_complete_ = false;
  next_startup_attempt_ms_ = 0;
  if (!wifiRuntimeModeStartsWorker(config_.runtime_mode) ||
      config_.startup_attempt_limit == 0 || config_.ap_ssid == nullptr ||
      config_.ap_passphrase == nullptr ||
      (state_.tcp_enabled && config_.port == 0)) {
    state_.counters.worker_start_fail_total = 1;
    mailbox_.publishState(state_);
    return false;
  }
  state_.worker_started = true;
  mailbox_.publishState(state_);
  thread_ = new (thread_storage_)
      rtos::Thread(osPriorityBelowNormal, sizeof(thread_stack_), thread_stack_,
                   "wifi-socket");
  const osStatus status = thread_->start(mbed::callback(this, &WifiSocketWorker::run));
  if (status != osOK) {
    thread_->~Thread();
    thread_ = nullptr;
    state_.worker_started = false;
    state_.counters.worker_start_fail_total = 1;
    mailbox_.publishState(state_);
    return false;
  }
  thread_started_ = true;
  return true;
}

void WifiSocketWorker::run() {
  state_.running = true;
  publishState(millis());
  while (true) {
    const uint32_t now_ms = millis();
    if (!startup_complete_) {
      if (!state_.startup_attempts_exhausted &&
          static_cast<int32_t>(now_ms - next_startup_attempt_ms_) >= 0) {
        startup_complete_ = initializeNetwork();
        if (!startup_complete_) {
          if (state_.counters.startup_attempt_total >=
              config_.startup_attempt_limit) {
            state_.startup_attempts_exhausted = true;
            state_.counters.startup_exhausted_total++;
          } else {
            next_startup_attempt_ms_ = millis() + 1000u;
          }
        }
      }
      publishState(millis());
      rtos::ThisThread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    // AccessPointOnly deliberately stops at radio/AP readiness. No server or
    // socket lifecycle is reachable in this mode.
    if (!state_.tcp_enabled) {
      publishState(now_ms);
      rtos::ThisThread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }

    serviceRequests();
    if (client_ == nullptr) {
      if (static_cast<uint32_t>(now_ms - last_accept_poll_ms_) >=
          BOARD_WIFI_ACCEPT_POLL_MS) {
        last_accept_poll_ms_ = now_ms;
        serviceAccept(now_ms, false);
      }
    } else {
      serviceClient(now_ms);
    }
    publishState(millis());
    rtos::ThisThread::sleep_for(std::chrono::milliseconds(5));
  }
}

bool WifiSocketWorker::initializeNetwork() {
  state_.counters.startup_attempt_total++;
  state_.ap_ready = false;
  state_.server_ready = false;
  state_.network_ready = false;
  state_.counters.ap_start_total++;

  beginCall(WifiWorkerCallPhase::ConfigureIp);
  WiFi.config(IPAddress(config_.ip[0], config_.ip[1], config_.ip[2], config_.ip[3]));
  endCall(0);

  beginCall(WifiWorkerCallPhase::BeginAccessPoint);
  const int status = WiFi.beginAP(config_.ap_ssid, config_.ap_passphrase,
                                  config_.channel);
  endCall(status);
  if (status != WL_AP_LISTENING && status != WL_AP_CONNECTED) {
    state_.counters.ap_start_fail_total++;
    state_.last_network_error = status;
    return false;
  }
  state_.ap_ready = true;
  state_.network_ready = true;
  state_.last_network_error = 0;

  if (!wifiRuntimeModeEnablesTcp(config_.runtime_mode)) return true;

  state_.counters.server_start_total++;
  beginCall(WifiWorkerCallPhase::BeginServer);
  server_.begin(config_.port);
  const bool ready = static_cast<bool>(server_);
  endCall(ready ? 0 : NSAPI_ERROR_NO_SOCKET);
  if (!ready) {
    state_.counters.server_start_fail_total++;
    state_.last_network_error = NSAPI_ERROR_NO_SOCKET;
    return false;
  }

  state_.server_ready = true;
  state_.last_network_error = 0;
  return true;
}

void WifiSocketWorker::serviceRequests() {
  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (disconnect_sequence != handled_disconnect_sequence_) {
    handled_disconnect_sequence_ = disconnect_sequence;
    closeClient(false);
  }
  applyAbortRequest();
}

void WifiSocketWorker::serviceClient(uint32_t now_ms) {
  if (pending_consume_ && !applyPendingConsume()) return;
  serviceReceive(now_ms);
  if (client_ == nullptr) return;
  serviceTransmit(now_ms);
  if (client_ == nullptr) return;
  if (static_cast<uint32_t>(now_ms - last_extra_accept_ms_) >= 100u) {
    last_extra_accept_ms_ = now_ms;
    serviceAccept(now_ms, true);
  }
}

void WifiSocketWorker::serviceAccept(uint32_t, bool extra) {
  nsapi_error_t error = NSAPI_ERROR_WOULD_BLOCK;
  beginCall(extra ? WifiWorkerCallPhase::AcceptExtraClient
                  : WifiWorkerCallPhase::AcceptClient);
  TCPSocket* candidate = server_.acceptRaw(&error);
  endCall(error);
  if (candidate == nullptr) {
    if (error != NSAPI_ERROR_WOULD_BLOCK) noteSocketError(error);
    return;
  }

  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (disconnect_sequence != handled_disconnect_sequence_) {
    handled_disconnect_sequence_ = disconnect_sequence;
    closeSocket(candidate, WifiWorkerCallPhase::CloseClient,
                WifiWorkerCallPhase::DeleteClient);
    applyAbortRequest();
    return;
  }

  beginCall(WifiWorkerCallPhase::ConfigureClient);
  candidate->set_blocking(false);
  endCall(0);

  if (extra) {
    closeSocket(candidate, WifiWorkerCallPhase::CloseExtraClient,
                WifiWorkerCallPhase::DeleteExtraClient);
    state_.counters.extra_client_reject_total++;
    return;
  }

  uint32_t aborted_bytes = 0;
  while (!mailbox_.tryApplyAbort(aborted_bytes)) rtos::ThisThread::yield();
  if (aborted_bytes > 0) {
    state_.counters.queue_abort_total++;
    state_.counters.queue_aborted_bytes_total += aborted_bytes;
  }
  mailbox_.discardRx();
  client_ = candidate;
  state_.connected = true;
  blocked_since_ms_ = 0;
  state_.backpressure_active = false;
  state_.backpressure_duration_ms = 0;
  state_.counters.connection_epoch++;
  state_.counters.connect_total++;
}

void WifiSocketWorker::serviceReceive(uint32_t) {
  beginCall(WifiWorkerCallPhase::Receive);
  const nsapi_size_or_error_t received = client_->recv(rx_buffer_, sizeof(rx_buffer_));
  const uint32_t duration_us = endCall(received);
  if (duration_us > state_.counters.recv_call_max_us) {
    state_.counters.recv_call_max_us = duration_us;
  }
  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (disconnect_sequence != handled_disconnect_sequence_) {
    handled_disconnect_sequence_ = disconnect_sequence;
    closeClient(false);
    applyAbortRequest();
    return;
  }
  if (received > 0) {
    if (!mailbox_.pushRx(rx_buffer_, static_cast<uint16_t>(received))) {
      state_.counters.rx_overflow_total++;
      closeClient(true);
    }
    return;
  }
  if (received == 0) {
    closeClient(false);
  } else if (received != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(received));
    closeClient(false);
  }
}

void WifiSocketWorker::serviceTransmit(uint32_t now_ms) {
  const WifiMailboxQueueSnapshot queued = mailbox_.queueSnapshot();
  if (queued.queued_records == 0) return;
  if (!queued.urgent && queued.queued_records < config_.batch_min_records &&
      config_.batch_max_latency_ms > 0 &&
      static_cast<uint32_t>(now_ms - queued.first_queued_ms) <
          config_.batch_max_latency_ms) {
    return;
  }

  uint32_t configured = config_.max_bytes_per_pump;
  if (configured == 0 || configured > BOARD_WIFI_TX_CHUNK_BYTES) {
    configured = BOARD_WIFI_TX_CHUNK_BYTES;
  }
  WifiMailboxTxLease lease;
  if (!mailbox_.tryStageTx(tx_buffer_, static_cast<uint16_t>(configured), lease)) return;

  state_.counters.write_attempt_total++;
  beginCall(WifiWorkerCallPhase::Send);
  const nsapi_size_or_error_t sent = client_->send(tx_buffer_, lease.length);
  const uint32_t duration_us = endCall(sent);
  if (duration_us > state_.counters.send_call_max_us) {
    state_.counters.send_call_max_us = duration_us;
  }
  if (config_.drain_time_budget_us > 0 &&
      duration_us > config_.drain_time_budget_us) {
    state_.counters.send_budget_overrun_total++;
  }
  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (disconnect_sequence != handled_disconnect_sequence_) {
    handled_disconnect_sequence_ = disconnect_sequence;
    state_.counters.late_send_result_total++;
    closeClient(false);
    applyAbortRequest();
    return;
  }

  if (sent < 0 && sent != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(sent));
    closeClient(false);
    return;
  }
  if (sent <= 0) {
    state_.counters.zero_write_total++;
    if (blocked_since_ms_ == 0) {
      blocked_since_ms_ = now_ms == 0 ? 1 : now_ms;
      state_.counters.backpressure_total++;
    }
    state_.backpressure_active = true;
    state_.backpressure_duration_ms = now_ms - blocked_since_ms_;
    if (state_.backpressure_duration_ms >
        state_.counters.backpressure_max_duration_ms) {
      state_.counters.backpressure_max_duration_ms =
          state_.backpressure_duration_ms;
    }
    const uint8_t normal_limit = static_cast<uint8_t>(
        BOARD_WIFI_SINK_QUEUE_RECORDS - BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS);
    if ((config_.stall_timeout_ms > 0 &&
         state_.backpressure_duration_ms >= config_.stall_timeout_ms) ||
        queued.queued_records >= normal_limit) {
      closeClient(true);
    }
    return;
  }

  pending_lease_ = lease;
  pending_consumed_bytes_ = sent > lease.length ? lease.length
                                                 : static_cast<uint16_t>(sent);
  pending_consume_ = true;
  if (pending_consumed_bytes_ < lease.length) state_.counters.partial_write_total++;
  applyPendingConsume();
  if (blocked_since_ms_ != 0) {
    const uint32_t duration = now_ms - blocked_since_ms_;
    if (duration > state_.counters.backpressure_max_duration_ms) {
      state_.counters.backpressure_max_duration_ms = duration;
    }
    blocked_since_ms_ = 0;
    state_.backpressure_active = false;
    state_.backpressure_duration_ms = 0;
  }
}

bool WifiSocketWorker::applyPendingConsume() {
  if (!pending_consume_) return true;
  FixedFrameQueue<BOARD_WIFI_SINK_QUEUE_RECORDS>::ConsumeResult consumed;
  bool stale = false;
  if (!mailbox_.tryConsumeTx(pending_lease_, pending_consumed_bytes_, consumed,
                             stale)) {
    return false;
  }
  pending_consume_ = false;
  if (stale) {
    state_.counters.late_send_result_total++;
    return true;
  }
  state_.counters.bytes_sent_total += pending_consumed_bytes_;
  state_.counters.frame_sent_total += consumed.frames;
  if (consumed.frames > 0) {
    state_.counters.last_sent_publish_seq = consumed.last_publish_seq;
  }
  return true;
}

void WifiSocketWorker::closeClient(bool stalled) {
  const bool was_connected = client_ != nullptr || state_.connected;
  closeSocket(client_, WifiWorkerCallPhase::CloseClient,
              WifiWorkerCallPhase::DeleteClient);
  state_.connected = false;
  blocked_since_ms_ = 0;
  state_.backpressure_active = false;
  state_.backpressure_duration_ms = 0;
  pending_consume_ = false;
  mailbox_.discardRx();
  uint32_t aborted_bytes = 0;
  while (!mailbox_.tryApplyAbort(aborted_bytes)) rtos::ThisThread::yield();
  if (aborted_bytes > 0) {
    state_.counters.queue_abort_total++;
    state_.counters.queue_aborted_bytes_total += aborted_bytes;
  }
  if (!was_connected) return;
  state_.counters.connection_epoch++;
  state_.counters.disconnect_total++;
  if (stalled) {
    state_.counters.stall_close_total++;
    state_.stall_event_duration_ms = config_.stall_timeout_ms;
    state_.stall_event_sequence++;
  }
}

void WifiSocketWorker::closeSocket(TCPSocket*& socket,
                                   WifiWorkerCallPhase close_phase,
                                   WifiWorkerCallPhase delete_phase) {
  TCPSocket* owned = socket;
  socket = nullptr;
  if (owned == nullptr) return;
  beginCall(close_phase);
  const nsapi_error_t error = owned->close();
  const uint32_t duration_us = endCall(error);
  if (duration_us > state_.counters.close_call_max_us) {
    state_.counters.close_call_max_us = duration_us;
  }
  if (error != NSAPI_ERROR_OK) noteSocketError(error);
  beginCall(delete_phase);
  delete owned;
  endCall(0);
}

void WifiSocketWorker::applyAbortRequest() {
  const uint32_t requested = mailbox_.abortRequestSequence();
  if (requested == handled_abort_sequence_) return;
  uint32_t aborted_bytes = 0;
  if (!mailbox_.tryApplyAbort(aborted_bytes)) return;
  handled_abort_sequence_ = requested;
  pending_consume_ = false;
  if (aborted_bytes > 0) {
    state_.counters.queue_abort_total++;
    state_.counters.queue_aborted_bytes_total += aborted_bytes;
  }
}

void WifiSocketWorker::noteSocketError(nsapi_error_t error) {
  state_.last_network_error = error;
  if (error != NSAPI_ERROR_OK && error != NSAPI_ERROR_WOULD_BLOCK) {
    state_.counters.socket_error_total++;
  }
}

void WifiSocketWorker::beginCall(WifiWorkerCallPhase phase) {
  const uint32_t now_ms = millis();
  publishState(now_ms);
  current_call_started_us_ = micros();
  mailbox_.beginCall(phase, now_ms);
  if (config_.call_persistence.enter != nullptr) {
    const WifiWorkerCallSnapshot call = mailbox_.callSnapshot();
    config_.call_persistence.enter(
        config_.call_persistence.context, static_cast<uint8_t>(phase),
        call.sequence, now_ms);
  }
}

uint32_t WifiSocketWorker::endCall(int32_t result) {
  const uint32_t duration_us = micros() - current_call_started_us_;
  const uint32_t now_ms = millis();
  mailbox_.endCall(now_ms, duration_us, result);
  if (config_.call_persistence.leave != nullptr) {
    config_.call_persistence.leave(config_.call_persistence.context, result,
                                   duration_us, now_ms);
  }
  if (config_.call_stall_timeout_ms > 0 &&
      duration_us >= config_.call_stall_timeout_ms * 1000u) {
    state_.counters.worker_returned_slow_call_total++;
  }
  publishState(now_ms);
  return duration_us;
}

void WifiSocketWorker::publishState(uint32_t now_ms) {
  sampleStack(now_ms);
  state_.heartbeat_ms = now_ms;
  mailbox_.publishState(state_);
}

void WifiSocketWorker::sampleStack(uint32_t now_ms) {
  if (thread_ == nullptr ||
      (last_stack_sample_ms_ != 0 &&
       static_cast<uint32_t>(now_ms - last_stack_sample_ms_) < 1000u)) {
    return;
  }
  last_stack_sample_ms_ = now_ms == 0 ? 1 : now_ms;
  state_.stack_free_bytes = thread_->free_stack();
  state_.stack_max_used_bytes = thread_->max_stack();
}

}  // namespace csm::board::uplink

#endif
