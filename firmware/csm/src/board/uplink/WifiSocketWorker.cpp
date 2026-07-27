#include "board/uplink/WifiSocketWorker.h"

#if BOARD_ENABLE_WIFI_UPLINK

#include <Arduino.h>
#include <WiFi.h>
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
  WifiWorkerNotifier notifier;
  notifier.context = this;
  notifier.notify = &WifiSocketWorker::notifyFromMailbox;
  mailbox_.setNotifier(notifier);
  thread_ = new (thread_storage_)
      rtos::Thread(osPriorityNormal, sizeof(thread_stack_), thread_stack_,
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
  signalWake(WifiWakeStartup);
  return true;
}

void WifiSocketWorker::run() {
  state_.running = true;
  publishState(millis(), true);
  while (true) {
    const uint32_t wait_timeout_ms = nextWaitTimeoutMs(millis());
    uint32_t flags = wake_flags_.wait_any(
        WifiWakeTxData | WifiWakeSocketState | WifiWakeControl |
            WifiWakeStartup,
        wait_timeout_ms, true);
    const bool fallback = (flags & osFlagsError) != 0;
    if (fallback) flags = WifiWakeNone;
    noteWake(flags, fallback);

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
      continue;
    }

    // AccessPointOnly deliberately stops at radio/AP readiness. No server or
    // socket lifecycle is reachable in this mode.
    if (!state_.tcp_enabled) {
      publishState(now_ms);
      continue;
    }

    serviceRequests();
    if (client_ == nullptr) {
      if (static_cast<uint32_t>(now_ms - last_accept_poll_ms_) >=
          BOARD_WIFI_ACCEPT_POLL_MS) {
        last_accept_poll_ms_ = now_ms;
        serviceAccept(now_ms);
      }
    } else {
      serviceClient(now_ms);
    }
    publishState(millis());
  }
}

void WifiSocketWorker::notifyFromMailbox(void* context, uint32_t bits) {
  if (context != nullptr) {
    static_cast<WifiSocketWorker*>(context)->signalWake(bits);
  }
}

void WifiSocketWorker::onSocketStateChanged() {
  sigio_total_.fetch_add(1, std::memory_order_relaxed);
  signalWake(WifiWakeSocketState);
}

void WifiSocketWorker::signalWake(uint32_t bits) {
  if (bits != WifiWakeNone) wake_flags_.set(bits);
}

uint32_t WifiSocketWorker::nextWaitTimeoutMs(uint32_t now_ms) const {
  if (!startup_complete_) {
    if (state_.startup_attempts_exhausted ||
        static_cast<int32_t>(now_ms - next_startup_attempt_ms_) >= 0) {
      return BOARD_WIFI_STATE_PUBLISH_PERIOD_MS;
    }
    const uint32_t until_startup = next_startup_attempt_ms_ - now_ms;
    return until_startup < BOARD_WIFI_STATE_PUBLISH_PERIOD_MS
               ? until_startup
               : BOARD_WIFI_STATE_PUBLISH_PERIOD_MS;
  }
  if (!state_.tcp_enabled) return BOARD_WIFI_STATE_PUBLISH_PERIOD_MS;
  if (client_ == nullptr) {
    const uint32_t elapsed = now_ms - last_accept_poll_ms_;
    return elapsed >= BOARD_WIFI_ACCEPT_POLL_MS
               ? 0
               : BOARD_WIFI_ACCEPT_POLL_MS - elapsed;
  }
  return BOARD_WIFI_CONNECTED_FALLBACK_MS;
}

void WifiSocketWorker::noteWake(uint32_t flags, bool fallback) {
  state_.counters.wake_total++;
  if (fallback) state_.counters.wake_fallback_total++;
  if ((flags & WifiWakeTxData) != 0) {
    state_.counters.wake_tx_data_total++;
  }
  if ((flags & WifiWakeSocketState) != 0) {
    state_.counters.wake_socket_state_total++;
  }
  if ((flags & WifiWakeControl) != 0) {
    state_.counters.wake_control_total++;
  }
  if ((flags & WifiWakeStartup) != 0) {
    state_.counters.wake_startup_total++;
  }
  state_.counters.sigio_total =
      sigio_total_.load(std::memory_order_relaxed);
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
  const uint32_t queue_pressure_sequence =
      mailbox_.queuePressureDisconnectRequestSequence();
  if (queue_pressure_sequence !=
      handled_queue_pressure_disconnect_sequence_) {
    handled_queue_pressure_disconnect_sequence_ = queue_pressure_sequence;
    // Coalesce a racing logical-isolation request into this one epoch close
    // and retain the more specific admission-loss reason.
    handled_disconnect_sequence_ = mailbox_.disconnectRequestSequence();
    closeClient(WifiCloseReason::QueuePressure);
    // Publish the closed epoch before releasing producer admission. The facade
    // reads this completion sequence before its state snapshot.
    publishState(millis());
    mailbox_.markQueuePressureDisconnectHandled(queue_pressure_sequence);
    return;
  }
  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (disconnect_sequence != handled_disconnect_sequence_) {
    handled_disconnect_sequence_ = disconnect_sequence;
    closeClient(WifiCloseReason::IsolationRequest);
  }
  applyAbortRequest();
}

void WifiSocketWorker::serviceClient(uint32_t now_ms) {
  if (pending_consume_ && !applyPendingConsume()) return;
  const WifiTransmitPumpResult transmitted = serviceTransmit(now_ms);
  if (client_ == nullptr) return;
  serviceReceive(now_ms);
  if (client_ != nullptr && transmitted.progressed() &&
      !transmitted.would_block && !transmitted.zero_write &&
      mailbox_.queueSnapshot().queued_records != 0) {
    // Yield after each bounded pump, then resume without waiting for the
    // fallback timer while the socket continues to make positive progress.
    signalWake(WifiWakeTxData);
  }
}

void WifiSocketWorker::serviceAccept(uint32_t) {
  nsapi_error_t error = NSAPI_ERROR_WOULD_BLOCK;
  beginCall(WifiWorkerCallPhase::AcceptClient);
  TCPSocket* candidate = server_.acceptRaw(&error);
  endCall(error);
  if (candidate == nullptr) {
    if (error != NSAPI_ERROR_WOULD_BLOCK) noteSocketError(error);
    return;
  }

  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (disconnect_sequence != handled_disconnect_sequence_) {
    handled_disconnect_sequence_ = disconnect_sequence;
    closeSocket(candidate, WifiWorkerCallPhase::CloseClient);
    applyAbortRequest();
    return;
  }

  beginCall(WifiWorkerCallPhase::ConfigureClient);
  candidate->set_blocking(false);
  candidate->sigio(
      mbed::callback(this, &WifiSocketWorker::onSocketStateChanged));
  endCall(0);

  uint32_t aborted_bytes = 0;
  while (!mailbox_.tryApplyAbort(aborted_bytes)) rtos::ThisThread::yield();
  if (aborted_bytes > 0) {
    state_.counters.queue_abort_total++;
    state_.counters.queue_aborted_bytes_total += aborted_bytes;
  }
  mailbox_.discardRx();
  client_ = candidate;
  state_.connected = true;
  tx_progress_.reset();
  state_.backpressure_active = false;
  state_.backpressure_duration_ms = 0;
  state_.counters.connection_epoch++;
  state_.counters.connect_total++;
  publishState(millis(), true);
  signalWake(WifiWakeTxData);
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
    closeClient(WifiCloseReason::IsolationRequest);
    applyAbortRequest();
    return;
  }
  if (received > 0) {
    if (!mailbox_.pushRx(rx_buffer_, static_cast<uint16_t>(received))) {
      state_.counters.rx_overflow_total++;
      closeClient(WifiCloseReason::ReceiveOverflow);
    }
    return;
  }
  if (received == 0) {
    closeClient(WifiCloseReason::PeerClosed);
  } else if (received != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(received));
    closeClient(WifiCloseReason::SocketError);
  }
}

WifiTransmitPumpResult WifiSocketWorker::serviceTransmit(uint32_t now_ms) {
  WifiTransmitPumpResult result;
  const WifiMailboxQueueSnapshot initial_queue = mailbox_.queueSnapshot();
  if (initial_queue.queued_records == 0) return result;
  const bool initial_pressure = wifiQueuePressureReached(
      initial_queue.queued_bytes, initial_queue.queued_records,
      BOARD_WIFI_SINK_QUEUE_BYTES, BOARD_WIFI_SINK_QUEUE_RECORDS,
      config_.isolate_high_water_percent);
  if (!initial_pressure && !initial_queue.urgent &&
      initial_queue.queued_records < config_.batch_min_records &&
      config_.batch_max_latency_ms > 0 &&
      static_cast<uint32_t>(now_ms - initial_queue.first_queued_ms) <
          config_.batch_max_latency_ms) {
    return result;
  }

  WifiTxPumpBudget budget(config_.max_writes_per_pump,
                          config_.max_bytes_per_pump,
                          BOARD_WIFI_TX_CHUNK_BYTES);
  const uint32_t pump_started_us = micros();
  while (client_ != nullptr && budget.canAttempt()) {
    if (config_.drain_time_budget_us > 0 &&
        static_cast<uint32_t>(micros() - pump_started_us) >=
            config_.drain_time_budget_us) {
      break;
    }

    WifiMailboxTxLease lease;
    const uint16_t write_capacity = budget.nextWriteCapacity();
    if (!mailbox_.tryStageTx(tx_buffer_, write_capacity, lease)) break;

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

    const uint16_t progressed_bytes =
        sent > 0
            ? (sent > lease.length ? lease.length
                                   : static_cast<uint16_t>(sent))
            : 0;
    budget.noteAttempt(progressed_bytes);
    result.writes_attempted = budget.writesAttempted();
    result.bytes_progressed = budget.bytesProgressed();

    const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
    if (disconnect_sequence != handled_disconnect_sequence_) {
      handled_disconnect_sequence_ = disconnect_sequence;
      state_.counters.late_send_result_total++;
      closeClient(WifiCloseReason::IsolationRequest);
      applyAbortRequest();
      notePumpResult(result);
      return result;
    }

    if (sent < 0 && sent != NSAPI_ERROR_WOULD_BLOCK) {
      noteSocketError(static_cast<nsapi_error_t>(sent));
      closeClient(WifiCloseReason::SocketError);
      notePumpResult(result);
      return result;
    }
    if (sent <= 0) {
      if (sent == NSAPI_ERROR_WOULD_BLOCK) {
        state_.counters.would_block_total++;
        result.would_block = true;
      } else {
        state_.counters.zero_write_total++;
        result.zero_write = true;
      }
      const WifiTxProgressObservation progress =
          tx_progress_.observe(now_ms, false, config_.stall_timeout_ms);
      if (progress.started) state_.counters.backpressure_total++;
      state_.backpressure_active = true;
      state_.backpressure_duration_ms = progress.duration_ms;
      result.no_progress_duration_ms = progress.duration_ms;
      if (state_.backpressure_duration_ms >
          state_.counters.backpressure_max_duration_ms) {
        state_.counters.backpressure_max_duration_ms =
            state_.backpressure_duration_ms;
      }
      if (progress.close_no_progress) {
        closeClient(WifiCloseReason::TransmitNoProgress);
      }
      notePumpResult(result);
      return result;
    }

    pending_lease_ = lease;
    pending_consumed_bytes_ = progressed_bytes;
    pending_consume_ = true;
    state_.counters.positive_write_total++;
    if (pending_consumed_bytes_ < lease.length) {
      state_.counters.partial_write_total++;
    }
    if (!applyPendingConsume()) return result;

    const WifiTxProgressObservation progress =
        tx_progress_.observe(now_ms, true, config_.stall_timeout_ms);
    if (progress.recovered) {
      if (progress.duration_ms > state_.counters.backpressure_max_duration_ms) {
        state_.counters.backpressure_max_duration_ms = progress.duration_ms;
      }
      state_.backpressure_active = false;
      state_.backpressure_duration_ms = 0;
    }
  }
  notePumpResult(result);
  return result;
}

void WifiSocketWorker::notePumpResult(
    const WifiTransmitPumpResult& result) {
  if (result.bytes_progressed > state_.counters.bytes_per_wake_max) {
    state_.counters.bytes_per_wake_max = result.bytes_progressed;
  }
  if (result.writes_attempted > state_.counters.writes_per_wake_max) {
    state_.counters.writes_per_wake_max = result.writes_attempted;
  }
}

bool WifiSocketWorker::applyPendingConsume() {
  if (!pending_consume_) return true;
  WifiWorkerMailbox::TxConsumeResult consumed;
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

void WifiSocketWorker::closeClient(WifiCloseReason reason) {
  const bool was_connected = client_ != nullptr || state_.connected;
  const uint32_t no_progress_duration_ms = state_.backpressure_duration_ms;
  TCPSocket* closing = client_;
  client_ = nullptr;
  state_.connected = false;
  state_.last_close_reason = reason;
  tx_progress_.reset();
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
  if (was_connected) {
    state_.counters.connection_epoch++;
    state_.counters.disconnect_total++;
  }
  if (was_connected && reason == WifiCloseReason::TransmitNoProgress) {
    state_.counters.stall_close_total++;
    state_.stall_event_duration_ms = no_progress_duration_ms;
    state_.stall_event_sequence++;
  }
  if (was_connected && reason == WifiCloseReason::QueuePressure) {
    state_.counters.queue_pressure_close_total++;
  }
  // Publish disconnected state before entering a potentially slow vendor
  // close. The facade must not misclassify the close itself as a new stall.
  publishState(millis(), true);
  closeSocket(closing, WifiWorkerCallPhase::CloseClient);
}

void WifiSocketWorker::closeSocket(TCPSocket*& socket,
                                   WifiWorkerCallPhase close_phase) {
  TCPSocket* owned = socket;
  socket = nullptr;
  if (owned == nullptr) return;
  owned->sigio(nullptr);
  beginCall(close_phase);
  // Mbed TCPSocket::accept() returns a factory-allocated socket. Its close()
  // deallocates that object; touching or deleting owned afterwards is UB.
  const nsapi_error_t error = owned->close();
  const uint32_t duration_us = endCall(error);
  if (duration_us > state_.counters.close_call_max_us) {
    state_.counters.close_call_max_us = duration_us;
  }
  if (error != NSAPI_ERROR_OK) noteSocketError(error);
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

void WifiSocketWorker::publishState(uint32_t now_ms, bool force) {
  if (!force && last_state_publish_ms_ != 0 &&
      static_cast<uint32_t>(now_ms - last_state_publish_ms_) <
          BOARD_WIFI_STATE_PUBLISH_PERIOD_MS) {
    return;
  }
  last_state_publish_ms_ = now_ms == 0 ? 1 : now_ms;
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
