#include "board/uplink/WifiWorkerMailbox.h"

#include <string.h>

namespace csm::board::uplink {

WifiWorkerMailbox::WifiWorkerMailbox(TxStorage& storage) : queue_(storage) {
  downlink_router_.begin(&WifiWorkerMailbox::appAckThunk,
                         &WifiWorkerMailbox::passthroughThunk, this);
}

void WifiWorkerMailbox::setNotifier(const WifiWorkerNotifier& notifier) {
  notifier_ = notifier;
}

void WifiWorkerMailbox::configureReliableSession(uint64_t boot_session_id) {
  boot_session_id_ = boot_session_id;
}

void WifiWorkerMailbox::activateReliableSession() {
  reliable_session_active_.store(true, std::memory_order_release);
}

void WifiWorkerMailbox::rewindUnacked() {
  queue_.rewindToLastAck();
  ++rewind_total_;
  downlink_router_.reset();
}

bool WifiWorkerMailbox::reliableSessionActive() const {
  return reliable_session_active_.load(std::memory_order_acquire);
}

bool WifiWorkerMailbox::reliableIntegrityFault() const {
  return reliable_integrity_fault_.load(std::memory_order_acquire);
}

WifiMailboxReliabilitySnapshot
WifiWorkerMailbox::workerReliabilitySnapshot() const {
  const TxQueue::Snapshot journal = queue_.snapshot(true);
  WifiMailboxReliabilitySnapshot result;
  result.boot_session_id = boot_session_id_;
  result.highest_sent_publish_seq = journal.highest_sent_publish_seq;
  result.last_acked_publish_seq = journal.last_acked_publish_seq;
  result.reclaimed_bytes_total = reclaimed_bytes_total_;
  result.ack_accepted_total = ack_accepted_total_;
  result.ack_rejected_total = ack_rejected_total_;
  result.rewind_total = rewind_total_;
  result.session_active = reliableSessionActive();
  result.ack_valid = journal.ack_valid;
  result.integrity_fault = reliableIntegrityFault();
  result.replay_active =
      journal.ack_valid && journal.unsent_bytes > 0 &&
      journal.unsent_bytes < journal.retained_bytes;
  return result;
}

WifiMailboxOfferResult WifiWorkerMailbox::tryOffer(
    const PublishedFrameView& frame, uint32_t now_ms) {
  if (frame.bytes == nullptr || frame.length == 0 ||
      frame.length > csm::encoded_typed_frame_len(csm::kMaxPayloadLen)) {
    return WifiMailboxOfferResult::Invalid;
  }
  producer_active_.store(true, std::memory_order_release);
  if (abort_in_progress_.load(std::memory_order_acquire)) {
    producer_active_.store(false, std::memory_order_release);
    return WifiMailboxOfferResult::Busy;
  }
  if (!reliableSessionActive()) {
    producer_active_.store(false, std::memory_order_release);
    return WifiMailboxOfferResult::Busy;
  }
  const bool critical = frame.priority == UplinkPriority::Critical;
  if (reliableIntegrityFault() && !critical) {
    producer_active_.store(false, std::memory_order_release);
    return WifiMailboxOfferResult::Full;
  }
  const TxQueue::Snapshot before = queue_.snapshot();
  const bool was_unsent_empty = before.unsent_records == 0;
  const bool latency_bounded =
      frame.delivery == UplinkDeliveryClass::LatencyBounded;
  WifiMailboxOfferResult result = WifiMailboxOfferResult::Accepted;
  const bool enters_critical_reserve =
      !critical &&
      (static_cast<uint32_t>(before.retained_records) + 1u >
           BOARD_WIFI_SINK_QUEUE_RECORDS -
               BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS ||
       before.retained_bytes + frame.length >
           BOARD_WIFI_SINK_QUEUE_BYTES -
               BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES);
  if (enters_critical_reserve) {
    result = WifiMailboxOfferResult::Reserved;
    reliable_integrity_fault_.store(true, std::memory_order_release);
    requestQueuePressureDisconnect();
  } else if (!queue_.push(frame, now_ms)) {
    result = WifiMailboxOfferResult::Full;
    reliable_integrity_fault_.store(true, std::memory_order_release);
    requestQueuePressureDisconnect();
  }
  producer_active_.store(false, std::memory_order_release);
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

bool WifiWorkerMailbox::tryStageTx(uint8_t* destination, uint16_t capacity,
                                   WifiMailboxTxLease& lease) {
  if (destination == nullptr || capacity == 0 ||
      abort_in_progress_.load(std::memory_order_acquire)) return false;
  TxQueue::TxLease journal_lease;
  if (!queue_.stage(destination, capacity, journal_lease)) return false;
  lease.generation = journal_lease.generation;
  lease.length = journal_lease.length;
  return true;
}

bool WifiWorkerMailbox::tryConsumeTx(
    const WifiMailboxTxLease& lease, uint16_t bytes,
    TxConsumeResult& result, bool& stale_generation) {
  TxQueue::TxLease journal_lease;
  journal_lease.generation = lease.generation;
  journal_lease.length = lease.length;
  return queue_.advanceSent(journal_lease, bytes, result, stale_generation);
}

bool WifiWorkerMailbox::tryApplyAbort(uint32_t& aborted_bytes) {
  abort_in_progress_.store(true, std::memory_order_release);
  if (producer_active_.load(std::memory_order_acquire)) return false;
  aborted_bytes = queue_.clear();
  reliable_session_active_.store(false, std::memory_order_release);
  reliable_integrity_fault_.store(false, std::memory_order_release);
  downlink_router_.reset();
  abort_in_progress_.store(false, std::memory_order_release);
  return true;
}

void WifiWorkerMailbox::requestAbort() {
  abort_request_sequence_.fetch_add(1, std::memory_order_release);
  notifyWorker(WifiWakeControl);
}

void WifiWorkerMailbox::requestDisconnect() {
  disconnect_request_sequence_.fetch_add(1, std::memory_order_release);
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
  const TxQueue::Snapshot journal = queue_.snapshot();
  snapshot.queued_bytes = journal.retained_bytes;
  snapshot.unsent_bytes = journal.unsent_bytes;
  snapshot.high_water_bytes = journal.high_water_bytes;
  snapshot.first_queued_ms = journal.front_admitted_ms;
  snapshot.empty_to_nonempty_wake_total =
      empty_to_nonempty_wake_total_.load(std::memory_order_acquire);
  snapshot.latency_wake_total =
      latency_wake_total_.load(std::memory_order_acquire);
  snapshot.queued_records = journal.retained_records;
  snapshot.unsent_records = journal.unsent_records;
  snapshot.high_water_records = journal.high_water_records;
  snapshot.latency_bounded = journal.front_latency_bounded;
  return snapshot;
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

bool WifiWorkerMailbox::applyAppAck(uint64_t boot_session_id,
                                    uint64_t publish_seq) {
  if (boot_session_id == 0 || boot_session_id != boot_session_id_) {
    ++ack_rejected_total_;
    return false;
  }
  const TxQueue::AckResult ack = queue_.acknowledge(publish_seq);
  if (!ack.accepted()) {
    ++ack_rejected_total_;
    return false;
  }
  ++ack_accepted_total_;
  reclaimed_bytes_total_ += ack.reclaimed_bytes;
  return true;
}

bool WifiWorkerMailbox::appAckThunk(void* context, uint64_t boot_session_id,
                                    uint64_t publish_seq) {
  return static_cast<WifiWorkerMailbox*>(context)->applyAppAck(
      boot_session_id, publish_seq);
}

bool WifiWorkerMailbox::passthroughThunk(void* context, const uint8_t* bytes,
                                        uint16_t length) {
  return static_cast<WifiWorkerMailbox*>(context)->pushPassthroughRx(
      bytes, length);
}

uint32_t WifiWorkerMailbox::rxAvailable() const {
  const uint32_t head = rx_head_.load(std::memory_order_acquire);
  const uint32_t tail = rx_tail_.load(std::memory_order_acquire);
  const uint32_t available = head - tail;
  return available > BOARD_WIFI_RX_MAILBOX_BYTES ? BOARD_WIFI_RX_MAILBOX_BYTES
                                                  : available;
}

int WifiWorkerMailbox::readRx() {
  const uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
  const uint32_t head = rx_head_.load(std::memory_order_acquire);
  if (tail == head) return -1;
  const int value = rx_bytes_[tail & (BOARD_WIFI_RX_MAILBOX_BYTES - 1u)];
  rx_tail_.store(tail + 1u, std::memory_order_release);
  return value;
}

int WifiWorkerMailbox::peekRx() const {
  const uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
  const uint32_t head = rx_head_.load(std::memory_order_acquire);
  return tail == head ? -1 : rx_bytes_[tail & (BOARD_WIFI_RX_MAILBOX_BYTES - 1u)];
}

void WifiWorkerMailbox::discardRx() {
  rx_tail_.store(rx_head_.load(std::memory_order_acquire),
                 std::memory_order_release);
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

void WifiWorkerMailbox::notifyWorker(uint32_t bits) const {
  if (notifier_.notify != nullptr) {
    notifier_.notify(notifier_.context, bits);
  }
}

}  // namespace csm::board::uplink
