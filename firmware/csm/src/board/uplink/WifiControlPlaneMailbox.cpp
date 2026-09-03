#include "board/uplink/WifiControlPlaneMailbox.h"

#include <string.h>

#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

static_assert((WifiControlPlaneMailbox::kQueueRecords &
               (WifiControlPlaneMailbox::kQueueRecords - 1u)) == 0,
              "control queue record count must be a power of two");
static_assert((WifiControlPlaneMailbox::kRxCapacity &
               (WifiControlPlaneMailbox::kRxCapacity - 1u)) == 0,
              "control RX capacity must be a power of two");
static_assert(WifiControlPlaneMailbox::kFrameCapacity >=
                  csm::encoded_typed_frame_len(csm::kControlAckPayloadLen),
              "control slot must hold CONTROL_ACK");

void WifiControlPlaneMailbox::configure(uint64_t boot_session_id) {
  boot_session_id_ = boot_session_id;
  publish_sequence_.store(0, std::memory_order_release);
  deactivate();
}

bool WifiControlPlaneMailbox::activate(uint64_t mono_us) {
  active_.store(false, std::memory_order_release);
  generation_.fetch_add(1, std::memory_order_acq_rel);
  const uint32_t tail = tail_.load(std::memory_order_acquire);
  head_.store(tail, std::memory_order_release);
  rx_head_.store(rx_tail_.load(std::memory_order_acquire),
                 std::memory_order_release);

  uint8_t payload[32] = {};
  payload[0] = csm::kStreamSessionSchema;
  payload[csm::kStreamSessionReasonOffset] = 2;  // SinkEpochChanged.
  csm::wr_u16_le(&payload[csm::kStreamSessionFlagsOffset], 0x0001);
  payload[csm::kStreamSessionProtocolVersionOffset] = csm::kProtocolVersion;
  csm::wr_u64_le(&payload[csm::kStreamSessionBootIdOffset], boot_session_id_);
  const uint32_t sequence =
      publish_sequence_.fetch_add(1, std::memory_order_acq_rel);
  csm::wr_u64_le(&payload[csm::kStreamSessionPublishSeqOffset], sequence);
  csm::wr_u64_le(&payload[csm::kStreamSessionMonoUsOffset], mono_us);
  size_t written = 0;
  if (!csm::encode_typed_frame(
          anchor_, sizeof(anchor_), csm::RecordType::StreamSession, payload,
          sizeof(payload), static_cast<uint16_t>(sequence & 0xFFFFu), 0,
          &written)) {
    return false;
  }
  anchor_length_ = static_cast<uint16_t>(written);
  connection_epoch_.fetch_add(1, std::memory_order_acq_rel);
  active_.store(true, std::memory_order_release);
  return true;
}

void WifiControlPlaneMailbox::deactivate() {
  const bool was_active = active_.exchange(false, std::memory_order_acq_rel);
  generation_.fetch_add(1, std::memory_order_acq_rel);
  head_.store(tail_.load(std::memory_order_acquire),
              std::memory_order_release);
  rx_head_.store(rx_tail_.load(std::memory_order_acquire),
                 std::memory_order_release);
  anchor_length_ = 0;
  if (was_active) {
    connection_epoch_.fetch_add(1, std::memory_order_acq_rel);
  }
}

bool WifiControlPlaneMailbox::offerAck(const uint8_t* payload,
                                       uint16_t length) {
  ack_offered_.fetch_add(1, std::memory_order_relaxed);
  if (payload != nullptr && length == csm::kControlAckPayloadLen) {
    ack_generated_id_.store(csm::rd_u32_le(payload + 8), std::memory_order_relaxed);
    ack_generated_ms_.store(static_cast<uint32_t>(csm::rd_u64_le(payload) / 1000u),
                             std::memory_order_relaxed);
  }
  if (payload == nullptr || length != csm::kControlAckPayloadLen ||
      !active_.load(std::memory_order_acquire)) {
    ack_rejected_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const uint32_t generation = generation_.load(std::memory_order_acquire);
  const uint32_t tail = tail_.load(std::memory_order_relaxed);
  const uint32_t head = head_.load(std::memory_order_acquire);
  if (tail - head >= kQueueRecords) {
    ack_rejected_.fetch_add(1, std::memory_order_relaxed);
    recordFirst(7u, 0, ack_generated_ms_.load(std::memory_order_relaxed));
    active_.store(false, std::memory_order_release);
    connection_epoch_.fetch_add(1, std::memory_order_acq_rel);
    disconnect_request_sequence_.fetch_add(1, std::memory_order_acq_rel);
    return false;
  }
  FrameSlot& slot = frames_[tail & (kQueueRecords - 1u)];
  const uint32_t sequence = publish_sequence_.load(std::memory_order_relaxed);
  size_t written = 0;
  if (!csm::encode_typed_frame(
          slot.bytes, sizeof(slot.bytes), csm::RecordType::ControlAck, payload,
          length, static_cast<uint16_t>(sequence & 0xFFFFu), 0, &written)) {
    ack_rejected_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  slot.length = static_cast<uint16_t>(written);
  if (!active_.load(std::memory_order_acquire) ||
      generation_.load(std::memory_order_acquire) != generation) {
    ack_rejected_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  publish_sequence_.store(sequence + 1u, std::memory_order_relaxed);
  tail_.store(tail + 1u, std::memory_order_release);
  ack_admitted_.fetch_add(1, std::memory_order_relaxed);
  if (tail + 1u - head > ack_high_water_.load(std::memory_order_relaxed))
    ack_high_water_.store(tail + 1u - head, std::memory_order_relaxed);
  return true;
}

bool WifiControlPlaneMailbox::copyAnchor(uint8_t* destination,
                                         uint16_t capacity,
                                         uint16_t& length) const {
  if (!connected() || anchor_length_ == 0 || destination == nullptr ||
      capacity < anchor_length_) {
    return false;
  }
  memcpy(destination, anchor_, anchor_length_);
  length = anchor_length_;
  return true;
}

bool WifiControlPlaneMailbox::peekTx(uint8_t* destination, uint16_t capacity,
                                     uint16_t& length) const {
  const uint32_t head = head_.load(std::memory_order_relaxed);
  const uint32_t tail = tail_.load(std::memory_order_acquire);
  if (!connected() || head == tail || destination == nullptr) return false;
  const FrameSlot& slot = frames_[head & (kQueueRecords - 1u)];
  if (slot.length == 0 || capacity < slot.length) return false;
  memcpy(destination, slot.bytes, slot.length);
  length = slot.length;
  return true;
}

void WifiControlPlaneMailbox::consumeTx() {
  const uint32_t head = head_.load(std::memory_order_relaxed);
  if (head != tail_.load(std::memory_order_acquire)) {
    head_.store(head + 1u, std::memory_order_release);
  }
}

bool WifiControlPlaneMailbox::pushRx(const uint8_t* bytes, uint16_t length) {
  if (bytes == nullptr || length == 0 || !connected()) return false;
  const uint32_t head = rx_head_.load(std::memory_order_acquire);
  const uint32_t tail = rx_tail_.load(std::memory_order_relaxed);
  if (length > kRxCapacity - (tail - head)) {
    rx_overflow_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  for (uint16_t i = 0; i < length; ++i) {
    rx_[(tail + i) & (kRxCapacity - 1u)] = bytes[i];
  }
  rx_tail_.store(tail + length, std::memory_order_release);
  if (tail + length - head > rx_high_water_.load(std::memory_order_relaxed))
    rx_high_water_.store(tail + length - head, std::memory_order_relaxed);
  return true;
}

uint16_t WifiControlPlaneMailbox::queuedRecords() const {
  return static_cast<uint16_t>(tail_.load(std::memory_order_acquire) -
                               head_.load(std::memory_order_acquire));
}

int WifiControlPlaneMailbox::available() {
  return static_cast<int>(rx_tail_.load(std::memory_order_acquire) -
                          rx_head_.load(std::memory_order_acquire));
}

int WifiControlPlaneMailbox::read() {
  const uint32_t head = rx_head_.load(std::memory_order_relaxed);
  if (head == rx_tail_.load(std::memory_order_acquire)) return -1;
  const uint8_t value = rx_[head & (kRxCapacity - 1u)];
  rx_head_.store(head + 1u, std::memory_order_release);
  rx_read_bytes_.fetch_add(1, std::memory_order_relaxed);
  return value;
}

int WifiControlPlaneMailbox::peek() {
  const uint32_t head = rx_head_.load(std::memory_order_relaxed);
  if (head == rx_tail_.load(std::memory_order_acquire)) return -1;
  return rx_[head & (kRxCapacity - 1u)];
}


ControlTransportEvidence WifiControlPlaneMailbox::evidence() const {
  ControlTransportEvidence result;
  result.rx_bytes = rx_bytes_.load(std::memory_order_relaxed);
  result.rx_read_bytes = rx_read_bytes_.load(std::memory_order_relaxed);
  result.rx_last_ms = rx_last_ms_.load(std::memory_order_relaxed);
  result.rx_high_water = rx_high_water_.load(std::memory_order_relaxed);
  result.rx_overflow = rx_overflow_.load(std::memory_order_relaxed);
  result.ack_generated_id = ack_generated_id_.load(std::memory_order_relaxed);
  result.ack_generated_ms = ack_generated_ms_.load(std::memory_order_relaxed);
  result.ack_offered = ack_offered_.load(std::memory_order_relaxed);
  result.ack_admitted = ack_admitted_.load(std::memory_order_relaxed);
  result.ack_rejected = ack_rejected_.load(std::memory_order_relaxed);
  result.ack_high_water = ack_high_water_.load(std::memory_order_relaxed);
  result.ack_sent_id = ack_sent_id_.load(std::memory_order_relaxed);
  result.ack_sent_ms = ack_sent_ms_.load(std::memory_order_relaxed);
  result.ack_sent_total = ack_sent_total_.load(std::memory_order_relaxed);
  result.tx_bytes = tx_bytes_.load(std::memory_order_relaxed);
  result.would_block = would_block_.load(std::memory_order_relaxed);
  result.send_max_us = send_max_us_.load(std::memory_order_relaxed);
  result.close_total = close_total_.load(std::memory_order_relaxed);
  result.close_reason = close_reason_.load(std::memory_order_relaxed);
  result.close_ms = close_ms_.load(std::memory_order_relaxed);
  result.close_result = close_result_.load(std::memory_order_relaxed);
  result.pending_id = pending_id_.load(std::memory_order_relaxed);
  result.tx_offset = tx_offset_.load(std::memory_order_relaxed);
  result.queued = queuedRecords();
  if (first_ready_.load(std::memory_order_acquire)) {
    for (unsigned i = 0; i < 8; ++i) result.first[i] = first_[i];
  }
  return result;
}

void WifiControlPlaneMailbox::recordFirst(uint32_t reason, int32_t result,
                                          uint32_t now_ms) {
  bool expected = false;
  if (!first_claimed_.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel)) return;
  first_[0] = reason;
  first_[1] = now_ms;
  first_[2] = connectionEpoch();
  first_[3] = rx_bytes_.load(std::memory_order_relaxed);
  first_[4] = ack_generated_id_.load(std::memory_order_relaxed);
  first_[5] = ack_sent_id_.load(std::memory_order_relaxed);
  first_[6] = tx_bytes_.load(std::memory_order_relaxed);
  first_[7] = static_cast<uint32_t>(result);
  first_ready_.store(true, std::memory_order_release);
}

void WifiControlPlaneMailbox::noteReceive(int32_t result, uint32_t now_ms) {
  if (result > 0) {
    rx_bytes_.fetch_add(static_cast<uint32_t>(result), std::memory_order_relaxed);
    rx_last_ms_.store(now_ms, std::memory_order_relaxed);
  }
}

void WifiControlPlaneMailbox::noteSend(int32_t result, uint32_t now_ms,
    uint32_t duration_us, uint32_t command_id, uint16_t offset, bool complete,
    bool would_block) {
  pending_id_.store(command_id, std::memory_order_relaxed);
  tx_offset_.store(offset, std::memory_order_relaxed);
  if (duration_us > send_max_us_.load(std::memory_order_relaxed))
    send_max_us_.store(duration_us, std::memory_order_relaxed);
  if (result > 0) tx_bytes_.fetch_add(result, std::memory_order_relaxed);
  else if (would_block) would_block_.fetch_add(1, std::memory_order_relaxed);
  if (complete && command_id != 0u) {
    ack_sent_id_.store(command_id, std::memory_order_relaxed);
    ack_sent_ms_.store(now_ms, std::memory_order_relaxed);
    ack_sent_total_.fetch_add(1, std::memory_order_relaxed);
  }
}

void WifiControlPlaneMailbox::noteClose(uint32_t reason, int32_t result,
                                        uint32_t now_ms) {
  close_total_.fetch_add(1, std::memory_order_relaxed);
  close_reason_.store(reason, std::memory_order_relaxed);
  close_ms_.store(now_ms, std::memory_order_relaxed);
  close_result_.store(static_cast<uint32_t>(result), std::memory_order_relaxed);
  recordFirst(reason, result, now_ms);
}

}  // namespace csm::board::uplink
