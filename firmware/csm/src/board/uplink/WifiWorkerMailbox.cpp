#include "board/uplink/WifiWorkerMailbox.h"

#include <string.h>

namespace csm::board::uplink {

WifiWorkerMailbox::WifiWorkerMailbox() {
  state_lock_.clear(std::memory_order_release);
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
  WifiMailboxOfferResult result = WifiMailboxOfferResult::Accepted;
  const uint16_t normal_limit = static_cast<uint16_t>(
      BOARD_WIFI_SINK_QUEUE_RECORDS - BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS);
  const uint32_t normal_byte_limit =
      BOARD_WIFI_SINK_QUEUE_BYTES - BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES;
  if (frame.priority != UplinkPriority::Critical &&
      (queue_.count() >= normal_limit ||
       queue_.queuedBytes() + frame.length > normal_byte_limit)) {
    result = WifiMailboxOfferResult::Reserved;
  } else {
    const bool critical = frame.priority == UplinkPriority::Critical;
    if (critical) {
      urgent_records_.fetch_add(1, std::memory_order_release);
    }
    if (!queue_.push(frame)) {
      if (critical) urgent_records_.fetch_sub(1, std::memory_order_acq_rel);
      result = WifiMailboxOfferResult::Full;
    } else {
      if (queue_.count() == 1) {
        first_queued_ms_.store(now_ms, std::memory_order_relaxed);
      }
      updateQueueSnapshot();
    }
  }
  producer_active_.store(false, std::memory_order_release);
  return result;
}

bool WifiWorkerMailbox::tryStageTx(uint8_t* destination, uint16_t capacity,
                                   WifiMailboxTxLease& lease) {
  if (destination == nullptr || capacity == 0 ||
      abort_in_progress_.load(std::memory_order_acquire)) return false;
  lease.generation = queue_generation_.load(std::memory_order_relaxed);
  lease.length = queue_.copyFrontBytes(destination, capacity);
  return lease.length > 0;
}

bool WifiWorkerMailbox::tryConsumeTx(
    const WifiMailboxTxLease& lease, uint16_t bytes,
    TxConsumeResult& result, bool& stale_generation) {
  stale_generation = lease.generation != queue_generation_.load(std::memory_order_relaxed);
  if (!stale_generation) {
    const uint16_t applied = bytes > lease.length ? lease.length : bytes;
    result = queue_.consumeMany(applied);
    if (result.critical_frames > 0) {
      urgent_records_.fetch_sub(result.critical_frames, std::memory_order_acq_rel);
    }
    if (queue_.empty()) {
      first_queued_ms_.store(0, std::memory_order_relaxed);
    }
    updateQueueSnapshot();
  }
  return true;
}

bool WifiWorkerMailbox::tryApplyAbort(uint32_t& aborted_bytes) {
  abort_in_progress_.store(true, std::memory_order_release);
  if (producer_active_.load(std::memory_order_acquire)) return false;
  aborted_bytes = queue_.clear();
  queue_generation_.fetch_add(1, std::memory_order_relaxed);
  first_queued_ms_.store(0, std::memory_order_relaxed);
  urgent_records_.store(0, std::memory_order_release);
  updateQueueSnapshot();
  abort_in_progress_.store(false, std::memory_order_release);
  return true;
}

void WifiWorkerMailbox::requestAbort() {
  abort_request_sequence_.fetch_add(1, std::memory_order_release);
}

void WifiWorkerMailbox::requestDisconnect() {
  disconnect_request_sequence_.fetch_add(1, std::memory_order_release);
  requestAbort();
}

uint32_t WifiWorkerMailbox::abortRequestSequence() const {
  return abort_request_sequence_.load(std::memory_order_acquire);
}

uint32_t WifiWorkerMailbox::disconnectRequestSequence() const {
  return disconnect_request_sequence_.load(std::memory_order_acquire);
}

WifiMailboxQueueSnapshot WifiWorkerMailbox::queueSnapshot() const {
  WifiMailboxQueueSnapshot snapshot;
  snapshot.queued_bytes = queued_bytes_.load(std::memory_order_acquire);
  snapshot.high_water_bytes = queue_high_water_bytes_.load(std::memory_order_acquire);
  snapshot.first_queued_ms = first_queued_ms_.load(std::memory_order_acquire);
  snapshot.queued_records = static_cast<uint16_t>(
      queued_records_.load(std::memory_order_acquire));
  snapshot.high_water_records = static_cast<uint16_t>(
      queue_high_water_records_.load(std::memory_order_acquire));
  snapshot.urgent = urgent_records_.load(std::memory_order_acquire) != 0;
  return snapshot;
}

bool WifiWorkerMailbox::pushRx(const uint8_t* bytes, uint16_t length) {
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
  WifiWorkerCallSnapshot snapshot;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
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
    if (before == after) return snapshot;
  }
  return snapshot;
}

void WifiWorkerMailbox::publishState(const WifiWorkerStateSnapshot& state) {
  if (state_lock_.test_and_set(std::memory_order_acquire)) return;
  state_ = state;
  state_lock_.clear(std::memory_order_release);
}

bool WifiWorkerMailbox::tryReadState(WifiWorkerStateSnapshot& state) const {
  if (state_lock_.test_and_set(std::memory_order_acquire)) return false;
  state = state_;
  state_lock_.clear(std::memory_order_release);
  return true;
}

void WifiWorkerMailbox::updateQueueSnapshot() {
  queued_bytes_.store(queue_.queuedBytes(), std::memory_order_release);
  queued_records_.store(queue_.count(), std::memory_order_release);
  queue_high_water_bytes_.store(queue_.highWaterBytes(), std::memory_order_release);
  queue_high_water_records_.store(queue_.highWaterRecords(), std::memory_order_release);
}

}  // namespace csm::board::uplink
