#include "board/uplink/WifiWorkerMailbox.h"

#include <string.h>

namespace csm::board::uplink {

WifiWorkerMailbox::WifiWorkerMailbox(TxStorage& storage) : queue_(storage) {
  downlink_router_.begin(&WifiWorkerMailbox::passthroughThunk, this);
}

void WifiWorkerMailbox::setNotifier(const WifiWorkerNotifier& notifier) {
  notifier_ = notifier;
}

void WifiWorkerMailbox::configureSession(uint64_t boot_session_id) {
  boot_session_id_ = boot_session_id;
  session_anchor_length_.store(0, std::memory_order_release);
}

void WifiWorkerMailbox::activateLiveSession() {
  session_anchor_length_.store(0, std::memory_order_release);
  downlink_router_.reset();
  live_session_generation_.fetch_add(1, std::memory_order_acq_rel);
  live_session_active_.store(true, std::memory_order_release);
}

void WifiWorkerMailbox::deactivateLiveSession() {
  live_session_active_.store(false, std::memory_order_release);
  live_session_generation_.fetch_add(1, std::memory_order_acq_rel);
}

bool WifiWorkerMailbox::liveSessionActive() const {
  return live_session_active_.load(std::memory_order_acquire);
}

WifiMailboxOfferResult WifiWorkerMailbox::tryOffer(
    const PublishedFrameView& frame, uint32_t now_ms) {
  if (frame.bytes == nullptr || frame.length == 0 ||
      frame.length > csm::encoded_typed_frame_len(csm::kMaxPayloadLen)) {
    return WifiMailboxOfferResult::Invalid;
  }
  uint32_t expected_gate = 0;
  if (!producer_abort_gate_.compare_exchange_strong(
          expected_gate, kProducerGateActive, std::memory_order_acq_rel,
          std::memory_order_acquire)) {
    return WifiMailboxOfferResult::Busy;
  }
  if (!liveSessionActive()) {
    releaseProducerGate();
    return WifiMailboxOfferResult::Busy;
  }
  if (session_anchor_length_.load(std::memory_order_acquire) == 0 &&
      frame.type != csm::RecordType::StreamSession) {
    releaseProducerGate();
    return WifiMailboxOfferResult::Busy;
  }
  const uint32_t live_generation =
      live_session_generation_.load(std::memory_order_acquire);
  if (!liveSessionActive()) {
    releaseProducerGate();
    return WifiMailboxOfferResult::Busy;
  }
  // The first session record of each TCP epoch has a dedicated one-frame
  // lane. The worker cannot send queued telemetry until this exact canonical
  // frame has been positively socket-sent.
  if (frame.type == csm::RecordType::StreamSession &&
      session_anchor_length_.load(std::memory_order_acquire) == 0) {
    // Keep length at zero while writing plain storage. The final release-store
    // below is the sole publication point; the worker's acquire-load can
    // therefore copy immutable bytes without racing the producer.
    memcpy(session_anchor_bytes_, frame.bytes, frame.length);
    session_anchor_publish_seq_ = frame.publish_seq;
    const bool same_live_epoch =
        liveSessionActive() &&
        live_session_generation_.load(std::memory_order_acquire) ==
            live_generation &&
        (producer_abort_gate_.load(std::memory_order_acquire) &
         kProducerGateAbort) == 0;
    if (!same_live_epoch) {
      // The close path observes the same producer/abort gate before applying
      // its abort.
      // Length is still zero, so this unpublished copy is neither transmitted
      // nor counted as accepted loss.
      releaseProducerGate();
      return WifiMailboxOfferResult::Busy;
    }
    beginAdmissionTransition();
    noteAccepted(frame);
    endAdmissionTransition();
    session_anchor_length_.store(frame.length, std::memory_order_release);
    releaseProducerGate();
    notifyWorker(WifiWakeTxData);
    return WifiMailboxOfferResult::Accepted;
  }

  const bool critical = frame.priority == UplinkPriority::Critical;
  const bool was_unsent_empty = queue_.count() == 0;
  const bool latency_bounded =
      frame.delivery == UplinkDeliveryClass::LatencyBounded;
  WifiMailboxOfferResult result = WifiMailboxOfferResult::Accepted;
  const bool enters_critical_reserve =
      !critical &&
      (static_cast<uint32_t>(queue_.count()) + 1u >
           BOARD_WIFI_SINK_QUEUE_RECORDS -
               BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS ||
       queue_.queuedBytes() + frame.length >
           BOARD_WIFI_SINK_QUEUE_BYTES -
               BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES);
  if (enters_critical_reserve) {
    result = WifiMailboxOfferResult::Reserved;
  } else {
    if (latency_bounded) {
      latency_records_.fetch_add(1, std::memory_order_release);
    }
    const bool pushed = queue_.push(frame);
    if (!pushed) {
      if (latency_bounded) {
        latency_records_.fetch_sub(1, std::memory_order_acq_rel);
      }
      result = WifiMailboxOfferResult::Full;
    } else if (queue_.count() == 1) {
      first_queued_ms_.store(now_ms == 0 ? 1u : now_ms,
                             std::memory_order_release);
    }
    if (pushed &&
        (!liveSessionActive() ||
         live_session_generation_.load(std::memory_order_acquire) !=
             live_generation ||
         (producer_abort_gate_.load(std::memory_order_acquire) &
          kProducerGateAbort) != 0)) {
      // A record committed concurrently with epoch close remains in the old
      // FIFO until the producer/abort handshake clears it. It was never
      // admitted to the sink, so exclude it from accepted-loss accounting.
      unaccepted_postcommit_bytes_.fetch_add(frame.length,
                                             std::memory_order_release);
      unaccepted_postcommit_records_.fetch_add(1, std::memory_order_release);
      result = WifiMailboxOfferResult::Busy;
    }
  }
  if (result == WifiMailboxOfferResult::Accepted) {
    beginAdmissionTransition();
    noteAccepted(frame);
    endAdmissionTransition();
  }
  if (result == WifiMailboxOfferResult::Reserved ||
      result == WifiMailboxOfferResult::Full) {
    // The first miss ends this live epoch. Do not accept later frames around
    // the gap; the next connection receives a fresh sequence anchor.
    deactivateLiveSession();
    requestQueuePressureDisconnect();
  }
  releaseProducerGate();
  if (result == WifiMailboxOfferResult::Accepted &&
      (was_unsent_empty || latency_bounded)) {
    if (was_unsent_empty) {
      empty_to_nonempty_wake_total_.fetch_add(1, std::memory_order_relaxed);
    }
    if (latency_bounded) {
      latency_wake_total_.fetch_add(1, std::memory_order_relaxed);
    }
    notifyWorker(WifiWakeTxData);
  }
  return result;
}

bool WifiWorkerMailbox::tryReadSessionAnchor(
    WifiMailboxSessionAnchor& anchor) const {
  const uint16_t length =
      session_anchor_length_.load(std::memory_order_acquire);
  if (length == 0 || length > sizeof(anchor.bytes)) return false;
  memcpy(anchor.bytes, session_anchor_bytes_, length);
  anchor.publish_seq = session_anchor_publish_seq_;
  anchor.length = length;
  return true;
}

bool WifiWorkerMailbox::tryStageTx(uint8_t* destination, uint16_t capacity,
                                   WifiMailboxTxLease& lease) {
  if (destination == nullptr || capacity == 0 ||
      producer_abort_gate_.load(std::memory_order_acquire) != 0u) {
    return false;
  }
  lease.generation = queue_generation_.load(std::memory_order_relaxed);
  lease.length = queue_.copyFrontBytes(destination, capacity);
  return lease.length != 0 &&
         producer_abort_gate_.load(std::memory_order_acquire) == 0u;
}

bool WifiWorkerMailbox::tryConsumeTx(
    const WifiMailboxTxLease& lease, uint16_t bytes,
    TxConsumeResult& result, bool& stale_generation) {
  stale_generation =
      lease.generation != queue_generation_.load(std::memory_order_relaxed);
  if (!stale_generation) {
    const uint16_t applied = bytes > lease.length ? lease.length : bytes;
    result = queue_.consumeMany(applied);
    if (result.latency_frames != 0) {
      latency_records_.fetch_sub(result.latency_frames,
                                 std::memory_order_acq_rel);
    }
    if (queue_.count() == 0) {
      first_queued_ms_.store(0, std::memory_order_release);
    }
  }
  return true;
}

bool WifiWorkerMailbox::tryApplyAbort(uint16_t session_anchor_sent_bytes,
                                      WifiMailboxAbortResult& result) {
  const uint32_t prior_gate = producer_abort_gate_.fetch_or(
      kProducerGateAbort, std::memory_order_acq_rel);
  if ((prior_gate & kProducerGateActive) != 0) return false;
  const uint32_t unaccepted_records =
      unaccepted_postcommit_records_.load(std::memory_order_acquire);
  const uint32_t unaccepted_bytes =
      unaccepted_postcommit_bytes_.load(std::memory_order_acquire);
  const Queue::ClearResult cleared =
      queue_.clearWithEvidence(unaccepted_records);
  const uint16_t anchor_length =
      session_anchor_length_.load(std::memory_order_acquire);
  const uint16_t bounded_anchor_sent =
      session_anchor_sent_bytes < anchor_length ? session_anchor_sent_bytes
                                                : anchor_length;
  const uint32_t anchor_pending_bytes = anchor_length - bounded_anchor_sent;
  result = {};
  result.records = cleared.records;
  result.bytes =
      cleared.bytes >= unaccepted_bytes ? cleared.bytes - unaccepted_bytes
                                        : 0u;
  if (cleared.sequence_valid) {
    result.sequence_valid = true;
    result.first_publish_seq = cleared.first_publish_seq;
    result.last_publish_seq = cleared.last_publish_seq;
  }
  if (anchor_pending_bytes != 0) {
    result.bytes += anchor_pending_bytes;
    result.records++;
    if (!result.sequence_valid ||
        session_anchor_publish_seq_ < result.first_publish_seq) {
      result.first_publish_seq = session_anchor_publish_seq_;
    }
    if (!result.sequence_valid ||
        session_anchor_publish_seq_ > result.last_publish_seq) {
      result.last_publish_seq = session_anchor_publish_seq_;
    }
    result.sequence_valid = true;
  }
  session_anchor_length_.store(0, std::memory_order_release);
  unaccepted_postcommit_bytes_.store(0, std::memory_order_release);
  unaccepted_postcommit_records_.store(0, std::memory_order_release);
  queue_generation_.fetch_add(1, std::memory_order_relaxed);
  first_queued_ms_.store(0, std::memory_order_relaxed);
  latency_records_.store(0, std::memory_order_release);
  downlink_router_.reset();
  producer_abort_gate_.store(0, std::memory_order_release);
  return true;
}

bool WifiWorkerMailbox::tryReadAdmissionSnapshot(
    WifiMailboxAdmissionSnapshot& snapshot) const {
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    const uint32_t before =
        admission_revision_.load(std::memory_order_acquire);
    if ((before & 1u) != 0u) continue;
    WifiMailboxAdmissionSnapshot candidate;
    const uint32_t accepted_bytes_low =
        accepted_bytes_total_low_.load(std::memory_order_relaxed);
    const uint32_t accepted_bytes_high =
        accepted_bytes_total_high_.load(std::memory_order_relaxed);
    candidate.accepted_bytes_total =
        (static_cast<uint64_t>(accepted_bytes_high) << 32u) |
        accepted_bytes_low;
    candidate.accepted_records_total =
        accepted_records_total_.load(std::memory_order_relaxed);
    const uint32_t sequence_low =
        last_accepted_publish_seq_low_.load(std::memory_order_relaxed);
    const uint32_t sequence_high =
        last_accepted_publish_seq_high_.load(std::memory_order_relaxed);
    candidate.last_accepted_publish_seq =
        (static_cast<uint64_t>(sequence_high) << 32u) | sequence_low;
    candidate.sequence_valid =
        accepted_sequence_valid_.load(std::memory_order_relaxed);
    const uint32_t after =
        admission_revision_.load(std::memory_order_acquire);
    if (before == after && (after & 1u) == 0u) {
      snapshot = candidate;
      return true;
    }
  }
  return false;
}

void WifiWorkerMailbox::requestAdmissionReconcile() {
  admission_reconcile_requested_.store(true, std::memory_order_release);
  // Close the store-vs-producer-release race in either direction. When the
  // producer is still active it performs the notification in
  // endAdmissionTransition(); when it already committed, notify here.
  if ((admission_revision_.load(std::memory_order_acquire) & 1u) == 0u &&
      admission_reconcile_requested_.exchange(
          false, std::memory_order_acq_rel)) {
    notifyWorker(WifiWakeControl);
  }
}

void WifiWorkerMailbox::requestAbort() {
  abort_request_sequence_.fetch_add(1, std::memory_order_release);
  notifyWorker(WifiWakeControl);
}

void WifiWorkerMailbox::requestDisconnect() {
  disconnect_request_sequence_.fetch_add(1, std::memory_order_release);
  deactivateLiveSession();
  requestAbort();
  notifyWorker(WifiWakeControl);
}

bool WifiWorkerMailbox::acknowledgeQueuePressureDisconnect(
    uint32_t handled_sequence) {
  if (handled_sequence !=
      queue_pressure_disconnect_request_sequence_.load(
          std::memory_order_acquire)) {
    return false;
  }
  queue_pressure_disconnect_latched_.store(false, std::memory_order_release);
  return true;
}

void WifiWorkerMailbox::markQueuePressureDisconnectHandled(
    uint32_t handled_sequence) {
  queue_pressure_disconnect_handled_sequence_.store(
      handled_sequence, std::memory_order_release);
}

uint32_t WifiWorkerMailbox::abortRequestSequence() const {
  return abort_request_sequence_.load(std::memory_order_acquire);
}

uint32_t WifiWorkerMailbox::disconnectRequestSequence() const {
  return disconnect_request_sequence_.load(std::memory_order_acquire);
}

uint32_t WifiWorkerMailbox::queuePressureDisconnectRequestSequence() const {
  return queue_pressure_disconnect_request_sequence_.load(
      std::memory_order_acquire);
}

uint32_t WifiWorkerMailbox::queuePressureDisconnectHandledSequence() const {
  return queue_pressure_disconnect_handled_sequence_.load(
      std::memory_order_acquire);
}

bool WifiWorkerMailbox::queuePressureDisconnectLatched() const {
  return queue_pressure_disconnect_latched_.load(std::memory_order_acquire);
}

WifiMailboxQueueSnapshot WifiWorkerMailbox::queueSnapshot() const {
  WifiMailboxQueueSnapshot snapshot;
  const uint32_t physical_bytes = queue_.queuedBytes();
  const uint32_t physical_records = queue_.count();
  const uint32_t unaccepted_bytes =
      unaccepted_postcommit_bytes_.load(std::memory_order_acquire);
  const uint32_t unaccepted_records =
      unaccepted_postcommit_records_.load(std::memory_order_acquire);
  snapshot.queued_bytes =
      physical_bytes >= unaccepted_bytes ? physical_bytes - unaccepted_bytes
                                         : 0u;
  snapshot.unsent_bytes = snapshot.queued_bytes;
  snapshot.high_water_bytes = queue_.highWaterBytes();
  snapshot.first_queued_ms = first_queued_ms_.load(std::memory_order_acquire);
  snapshot.empty_to_nonempty_wake_total =
      empty_to_nonempty_wake_total_.load(std::memory_order_acquire);
  snapshot.latency_wake_total =
      latency_wake_total_.load(std::memory_order_acquire);
  snapshot.queued_records =
      physical_records >= unaccepted_records
          ? static_cast<uint16_t>(physical_records - unaccepted_records)
          : 0u;
  snapshot.unsent_records = snapshot.queued_records;
  snapshot.high_water_records = queue_.highWaterRecords();
  snapshot.latency_bounded =
      latency_records_.load(std::memory_order_acquire) != 0;
  return snapshot;
}

void WifiWorkerMailbox::beginAdmissionTransition() {
  admission_revision_.fetch_add(1u, std::memory_order_acq_rel);
}

void WifiWorkerMailbox::endAdmissionTransition() {
  admission_revision_.fetch_add(1u, std::memory_order_release);
  if (admission_reconcile_requested_.exchange(
          false, std::memory_order_acq_rel)) {
    notifyWorker(WifiWakeControl);
  }
}

void WifiWorkerMailbox::noteAccepted(const PublishedFrameView& frame) {
  const uint32_t prior_low =
      accepted_bytes_total_low_.load(std::memory_order_relaxed);
  const uint32_t next_low = prior_low + frame.length;
  accepted_bytes_total_low_.store(next_low, std::memory_order_relaxed);
  if (next_low < prior_low) {
    accepted_bytes_total_high_.fetch_add(1u, std::memory_order_relaxed);
  }
  accepted_records_total_.fetch_add(1u, std::memory_order_relaxed);
  last_accepted_publish_seq_low_.store(
      static_cast<uint32_t>(frame.publish_seq), std::memory_order_relaxed);
  last_accepted_publish_seq_high_.store(
      static_cast<uint32_t>(frame.publish_seq >> 32u),
      std::memory_order_relaxed);
  accepted_sequence_valid_.store(true, std::memory_order_relaxed);
}

bool WifiWorkerMailbox::pushRx(const uint8_t* bytes, uint16_t length) {
  return downlink_router_.feed(bytes, length);
}

bool WifiWorkerMailbox::pushPassthroughRx(const uint8_t* bytes,
                                          uint16_t length) {
  if (bytes == nullptr || length == 0 || length > BOARD_WIFI_RX_MAILBOX_BYTES) {
    return false;
  }
  const uint32_t head = rx_head_.load(std::memory_order_relaxed);
  const uint32_t tail = rx_tail_.load(std::memory_order_acquire);
  if (head - tail + length > BOARD_WIFI_RX_MAILBOX_BYTES) return false;
  for (uint16_t index = 0; index < length; ++index) {
    rx_bytes_[(head + index) & (BOARD_WIFI_RX_MAILBOX_BYTES - 1u)] = bytes[index];
  }
  rx_head_.store(head + length, std::memory_order_release);
  return true;
}

bool WifiWorkerMailbox::passthroughThunk(void* context, const uint8_t* bytes,
                                        uint16_t length) {
  return static_cast<WifiWorkerMailbox*>(context)->pushPassthroughRx(
      bytes, length);
}

uint32_t WifiWorkerMailbox::rxAvailable() const {
  const uint32_t generation_before =
      rx_epoch_generation_.load(std::memory_order_acquire);
  if ((generation_before & 1u) != 0u) return 0;
  const uint32_t head = rx_head_.load(std::memory_order_acquire);
  const uint32_t tail = rx_tail_.load(std::memory_order_acquire);
  const uint32_t generation_after =
      rx_epoch_generation_.load(std::memory_order_acquire);
  if (generation_before != generation_after ||
      (generation_after & 1u) != 0u) {
    return 0;
  }
  const uint32_t available = head - tail;
  return available > BOARD_WIFI_RX_MAILBOX_BYTES ? BOARD_WIFI_RX_MAILBOX_BYTES
                                                  : available;
}

int WifiWorkerMailbox::readRx() {
  for (;;) {
    const uint32_t generation_before =
        rx_epoch_generation_.load(std::memory_order_acquire);
    if ((generation_before & 1u) != 0u) return -1;
    uint32_t tail = rx_tail_.load(std::memory_order_acquire);
    const uint32_t head = rx_head_.load(std::memory_order_acquire);
    if (tail == head) return -1;
    const int value = rx_bytes_[tail & (BOARD_WIFI_RX_MAILBOX_BYTES - 1u)];
    if (!rx_tail_.compare_exchange_weak(
            tail, tail + 1u, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      continue;
    }
    const uint32_t generation_after =
        rx_epoch_generation_.load(std::memory_order_acquire);
    return generation_before == generation_after &&
                   (generation_after & 1u) == 0u
               ? value
               : -1;
  }
}

int WifiWorkerMailbox::peekRx() const {
  const uint32_t generation_before =
      rx_epoch_generation_.load(std::memory_order_acquire);
  if ((generation_before & 1u) != 0u) return -1;
  const uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
  const uint32_t head = rx_head_.load(std::memory_order_acquire);
  if (tail == head) return -1;
  const int value = rx_bytes_[tail & (BOARD_WIFI_RX_MAILBOX_BYTES - 1u)];
  const uint32_t generation_after =
      rx_epoch_generation_.load(std::memory_order_acquire);
  return generation_before == generation_after &&
                 (generation_after & 1u) == 0u
             ? value
             : -1;
}

void WifiWorkerMailbox::discardRx() {
  uint32_t tail = rx_tail_.load(std::memory_order_acquire);
  for (;;) {
    const uint32_t head = rx_head_.load(std::memory_order_acquire);
    if (tail == head) return;
    if (rx_tail_.compare_exchange_weak(
            tail, head, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
      return;
    }
  }
}

void WifiWorkerMailbox::invalidateRxEpoch() {
  uint32_t generation =
      rx_epoch_generation_.load(std::memory_order_acquire);
  while ((generation & 1u) == 0u &&
         !rx_epoch_generation_.compare_exchange_weak(
             generation, generation + 1u, std::memory_order_acq_rel,
             std::memory_order_acquire)) {
  }
  discardRx();
}

void WifiWorkerMailbox::resetRxForNewEpoch() {
  invalidateRxEpoch();
  // The invalid generation stops consumers while the worker clears every byte
  // from the prior socket epoch. Only then is a new even generation exposed.
  discardRx();
  uint32_t generation =
      rx_epoch_generation_.load(std::memory_order_acquire);
  while ((generation & 1u) != 0u &&
         !rx_epoch_generation_.compare_exchange_weak(
             generation, generation + 1u, std::memory_order_acq_rel,
             std::memory_order_acquire)) {
  }
}

uint32_t WifiWorkerMailbox::rxEpochGeneration() const {
  return rx_epoch_generation_.load(std::memory_order_acquire);
}

void WifiWorkerMailbox::beginCall(WifiWorkerCallPhase phase, uint32_t now_ms) {
  call_revision_.fetch_add(1, std::memory_order_acq_rel);
  call_phase_.store(static_cast<uint32_t>(phase), std::memory_order_relaxed);
  call_started_ms_.store(now_ms, std::memory_order_relaxed);
  call_duration_us_.store(0, std::memory_order_relaxed);
  call_result_.store(0, std::memory_order_relaxed);
  call_sequence_.fetch_add(1, std::memory_order_relaxed);
  call_heartbeat_ms_.store(now_ms, std::memory_order_relaxed);
  call_in_progress_.store(1, std::memory_order_relaxed);
  call_revision_.fetch_add(1, std::memory_order_release);
}

void WifiWorkerMailbox::endCall(uint32_t now_ms, uint32_t duration_us,
                                int32_t result) {
  call_revision_.fetch_add(1, std::memory_order_acq_rel);
  call_duration_us_.store(duration_us, std::memory_order_relaxed);
  call_result_.store(result, std::memory_order_relaxed);
  call_heartbeat_ms_.store(now_ms, std::memory_order_relaxed);
  call_in_progress_.store(0, std::memory_order_relaxed);
  call_revision_.fetch_add(1, std::memory_order_release);
}

WifiWorkerCallSnapshot WifiWorkerMailbox::callSnapshot() const {
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    WifiWorkerCallSnapshot snapshot;
    const uint32_t before = call_revision_.load(std::memory_order_acquire);
    if ((before & 1u) != 0) continue;
    snapshot.phase = static_cast<WifiWorkerCallPhase>(
        call_phase_.load(std::memory_order_relaxed));
    snapshot.in_progress = call_in_progress_.load(std::memory_order_relaxed) != 0;
    snapshot.sequence = call_sequence_.load(std::memory_order_relaxed);
    snapshot.started_ms = call_started_ms_.load(std::memory_order_relaxed);
    snapshot.duration_us = call_duration_us_.load(std::memory_order_relaxed);
    snapshot.result = call_result_.load(std::memory_order_relaxed);
    snapshot.heartbeat_ms = call_heartbeat_ms_.load(std::memory_order_relaxed);
    const uint32_t after = call_revision_.load(std::memory_order_acquire);
    if (before == after) {
      snapshot.coherent = true;
      return snapshot;
    }
  }
  // A racing phase transition is not evidence of a stalled call. Returning a
  // value assembled across revisions previously produced an unsigned call-age
  // underflow and false epoch isolation. The next facade service retries.
  return WifiWorkerCallSnapshot{};
}

void WifiWorkerMailbox::publishState(const WifiWorkerStateSnapshot& state) {
  // Single-writer seqlock over atomic words. The old lock path silently
  // discarded a forced disconnect snapshot when the facade happened to read.
  state_revision_.fetch_add(1u, std::memory_order_acq_rel);
  const auto* source = reinterpret_cast<const uint8_t*>(&state);
  for (size_t index = 0; index < kStateWordCount; ++index) {
    const size_t offset = index * sizeof(uint32_t);
    const size_t remaining = sizeof(WifiWorkerStateSnapshot) - offset;
    const size_t count =
        remaining < sizeof(uint32_t) ? remaining : sizeof(uint32_t);
    uint32_t word = 0;
    memcpy(&word, source + offset, count);
    state_words_[index].store(word, std::memory_order_relaxed);
  }
  state_revision_.fetch_add(1u, std::memory_order_release);
}

bool WifiWorkerMailbox::tryReadState(WifiWorkerStateSnapshot& state) const {
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    const uint32_t before = state_revision_.load(std::memory_order_acquire);
    if ((before & 1u) != 0u) continue;
    uint32_t words[kStateWordCount] = {};
    for (size_t index = 0; index < kStateWordCount; ++index) {
      words[index] = state_words_[index].load(std::memory_order_relaxed);
    }
    const uint32_t after = state_revision_.load(std::memory_order_acquire);
    if (before == after && (after & 1u) == 0u) {
      memcpy(&state, words, sizeof(WifiWorkerStateSnapshot));
      return true;
    }
  }
  return false;
}

void WifiWorkerMailbox::requestQueuePressureDisconnect() {
  bool expected = false;
  if (queue_pressure_disconnect_latched_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel,
          std::memory_order_acquire)) {
    queue_pressure_disconnect_request_sequence_.fetch_add(
        1, std::memory_order_release);
    notifyWorker(WifiWakeControl);
  }
}

void WifiWorkerMailbox::releaseProducerGate() {
  producer_abort_gate_.fetch_and(~kProducerGateActive,
                                 std::memory_order_release);
}

void WifiWorkerMailbox::notifyWorker(uint32_t bits) const {
  if (notifier_.notify != nullptr) {
    notifier_.notify(notifier_.context, bits);
  }
}

}  // namespace csm::board::uplink
