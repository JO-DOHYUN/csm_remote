#include "board/uplink/WifiSocketWorker.h"

#if BOARD_ENABLE_WIFI_UPLINK

#include <Arduino.h>
#include <new>
#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

namespace {
// The product WHD interface is backed by lwIP. These are the stable POSIX/lwIP
// IPPROTO_TCP and TCP_NODELAY values accepted as stack-specific Mbed options.
constexpr int kTcpProtocolLevel = 6;
constexpr int kTcpNoDelayOption = 1;
}  // namespace

WifiSocketWorker::WifiSocketWorker(
    WifiWorkerMailbox& mailbox, WifiControlPlaneMailbox& control_mailbox,
    WifiRealtimeMailbox& realtime_mailbox)
    : mailbox_(mailbox), control_mailbox_(control_mailbox),
      realtime_mailbox_(realtime_mailbox),
      realtime_worker_(realtime_mailbox) {}

bool WifiSocketWorker::start(const WifiTcpSinkConfig& config) {
  if (thread_started_) {
    return false;
  }
  config_ = config;
  mailbox_.configureSession(config_.boot_session_id);
  last_admission_snapshot_ = {};
  (void)mailbox_.tryReadAdmissionSnapshot(last_admission_snapshot_);
  state_ = {};
  state_.runtime_mode = config_.runtime_mode;
  state_.tcp_enabled = wifiRuntimeModeEnablesTcp(config_.runtime_mode);
  state_.counters.worker_start_total = 1;
  startup_complete_ = false;
  next_startup_attempt_ms_ = 0;
  if (!wifiRuntimeModeStartsWorker(config_.runtime_mode) ||
      config_.startup_retry_ms == 0 || config_.ap_ssid == nullptr ||
      config_.ap_passphrase == nullptr ||
      (state_.tcp_enabled &&
       (config_.port == 0 || config_.control_port == 0 ||
        config_.realtime_port == 0 || config_.port == config_.control_port ||
        config_.port == config_.realtime_port ||
        config_.control_port == config_.realtime_port))) {
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
        if (!startup_complete_ && !state_.startup_attempts_exhausted) {
          if (wifiStartupAttemptsExhausted(
                  state_.counters.startup_attempt_total,
                  config_.startup_attempt_limit)) {
            state_.startup_attempts_exhausted = true;
            state_.counters.startup_exhausted_total++;
          } else {
            next_startup_attempt_ms_ =
                millis() + config_.startup_retry_ms;
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
    serviceControlRequests();
    if (mailbox_.abortRequestSequence() != handled_abort_sequence_) {
      // A producer was still committing while the prior epoch closed. Do not
      // accept a new client until that epoch's unsent queue is fully discarded.
      publishState(now_ms);
      continue;
    }
    // Host causal ACKs are serviced before the bulk evidence socket. Both are
    // nonblocking and bounded, but telemetry backpressure never gets first
    // claim on a worker turn.
    if (static_cast<uint32_t>(now_ms - last_control_accept_poll_ms_) >=
        BOARD_WIFI_ACCEPT_POLL_MS) {
      last_control_accept_poll_ms_ = now_ms;
      // Polling accept returns a bounded, concrete candidate. A wake alone is
      // not replacement evidence and cannot close the current control epoch.
      serviceControlAccept(now_ms);
    }
    if (control_client_ != nullptr) {
      serviceControlClient(now_ms);
    }
    if (static_cast<uint32_t>(now_ms - last_accept_poll_ms_) >=
        BOARD_WIFI_ACCEPT_POLL_MS) {
      last_accept_poll_ms_ = now_ms;
      // A half-open client is replaced only after accept() hands this worker
      // a new candidate; generic SIGIO never owns epoch closure.
      serviceAccept(now_ms);
    }
    if (client_ != nullptr) {
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
  if (client_ == nullptr || control_client_ == nullptr) {
    const uint32_t telemetry_elapsed = now_ms - last_accept_poll_ms_;
    const uint32_t control_elapsed = now_ms - last_control_accept_poll_ms_;
    const uint32_t elapsed = client_ == nullptr && control_client_ == nullptr
                                 ? (telemetry_elapsed < control_elapsed
                                        ? telemetry_elapsed
                                        : control_elapsed)
                                 : (client_ == nullptr ? telemetry_elapsed
                                                       : control_elapsed);
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
  if (!rollbackNetwork()) {
    quarantineStartupFailure(state_.last_network_error);
    return false;
  }
  state_.counters.startup_attempt_total++;
  state_.ap_ready = false;
  state_.server_ready = false;
  state_.network_ready = false;
  state_.counters.ap_start_total++;

  ap_interface_ = WhdSoftAPInterface::get_default_instance();
  if (ap_interface_ == nullptr) {
    state_.counters.ap_start_fail_total++;
    state_.last_network_error = NSAPI_ERROR_NO_CONNECTION;
    return false;
  }

  const uint8_t netmask_bytes[4] = {255, 255, 255, 0};
  const SocketAddress ip_address(config_.ip, NSAPI_IPv4);
  const SocketAddress netmask(netmask_bytes, NSAPI_IPv4);
  const SocketAddress gateway(config_.ip, NSAPI_IPv4);
  beginCall(WifiWorkerCallPhase::ConfigureIp);
  const nsapi_error_t configure_result =
      ap_interface_->set_network(ip_address, netmask, gateway);
  endCall(configure_result);
  if (configure_result != NSAPI_ERROR_OK) {
    state_.counters.ap_start_fail_total++;
    state_.last_network_error = configure_result;
    return false;
  }

  beginCall(WifiWorkerCallPhase::BeginAccessPoint);
  const nsapi_error_t ap_result = ap_interface_->start(
      config_.ap_ssid, config_.ap_passphrase, NSAPI_SECURITY_WPA2,
      config_.channel, true, nullptr, config_.ap_sta_concur);
  endCall(ap_result);
  if (ap_result != NSAPI_ERROR_OK) {
    state_.counters.ap_start_fail_total++;
    // start() is an opaque multi-stage framework transaction. A failed call
    // cannot be safely paired with stop() at every internal partial state, so
    // repeated attempts would accumulate radio/DHCP resources. Quarantine this
    // boot; a transactional framework implementation may later relax it.
    if (!wifiStartupRetryAllowed(
            WifiStartupFailureBoundary::OpaqueApStart, false)) {
      quarantineStartupFailure(ap_result);
    }
    return false;
  }
  ap_started_ = true;
  state_.ap_ready = true;
  state_.network_ready = true;
  state_.last_network_error = 0;

  if (!wifiRuntimeModeEnablesTcp(config_.runtime_mode)) return true;

  state_.counters.server_start_total++;
  beginCall(WifiWorkerCallPhase::OpenTelemetryServer);
  nsapi_error_t server_result = server_.open(ap_interface_);
  endCall(server_result);
  if (server_result == NSAPI_ERROR_OK) {
    server_opened_ = true;
    int reuse_address = 1;
    beginCall(WifiWorkerCallPhase::ConfigureTelemetryServer);
    server_result =
        server_.setsockopt(NSAPI_SOCKET, NSAPI_REUSEADDR, &reuse_address,
                           sizeof(reuse_address));
    endCall(server_result);
  }
  if (server_result == NSAPI_ERROR_OK) {
    beginCall(WifiWorkerCallPhase::BindTelemetryServer);
    server_result = server_.bind(config_.port);
    endCall(server_result);
  }
  if (server_result == NSAPI_ERROR_OK) {
    beginCall(WifiWorkerCallPhase::ListenTelemetryServer);
    server_result = server_.listen(1);
    endCall(server_result);
  }
  if (server_result == NSAPI_ERROR_OK) {
    server_.set_blocking(false);
    server_.sigio(
        mbed::callback(this, &WifiSocketWorker::onSocketStateChanged));
  }
  if (server_result != NSAPI_ERROR_OK) {
    state_.counters.server_start_fail_total++;
    state_.last_network_error = server_result;
    const bool cleanup_confirmed = rollbackNetwork();
    if (!wifiStartupRetryAllowed(
            WifiStartupFailureBoundary::AfterApStarted,
            cleanup_confirmed)) {
      quarantineStartupFailure(state_.last_network_error);
    } else {
      state_.last_network_error = server_result;
    }
    return false;
  }

  state_.server_ready = true;

  beginCall(WifiWorkerCallPhase::OpenControlServer);
  nsapi_error_t control_server_result = control_server_.open(ap_interface_);
  endCall(control_server_result);
  if (control_server_result == NSAPI_ERROR_OK) {
    control_server_opened_ = true;
    int reuse_address = 1;
    beginCall(WifiWorkerCallPhase::ConfigureControlServer);
    control_server_result = control_server_.setsockopt(
        NSAPI_SOCKET, NSAPI_REUSEADDR, &reuse_address, sizeof(reuse_address));
    endCall(control_server_result);
  }
  if (control_server_result == NSAPI_ERROR_OK) {
    beginCall(WifiWorkerCallPhase::BindControlServer);
    control_server_result = control_server_.bind(config_.control_port);
    endCall(control_server_result);
  }
  if (control_server_result == NSAPI_ERROR_OK) {
    beginCall(WifiWorkerCallPhase::ListenControlServer);
    control_server_result = control_server_.listen(1);
    endCall(control_server_result);
  }
  if (control_server_result == NSAPI_ERROR_OK) {
    control_server_.set_blocking(false);
    control_server_.sigio(
        mbed::callback(this, &WifiSocketWorker::onSocketStateChanged));
  }
  if (control_server_result != NSAPI_ERROR_OK) {
    state_.counters.server_start_fail_total++;
    state_.last_network_error = control_server_result;
    const bool cleanup_confirmed = rollbackNetwork();
    if (!wifiStartupRetryAllowed(
            WifiStartupFailureBoundary::AfterApStarted,
            cleanup_confirmed)) {
      quarantineStartupFailure(state_.last_network_error);
    } else {
      state_.last_network_error = control_server_result;
    }
    return false;
  }

  ++network_epoch_;
  if (network_epoch_ == 0u) ++network_epoch_;
  if (!realtime_worker_.start(ap_interface_, config_.realtime_port,
                              network_epoch_)) {
    state_.counters.server_start_fail_total++;
    state_.last_network_error = NSAPI_ERROR_NO_MEMORY;
    const bool cleanup_confirmed = rollbackNetwork();
    if (!wifiStartupRetryAllowed(
            WifiStartupFailureBoundary::AfterApStarted,
            cleanup_confirmed)) {
      quarantineStartupFailure(state_.last_network_error);
    }
    return false;
  }

  state_.last_network_error = 0;
  return true;
}

bool WifiSocketWorker::rollbackNetwork() {
  bool cleanup_confirmed = true;
  closeControlClient();
  if (control_server_opened_) {
    control_server_.sigio(nullptr);
    beginCall(WifiWorkerCallPhase::StopControlServer);
    const nsapi_error_t result = control_server_.close();
    endCall(result);
    if (result == NSAPI_ERROR_OK || result == NSAPI_ERROR_NO_SOCKET) {
      control_server_opened_ = false;
    } else {
      noteSocketError(result);
      cleanup_confirmed = false;
    }
  }
  if (server_opened_) {
    server_.sigio(nullptr);
    beginCall(WifiWorkerCallPhase::StopServer);
    const nsapi_error_t result = server_.close();
    endCall(result);
    if (result == NSAPI_ERROR_OK || result == NSAPI_ERROR_NO_SOCKET) {
      server_opened_ = false;
    } else {
      noteSocketError(result);
      cleanup_confirmed = false;
    }
  }
  // Do not tear down the parent AP when its child server could not be closed.
  // The worker is quarantined instead of retrying into an unknown ownership
  // state.
  if (ap_started_ && !server_opened_ && !control_server_opened_ &&
      ap_interface_ != nullptr) {
    beginCall(WifiWorkerCallPhase::StopAccessPoint);
    const nsapi_error_t result = ap_interface_->stop();
    endCall(result);
    if (result == NSAPI_ERROR_OK) {
      ap_started_ = false;
    } else {
      noteSocketError(result);
      cleanup_confirmed = false;
    }
  } else if (ap_started_ && ap_interface_ == nullptr) {
    cleanup_confirmed = false;
  }
  state_.ap_ready = ap_started_;
  state_.server_ready = server_opened_;
  state_.network_ready = false;
  return cleanup_confirmed && !server_opened_ && !control_server_opened_ &&
         !ap_started_;
}

void WifiSocketWorker::quarantineStartupFailure(nsapi_error_t error) {
  state_.last_network_error = error;
  if (!state_.startup_attempts_exhausted) {
    state_.startup_attempts_exhausted = true;
    state_.counters.startup_exhausted_total++;
  }
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
  updateQueuePressure(now_ms);
  if (pending_consume_ && !applyPendingConsume()) return;
  // Downlink is sampled before each nonblocking TX pump so Service/HIL control
  // cannot be starved by a saturated telemetry stream.
  serviceReceive(now_ms);
  if (client_ == nullptr) return;
  const WifiTransmitPumpResult anchor = serviceSessionAnchor(millis());
  if (client_ == nullptr || session_anchor_required_) {
    updateQueuePressure(millis());
    if (client_ != nullptr && anchor.progressed()) signalWake(WifiWakeTxData);
    return;
  }
  const WifiTransmitPumpResult transmitted = serviceTransmit(millis());
  updateQueuePressure(millis());
  if (client_ != nullptr && transmitted.progressed() &&
      !transmitted.would_block && !transmitted.zero_write &&
      mailbox_.queueSnapshot().unsent_records != 0) {
    // Yield after each bounded pump, then resume without waiting for the
    // fallback timer while the socket continues to make positive progress.
    signalWake(WifiWakeTxData);
  }
}

void WifiSocketWorker::serviceAccept(uint32_t) {
  if (mailbox_.queuePressureDisconnectLatched()) return;
  nsapi_error_t error = NSAPI_ERROR_WOULD_BLOCK;
  beginCall(WifiWorkerCallPhase::AcceptClient);
  TCPSocket* candidate = server_.accept(&error);
  endCall(error);
  if (candidate == nullptr) {
    if (error != NSAPI_ERROR_WOULD_BLOCK) noteSocketError(error);
    return;
  }

  if (client_ != nullptr) {
    closeClient(WifiCloseReason::AcceptedReplacement);
    applyAbortRequest();
    if (mailbox_.abortRequestSequence() != handled_abort_sequence_) {
      closeSocket(candidate, WifiWorkerCallPhase::CloseClient);
      return;
    }
  }

  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (disconnect_sequence != handled_disconnect_sequence_) {
    handled_disconnect_sequence_ = disconnect_sequence;
    closeSocket(candidate, WifiWorkerCallPhase::CloseClient);
    return;
  }

  beginCall(WifiWorkerCallPhase::ConfigureClient);
  candidate->set_blocking(false);
  int no_delay = 1;
  const nsapi_error_t configure_result = candidate->setsockopt(
      kTcpProtocolLevel, kTcpNoDelayOption, &no_delay, sizeof(no_delay));
  if (configure_result == NSAPI_ERROR_OK) {
    candidate->sigio(
        mbed::callback(this, &WifiSocketWorker::onSocketStateChanged));
  }
  endCall(configure_result);
  if (configure_result != NSAPI_ERROR_OK) {
    noteSocketError(configure_result);
    closeSocket(candidate, WifiWorkerCallPhase::CloseClient);
    return;
  }

  // RX reset has a worker-owned odd/even generation. Clear the prior socket
  // epoch before producer admission or facade downlink can observe this one.
  mailbox_.resetRxForNewEpoch();
  mailbox_.activateLiveSession();
  client_ = candidate;
  state_.connected = true;
  session_anchor_ = {};
  session_anchor_offset_ = 0;
  session_anchor_accounted_offset_ = 0;
  session_anchor_required_ = true;
  session_anchor_loaded_ = false;
  tx_progress_.reset();
  state_.backpressure_active = false;
  state_.backpressure_duration_ms = 0;
  state_.counters.connection_epoch++;
  state_.counters.connect_total++;
  publishState(millis(), true);
  signalWake(WifiWakeTxData);
}

WifiTransmitPumpResult WifiSocketWorker::serviceSessionAnchor(uint32_t now_ms) {
  WifiTransmitPumpResult result;
  if (!session_anchor_required_ || client_ == nullptr) return result;
  if (!session_anchor_loaded_) {
    if (!mailbox_.tryReadSessionAnchor(session_anchor_)) return result;
    session_anchor_loaded_ = true;
  }
  if (session_anchor_offset_ >= session_anchor_.length) {
    session_anchor_required_ = false;
    return result;
  }

  const uint16_t remaining =
      static_cast<uint16_t>(session_anchor_.length - session_anchor_offset_);
  if (closePendingIsolationBeforeSocketSend()) return result;
  if (!refreshAdmissionSnapshotForSettlement()) return result;
  state_.counters.write_attempt_total++;
  state_.counters.send_request_bytes_total += remaining;
  beginCall(WifiWorkerCallPhase::Send);
  const nsapi_size_or_error_t sent =
      client_->send(session_anchor_.bytes + session_anchor_offset_, remaining);
  const uint32_t duration_us = endCall(sent);
  const uint32_t completed_ms = millis();
  result.writes_attempted = 1;
  if (duration_us > state_.counters.send_call_max_us) {
    state_.counters.send_call_max_us = duration_us;
  }
  if (config_.drain_time_budget_us > 0 &&
      duration_us > config_.drain_time_budget_us) {
    state_.counters.send_budget_overrun_total++;
  }

  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  const bool disconnect_pending =
      disconnect_sequence != handled_disconnect_sequence_;
  if (wifiSendResultHasPositiveProgress(sent)) {
    const uint16_t progressed =
        sent > remaining ? remaining : static_cast<uint16_t>(sent);
    const uint16_t prior_anchor_offset = session_anchor_offset_;
    session_anchor_offset_ =
        static_cast<uint16_t>(session_anchor_offset_ + progressed);
    session_anchor_accounted_offset_ = session_anchor_offset_;
    result.bytes_progressed = progressed;
    state_.counters.positive_write_total++;
    state_.counters.bytes_sent_total += progressed;
    state_.counters.socket_sent_bytes_total += progressed;
    if (progressed < remaining) state_.counters.partial_write_total++;
    const WifiTxProgressObservation progress =
        tx_progress_.observe(completed_ms, true, config_.stall_timeout_ms);
    if (progress.recovered) {
      state_.backpressure_active = false;
      state_.backpressure_duration_ms = 0;
    }
    if (wifiSessionAnchorCompleted(prior_anchor_offset, progressed,
                                   session_anchor_.length)) {
      session_anchor_required_ = false;
      state_.counters.fresh_anchor_sent_total++;
      state_.counters.frame_sent_total++;
      state_.counters.last_sent_publish_seq = session_anchor_.publish_seq;
    }
    closePendingIsolationAfterPositiveSend();
    notePumpResult(result);
    (void)now_ms;
    return result;
  }
  if (disconnect_pending) {
    handled_disconnect_sequence_ = disconnect_sequence;
    state_.counters.late_send_result_total++;
    closeClient(WifiCloseReason::IsolationRequest);
    return result;
  }
  if (sent < 0 && sent != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(sent));
    closeClient(WifiCloseReason::SocketError);
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
        tx_progress_.observe(completed_ms, false, config_.stall_timeout_ms);
    if (progress.started) state_.counters.backpressure_total++;
    state_.backpressure_active = true;
    state_.backpressure_duration_ms = progress.duration_ms;
    result.no_progress_duration_ms = progress.duration_ms;
    if (progress.close_no_progress) {
      closeClient(WifiCloseReason::TransmitNoProgress);
    }
    return result;
  }
  return result;
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

void WifiSocketWorker::serviceControlRequests() {
  const uint32_t requested =
      control_mailbox_.disconnectRequestSequence();
  if (requested != handled_control_disconnect_sequence_) {
    handled_control_disconnect_sequence_ = requested;
    closeControlClient();
  }
}

void WifiSocketWorker::serviceControlAccept(uint32_t) {
  nsapi_error_t error = NSAPI_ERROR_WOULD_BLOCK;
  beginCall(WifiWorkerCallPhase::AcceptControlClient);
  TCPSocket* candidate = control_server_.accept(&error);
  endCall(error);
  if (candidate == nullptr) {
    if (error != NSAPI_ERROR_WOULD_BLOCK) noteSocketError(error);
    return;
  }
  beginCall(WifiWorkerCallPhase::ConfigureControlClient);
  candidate->set_blocking(false);
  int no_delay = 1;
  const nsapi_error_t configured = candidate->setsockopt(
      kTcpProtocolLevel, kTcpNoDelayOption, &no_delay, sizeof(no_delay));
  if (configured == NSAPI_ERROR_OK) {
    candidate->sigio(
        mbed::callback(this, &WifiSocketWorker::onSocketStateChanged));
  }
  endCall(configured);
  if (configured != NSAPI_ERROR_OK ||
      (control_client_ == nullptr &&
       !control_mailbox_.activate(static_cast<uint64_t>(micros())))) {
    if (configured != NSAPI_ERROR_OK) noteSocketError(configured);
    closeSocket(candidate, WifiWorkerCallPhase::CloseControlClient);
    return;
  }
  if (control_client_ != nullptr) {
    closeControlClient(7u, 0);
    if (!control_mailbox_.activate(static_cast<uint64_t>(micros()))) {
      closeSocket(candidate, WifiWorkerCallPhase::CloseControlClient);
      return;
    }
  }
  control_client_ = candidate;
  control_anchor_length_ = 0;
  control_anchor_offset_ = 0;
  control_anchor_pending_ = true;
  control_tx_length_ = 0;
  control_tx_offset_ = 0;
  control_tx_progress_.reset();
  signalWake(WifiWakeTxData);
}

void WifiSocketWorker::serviceControlClient(uint32_t now_ms) {
  serviceControlReceive();
  if (control_client_ == nullptr) return;
  serviceControlTransmit(now_ms);
}

void WifiSocketWorker::serviceControlReceive() {
  beginCall(WifiWorkerCallPhase::ReceiveControl);
  const nsapi_size_or_error_t received =
      control_client_->recv(control_rx_buffer_, sizeof(control_rx_buffer_));
  endCall(received);
  control_mailbox_.noteReceive(received, millis());
  if (received > 0) {
    if (!control_mailbox_.pushRx(control_rx_buffer_,
                                 static_cast<uint16_t>(received))) {
      closeControlClient(3u, received);
    }
    return;
  }
  if (received == 0) {
    closeControlClient(1u, received);
  } else if (received != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(received));
    closeControlClient(2u, received);
  }
}

void WifiSocketWorker::serviceControlTransmit(uint32_t now_ms) {
  if (control_anchor_pending_ && control_anchor_length_ == 0) {
    if (!control_mailbox_.copyAnchor(
            control_tx_buffer_, sizeof(control_tx_buffer_),
            control_anchor_length_)) {
      closeControlClient();
      return;
    }
  }
  if (!control_anchor_pending_ && control_tx_length_ == 0) {
    if (!control_mailbox_.peekTx(control_tx_buffer_, sizeof(control_tx_buffer_),
                                 control_tx_length_)) {
      return;
    }
    control_tx_offset_ = 0;
  }
  const uint16_t length =
      control_anchor_pending_ ? control_anchor_length_ : control_tx_length_;
  uint16_t& offset =
      control_anchor_pending_ ? control_anchor_offset_ : control_tx_offset_;
  if (length == 0 || offset >= length) return;

  beginCall(WifiWorkerCallPhase::SendControl);
  const nsapi_size_or_error_t sent =
      control_client_->send(control_tx_buffer_ + offset, length - offset);
  const uint32_t duration_us = endCall(sent);
  const uint32_t completed_ms = millis();
  // Socket acceptance is evidence of ACK TX only, never remote receipt.
  const uint32_t ack_id = control_anchor_pending_ ? 0u :
      csm::rd_u32_le(control_tx_buffer_ + 9u + csm::kControlAckCommandIdOffset);
  control_mailbox_.noteSend(sent, completed_ms, duration_us, ack_id,
      static_cast<uint16_t>(offset + (sent > 0 ? sent : 0)),
      sent > 0 && static_cast<uint32_t>(offset) + sent >= length,
      sent == NSAPI_ERROR_WOULD_BLOCK);
  if (sent > 0) {
    const uint16_t remaining = static_cast<uint16_t>(length - offset);
    const uint16_t progressed =
        sent > remaining ? remaining : static_cast<uint16_t>(sent);
    offset = static_cast<uint16_t>(offset + progressed);
    control_tx_progress_.observe(completed_ms, true, 300);
    if (offset == length) {
      if (control_anchor_pending_) {
        control_anchor_pending_ = false;
        control_anchor_length_ = 0;
        control_anchor_offset_ = 0;
      } else {
        control_mailbox_.consumeTx();
        control_tx_length_ = 0;
        control_tx_offset_ = 0;
      }
      if (control_mailbox_.queuedRecords() != 0) signalWake(WifiWakeTxData);
    }
    return;
  }
  if (sent < 0 && sent != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(sent));
    closeControlClient(4u, sent);
    return;
  }
  const WifiTxProgressObservation progress =
      control_tx_progress_.observe(completed_ms, false, 300);
  if (progress.close_no_progress ||
      static_cast<uint32_t>(completed_ms - now_ms) > 300u) {
    closeControlClient(6u, sent);
  }
}

WifiTransmitPumpResult WifiSocketWorker::serviceTransmit(uint32_t now_ms) {
  WifiTransmitPumpResult result;
  const WifiMailboxQueueSnapshot initial_queue = mailbox_.queueSnapshot();
  if (initial_queue.unsent_records == 0) return result;
  const bool initial_pressure = wifiQueuePressureReached(
      initial_queue.queued_bytes, initial_queue.queued_records,
      config_.pressure_high_water_bytes,
      config_.pressure_high_water_records);
  const uint32_t latency_limit_ms =
      initial_queue.latency_bounded ? config_.latency_bound_max_ms
                                    : config_.batch_max_latency_ms;
  if (!initial_pressure && config_.batch_target_bytes > 0 &&
      initial_queue.unsent_bytes < config_.batch_target_bytes &&
      latency_limit_ms > 0 &&
      static_cast<uint32_t>(now_ms - initial_queue.first_queued_ms) <
          latency_limit_ms) {
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
    if (closePendingIsolationBeforeSocketSend()) {
      notePumpResult(result);
      return result;
    }
    if (!refreshAdmissionSnapshotForSettlement()) {
      notePumpResult(result);
      return result;
    }

    state_.counters.write_attempt_total++;
    state_.counters.send_request_bytes_total += lease.length;
    beginCall(WifiWorkerCallPhase::Send);
    const nsapi_size_or_error_t sent = client_->send(tx_buffer_, lease.length);
    const uint32_t duration_us = endCall(sent);
    const uint32_t send_completed_ms = millis();
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
    const bool disconnect_pending =
        disconnect_sequence != handled_disconnect_sequence_;
    if (wifiSendResultHasPositiveProgress(sent)) {
      pending_lease_ = lease;
      pending_consumed_bytes_ = progressed_bytes;
      pending_consume_ = true;
      state_.counters.positive_write_total++;
      if (pending_consumed_bytes_ < lease.length) {
        state_.counters.partial_write_total++;
      }
      if (!applyPendingConsume()) return result;

      const WifiTxProgressObservation progress =
          tx_progress_.observe(send_completed_ms, true,
                               config_.stall_timeout_ms);
      if (progress.recovered) {
        if (progress.duration_ms >
            state_.counters.backpressure_max_duration_ms) {
          state_.counters.backpressure_max_duration_ms = progress.duration_ms;
        }
        state_.backpressure_active = false;
        state_.backpressure_duration_ms = 0;
      }
      if (closePendingIsolationAfterPositiveSend()) {
        notePumpResult(result);
        return result;
      }
      continue;
    }
    if (disconnect_pending) {
      handled_disconnect_sequence_ = disconnect_sequence;
      state_.counters.late_send_result_total++;
      closeClient(WifiCloseReason::IsolationRequest);
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
          tx_progress_.observe(send_completed_ms, false,
                               config_.stall_timeout_ms);
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

void WifiSocketWorker::updateQueuePressure(uint32_t now_ms) {
  const WifiMailboxQueueSnapshot queued = mailbox_.queueSnapshot();
  const WifiQueuePressureObservation pressure = queue_pressure_.observe(
      now_ms, queued.queued_bytes, queued.queued_records,
      config_.pressure_high_water_bytes,
      config_.pressure_high_water_records,
      config_.pressure_low_water_bytes,
      config_.pressure_low_water_records);
  if (pressure.entered) state_.counters.pressure_enter_total++;
  if (pressure.recovered) state_.counters.pressure_recover_total++;
  state_.pressure_active = pressure.active;
  state_.pressure_duration_ms = pressure.duration_ms;
  if (pressure.duration_ms > state_.counters.pressure_max_duration_ms) {
    state_.counters.pressure_max_duration_ms = pressure.duration_ms;
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
  state_.counters.socket_sent_bytes_total += pending_consumed_bytes_;
  state_.counters.frame_sent_total += consumed.frames;
  if (consumed.frames > 0) {
    state_.counters.last_sent_publish_seq = consumed.last_publish_seq;
  }
  return true;
}

bool WifiSocketWorker::closePendingIsolationBeforeSocketSend() {
  const bool queue_pressure_latched =
      mailbox_.queuePressureDisconnectLatched();
  const uint32_t queue_pressure_sequence =
      mailbox_.queuePressureDisconnectRequestSequence();
  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (wifiSocketSendPermitted(
          queue_pressure_latched, queue_pressure_sequence,
          handled_queue_pressure_disconnect_sequence_, disconnect_sequence,
          handled_disconnect_sequence_)) {
    return false;
  }
  switch (wifiPostSendIsolationDecision(
      queue_pressure_sequence, handled_queue_pressure_disconnect_sequence_,
      disconnect_sequence, handled_disconnect_sequence_)) {
    case WifiPostSendIsolation::QueuePressure:
      handled_queue_pressure_disconnect_sequence_ = queue_pressure_sequence;
      handled_disconnect_sequence_ = disconnect_sequence;
      closeClient(WifiCloseReason::QueuePressure);
      mailbox_.markQueuePressureDisconnectHandled(queue_pressure_sequence);
      return true;
    case WifiPostSendIsolation::IsolationRequest:
      handled_disconnect_sequence_ = disconnect_sequence;
      closeClient(WifiCloseReason::IsolationRequest);
      return true;
    case WifiPostSendIsolation::None:
      // The producer publishes the latch before advancing its request
      // sequence. Treat that short transition as a closed send gate; the
      // control wake will complete the close once the sequence is visible.
      return true;
  }
  return true;
}

bool WifiSocketWorker::closePendingIsolationAfterPositiveSend() {
  const bool queue_pressure_latched =
      mailbox_.queuePressureDisconnectLatched();
  const uint32_t queue_pressure_sequence =
      mailbox_.queuePressureDisconnectRequestSequence();
  const uint32_t disconnect_sequence = mailbox_.disconnectRequestSequence();
  if (wifiSocketSendPermitted(
          queue_pressure_latched, queue_pressure_sequence,
          handled_queue_pressure_disconnect_sequence_, disconnect_sequence,
          handled_disconnect_sequence_)) {
    return false;
  }
  switch (wifiPostSendIsolationDecision(
      queue_pressure_sequence, handled_queue_pressure_disconnect_sequence_,
      disconnect_sequence, handled_disconnect_sequence_)) {
    case WifiPostSendIsolation::QueuePressure:
      handled_queue_pressure_disconnect_sequence_ = queue_pressure_sequence;
      handled_disconnect_sequence_ = disconnect_sequence;
      state_.counters.late_send_result_total++;
      closeClient(WifiCloseReason::QueuePressure);
      // Do not release the producer-side pressure latch until the closed state
      // has been published by closeClient().
      mailbox_.markQueuePressureDisconnectHandled(queue_pressure_sequence);
      return true;
    case WifiPostSendIsolation::IsolationRequest:
      handled_disconnect_sequence_ = disconnect_sequence;
      state_.counters.late_send_result_total++;
      closeClient(WifiCloseReason::IsolationRequest);
      return true;
    case WifiPostSendIsolation::None:
      // The request sequence is published immediately after the latch.
      // Return to the worker loop without issuing another socket operation.
      return true;
  }
  return false;
}

bool WifiSocketWorker::refreshAdmissionSnapshotForSettlement() {
  WifiMailboxAdmissionSnapshot admission;
  if (!mailbox_.tryReadAdmissionSnapshot(admission)) {
    // Never spin while a same-core producer owns the odd revision. Its release
    // schedules a prompt reconciliation; this pump performs no settlement.
    mailbox_.requestAdmissionReconcile();
    return false;
  }
  last_admission_snapshot_ = admission;
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
  queue_pressure_.reset();
  state_.backpressure_active = false;
  state_.backpressure_duration_ms = 0;
  state_.pressure_active = false;
  state_.pressure_duration_ms = 0;
  pending_consume_ = false;
  session_anchor_ = {};
  session_anchor_offset_ = 0;
  session_anchor_required_ = false;
  session_anchor_loaded_ = false;
  mailbox_.deactivateLiveSession();
  mailbox_.invalidateRxEpoch();
  // Unsent telemetry belongs only to the closed live epoch. It is discarded
  // asynchronously through the same producer/worker abort gate and is never
  // replayed into a later connection.
  mailbox_.requestAbort();
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

void WifiSocketWorker::closeControlClient(uint32_t reason, int32_t result) {
  TCPSocket* closing = control_client_;
  if (closing != nullptr) control_mailbox_.noteClose(reason, result, millis());
  control_client_ = nullptr;
  control_mailbox_.deactivate();
  control_anchor_pending_ = false;
  control_anchor_length_ = 0;
  control_anchor_offset_ = 0;
  control_tx_length_ = 0;
  control_tx_offset_ = 0;
  control_tx_progress_.reset();
  closeSocket(closing, WifiWorkerCallPhase::CloseControlClient);
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
  if (!refreshAdmissionSnapshotForSettlement()) return;
  WifiMailboxAbortResult aborted;
  if (!mailbox_.tryApplyAbort(session_anchor_accounted_offset_, aborted)) {
    return;
  }
  handled_abort_sequence_ = requested;
  session_anchor_accounted_offset_ = 0;
  pending_consume_ = false;
  if (aborted.bytes > 0) {
    state_.counters.queue_abort_total++;
    state_.counters.queue_aborted_bytes_total += aborted.bytes;
    state_.counters.queue_aborted_records_total += aborted.records;
    if (aborted.sequence_valid) {
      if (!state_.counters.queue_abort_sequence_valid) {
        state_.counters.first_queue_aborted_publish_seq =
            aborted.first_publish_seq;
        state_.counters.queue_abort_sequence_valid = true;
      }
      state_.counters.last_queue_aborted_publish_seq =
          aborted.last_publish_seq;
    }
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
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  current_call_phase_ = phase;
#endif
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
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  if (failure_latch_.observe(current_call_phase_, result,
                             NSAPI_ERROR_WOULD_BLOCK)) {
    state_.last_failure_phase = failure_latch_.phase();
    state_.last_failure_result = failure_latch_.result();
  }
#endif
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
  WifiMailboxAdmissionSnapshot admission;
  if (mailbox_.tryReadAdmissionSnapshot(admission)) {
    last_admission_snapshot_ = admission;
  } else {
    if (!force) return;
    // A forced epoch/RX/close transition must never be dropped. Settlement
    // can only advance after refreshAdmissionSnapshotForSettlement() has
    // cached an admission snapshot covering those bytes/records, so the last
    // cache remains a coherent conservation owner while this connection state
    // is published immediately. Producer release requests a fast reconcile.
    admission = last_admission_snapshot_;
    mailbox_.requestAdmissionReconcile();
  }
  last_state_publish_ms_ = now_ms == 0 ? 1 : now_ms;
  sampleStack(now_ms);
  const WifiMailboxQueueSnapshot queue = mailbox_.queueSnapshot();
  state_.accepted_bytes_total = admission.accepted_bytes_total;
  state_.accepted_records_total = admission.accepted_records_total;
  state_.last_accepted_publish_seq =
      admission.last_accepted_publish_seq;
  state_.accepted_sequence_valid = admission.sequence_valid;
  state_.queue_bytes = wifiPendingAcceptedBytes(
      admission.accepted_bytes_total,
      state_.counters.socket_sent_bytes_total,
      state_.counters.queue_aborted_bytes_total);
  state_.queue_high_water_bytes = queue.high_water_bytes;
  state_.queue_records =
      admission.accepted_records_total -
      state_.counters.frame_sent_total -
      state_.counters.queue_aborted_records_total;
  state_.queue_high_water_records = queue.high_water_records;
  WifiMailboxSessionAnchor pending_anchor;
  if (mailbox_.tryReadSessionAnchor(pending_anchor)) {
    const uint16_t sent =
        session_anchor_accounted_offset_ < pending_anchor.length
            ? session_anchor_accounted_offset_
            : pending_anchor.length;
    state_.fresh_anchor_pending_bytes = pending_anchor.length - sent;
    state_.fresh_anchor_pending_records =
        state_.fresh_anchor_pending_bytes == 0 ? 0u : 1u;
  } else {
    state_.fresh_anchor_pending_bytes = 0;
    state_.fresh_anchor_pending_records = 0;
  }
  state_.boot_session_id = config_.boot_session_id;
  state_.live_session_active = mailbox_.liveSessionActive();
  state_.rx_epoch_generation = mailbox_.rxEpochGeneration();
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
