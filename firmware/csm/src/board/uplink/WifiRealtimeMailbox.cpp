#include "board/uplink/WifiRealtimeMailbox.h"

#include <string.h>

namespace csm::board::uplink {

void WifiRealtimeMailbox::reset() {
  rx_guard_.clear(std::memory_order_relaxed);
  rx_slot_ = {};
  rx_next_token_.store(0, std::memory_order_relaxed);
  rx_consumed_token_.store(0, std::memory_order_relaxed);
  proof_guard_.clear(std::memory_order_relaxed);
  proof_slot_ = {};
  proof_next_token_.store(0, std::memory_order_relaxed);
  proof_consumed_token_.store(0, std::memory_order_relaxed);
  worker_started_.store(false, std::memory_order_relaxed);
  socket_ready_.store(false, std::memory_order_relaxed);
  network_epoch_.store(0, std::memory_order_relaxed);
  rx_datagrams_.store(0, std::memory_order_relaxed);
  rx_bytes_.store(0, std::memory_order_relaxed);
  rx_overwrite_.store(0, std::memory_order_relaxed);
  rx_last_ms_.store(0, std::memory_order_relaxed);
  rx_max_gap_ms_.store(0, std::memory_order_relaxed);
  rx_budget_hits_.store(0, std::memory_order_relaxed);
  proof_staged_.store(0, std::memory_order_relaxed);
  proof_sent_.store(0, std::memory_order_relaxed);
  proof_would_block_.store(0, std::memory_order_relaxed);
  proof_socket_error_.store(0, std::memory_order_relaxed);
  proof_last_sequence_.store(0, std::memory_order_relaxed);
  proof_last_sent_ms_.store(0, std::memory_order_relaxed);
  socket_error_.store(0, std::memory_order_relaxed);
  last_socket_result_.store(0, std::memory_order_relaxed);
  worker_heartbeat_ms_.store(0, std::memory_order_relaxed);
}

bool WifiRealtimeMailbox::publishRx(const uint8_t* bytes, uint16_t length,
                                    uint32_t arrival_ms, uint32_t* token) {
  if (bytes == nullptr || length == 0u ||
      length > kRealtimeDatagramCapacity) return false;
  if (rx_guard_.test_and_set(std::memory_order_acquire)) {
    incrementSaturating(&rx_overwrite_);
    return false;
  }
  uint32_t next = rx_next_token_.fetch_add(1, std::memory_order_relaxed) + 1u;
  if (next == 0u) next = rx_next_token_.fetch_add(1, std::memory_order_relaxed) + 1u;
  const uint32_t prior = rx_slot_.token;
  if (prior != 0u && prior != rx_consumed_token_.load(std::memory_order_acquire)) {
    incrementSaturating(&rx_overwrite_);
  }
  memcpy(rx_slot_.bytes, bytes, length);
  rx_slot_.length = length;
  rx_slot_.token = next;
  rx_slot_.arrival_ms = arrival_ms;
  rx_guard_.clear(std::memory_order_release);
  incrementSaturating(&rx_datagrams_);
  rx_bytes_.fetch_add(length, std::memory_order_relaxed);
  const uint32_t prior_ms = rx_last_ms_.exchange(arrival_ms, std::memory_order_relaxed);
  if (prior_ms != 0u) {
    const uint32_t gap = arrival_ms - prior_ms;
    uint32_t maximum = rx_max_gap_ms_.load(std::memory_order_relaxed);
    while (gap > maximum && !rx_max_gap_ms_.compare_exchange_weak(
        maximum, gap, std::memory_order_relaxed)) {}
  }
  if (token != nullptr) *token = next;
  return true;
}

bool WifiRealtimeMailbox::takeLatestRx(WifiRealtimeDatagram* datagram) {
  if (datagram == nullptr) return false;
  if (rx_guard_.test_and_set(std::memory_order_acquire)) return false;
  const WifiRealtimeDatagram candidate = rx_slot_;
  if (candidate.token == 0u ||
      candidate.token == rx_consumed_token_.load(std::memory_order_relaxed)) {
    rx_guard_.clear(std::memory_order_release);
    return false;
  }
  *datagram = candidate;
  rx_consumed_token_.store(candidate.token, std::memory_order_release);
  rx_guard_.clear(std::memory_order_release);
  return true;
}

bool WifiRealtimeMailbox::stageProof(const uint8_t* bytes, uint16_t length,
                                     uint32_t rx_token,
                                     uint32_t proof_sequence,
                                     uint32_t now_ms) {
  if (bytes == nullptr || length != kRealtimeProofFrameBytes ||
      rx_token == 0u || proof_sequence == 0u) return false;
  if (proof_guard_.test_and_set(std::memory_order_acquire)) {
    incrementSaturating(&proof_would_block_);
    return false;
  }
  uint32_t next = proof_next_token_.fetch_add(1, std::memory_order_relaxed) + 1u;
  if (next == 0u) next = proof_next_token_.fetch_add(1, std::memory_order_relaxed) + 1u;
  memcpy(proof_slot_.bytes, bytes, length);
  proof_slot_.length = length;
  proof_slot_.token = next;
  proof_slot_.rx_token = rx_token;
  proof_slot_.staged_ms = now_ms;
  proof_guard_.clear(std::memory_order_release);
  incrementSaturating(&proof_staged_);
  notify();
  return true;
}

bool WifiRealtimeMailbox::peekProof(WifiRealtimeProof* proof) const {
  if (proof == nullptr) return false;
  if (proof_guard_.test_and_set(std::memory_order_acquire)) return false;
  const WifiRealtimeProof candidate = proof_slot_;
  if (candidate.token == 0u || candidate.token ==
      proof_consumed_token_.load(std::memory_order_relaxed)) {
    proof_guard_.clear(std::memory_order_release);
    return false;
  }
  *proof = candidate;
  proof_guard_.clear(std::memory_order_release);
  return true;
}

void WifiRealtimeMailbox::consumeProof(uint32_t token) {
  if (token != 0u) proof_consumed_token_.store(token, std::memory_order_release);
}

void WifiRealtimeMailbox::discardProof() {
  // The worker may discard while M7 stages a newer proof. Never read the
  // seqlock-protected plain slot outside a stable snapshot; consuming the
  // latest published token also ensures a proof is never replayed after a
  // socket/peer epoch closes.
  proof_consumed_token_.store(
      proof_next_token_.load(std::memory_order_acquire),
      std::memory_order_release);
}

void WifiRealtimeMailbox::noteWorkerStarted(bool started) {
  worker_started_.store(started, std::memory_order_relaxed);
}

void WifiRealtimeMailbox::noteSocketReady(bool ready, uint32_t network_epoch) {
  socket_ready_.store(ready, std::memory_order_relaxed);
  network_epoch_.store(network_epoch, std::memory_order_relaxed);
}

void WifiRealtimeMailbox::noteRxBudgetHit() { incrementSaturating(&rx_budget_hits_); }

void WifiRealtimeMailbox::noteProofSend(int32_t result,
                                        uint32_t proof_sequence,
                                        uint32_t now_ms,
                                        bool would_block) {
  last_socket_result_.store(result, std::memory_order_relaxed);
  if (would_block) {
    incrementSaturating(&proof_would_block_);
  } else if (result > 0) {
    incrementSaturating(&proof_sent_);
    proof_last_sequence_.store(proof_sequence, std::memory_order_relaxed);
    proof_last_sent_ms_.store(now_ms, std::memory_order_relaxed);
  } else if (result < 0) {
    incrementSaturating(&proof_socket_error_);
  }
}

void WifiRealtimeMailbox::noteSocketError(int32_t result) {
  last_socket_result_.store(result, std::memory_order_relaxed);
  incrementSaturating(&socket_error_);
}

void WifiRealtimeMailbox::noteWorkerHeartbeat(uint32_t now_ms) {
  worker_heartbeat_ms_.store(now_ms, std::memory_order_relaxed);
}

WifiRealtimeEvidence WifiRealtimeMailbox::evidence() const {
  WifiRealtimeEvidence value;
  value.worker_started = worker_started_.load(std::memory_order_relaxed);
  value.socket_ready = socket_ready_.load(std::memory_order_relaxed);
  value.network_epoch = network_epoch_.load(std::memory_order_relaxed);
  value.rx_datagrams = rx_datagrams_.load(std::memory_order_relaxed);
  value.rx_bytes = rx_bytes_.load(std::memory_order_relaxed);
  value.rx_overwrite = rx_overwrite_.load(std::memory_order_relaxed);
  value.rx_last_token = rx_next_token_.load(std::memory_order_relaxed);
  value.rx_last_ms = rx_last_ms_.load(std::memory_order_relaxed);
  value.rx_max_gap_ms = rx_max_gap_ms_.load(std::memory_order_relaxed);
  value.rx_budget_hits = rx_budget_hits_.load(std::memory_order_relaxed);
  value.proof_staged = proof_staged_.load(std::memory_order_relaxed);
  value.proof_sent = proof_sent_.load(std::memory_order_relaxed);
  value.proof_would_block = proof_would_block_.load(std::memory_order_relaxed);
  value.proof_socket_error = proof_socket_error_.load(std::memory_order_relaxed);
  value.proof_last_sequence = proof_last_sequence_.load(std::memory_order_relaxed);
  value.proof_last_sent_ms = proof_last_sent_ms_.load(std::memory_order_relaxed);
  value.socket_error = socket_error_.load(std::memory_order_relaxed);
  value.last_socket_result = last_socket_result_.load(std::memory_order_relaxed);
  value.worker_heartbeat_ms = worker_heartbeat_ms_.load(std::memory_order_relaxed);
  return value;
}

void WifiRealtimeMailbox::incrementSaturating(std::atomic<uint32_t>* value) {
  uint32_t current = value->load(std::memory_order_relaxed);
  while (current != UINT32_MAX && !value->compare_exchange_weak(
      current, current + 1u, std::memory_order_relaxed)) {}
}

void WifiRealtimeMailbox::notify() {
  const WifiRealtimeNotifier notifier = notifier_;
  if (notifier.notify != nullptr) notifier.notify(notifier.context);
}

}  // namespace csm::board::uplink
