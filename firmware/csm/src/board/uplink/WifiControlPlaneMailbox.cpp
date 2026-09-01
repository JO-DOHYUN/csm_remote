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
  if (payload == nullptr || length != csm::kControlAckPayloadLen ||
      !active_.load(std::memory_order_acquire)) {
    return false;
  }
  const uint32_t generation = generation_.load(std::memory_order_acquire);
  const uint32_t tail = tail_.load(std::memory_order_relaxed);
  const uint32_t head = head_.load(std::memory_order_acquire);
  if (tail - head >= kQueueRecords) {
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
    return false;
  }
  slot.length = static_cast<uint16_t>(written);
  if (!active_.load(std::memory_order_acquire) ||
      generation_.load(std::memory_order_acquire) != generation) {
    return false;
  }
  publish_sequence_.store(sequence + 1u, std::memory_order_relaxed);
  tail_.store(tail + 1u, std::memory_order_release);
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
  if (length > kRxCapacity - (tail - head)) return false;
  for (uint16_t i = 0; i < length; ++i) {
    rx_[(tail + i) & (kRxCapacity - 1u)] = bytes[i];
  }
  rx_tail_.store(tail + length, std::memory_order_release);
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
  return value;
}

int WifiControlPlaneMailbox::peek() {
  const uint32_t head = rx_head_.load(std::memory_order_relaxed);
  if (head == rx_tail_.load(std::memory_order_acquire)) return -1;
  return rx_[head & (kRxCapacity - 1u)];
}

}  // namespace csm::board::uplink
