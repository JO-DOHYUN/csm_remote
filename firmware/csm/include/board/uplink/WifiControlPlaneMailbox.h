#pragma once

#include <Arduino.h>
#include <atomic>
#include <stdint.h>

#include "board/uplink/UplinkPriority.h"
#include "protocol/TypedFrame.h"

namespace csm::board::uplink {

struct ControlTransportEvidence {
  uint32_t rx_bytes = 0;
  uint32_t rx_read_bytes = 0;
  uint32_t rx_last_ms = 0;
  uint32_t rx_high_water = 0;
  uint32_t rx_overflow = 0;
  uint32_t ack_generated_id = 0;
  uint32_t ack_generated_ms = 0;
  uint32_t ack_offered = 0;
  uint32_t ack_admitted = 0;
  uint32_t ack_rejected = 0;
  uint32_t ack_high_water = 0;
  uint32_t ack_sent_id = 0;
  uint32_t ack_sent_ms = 0;
  uint32_t ack_sent_total = 0;
  uint32_t tx_bytes = 0;
  uint32_t would_block = 0;
  uint32_t send_max_us = 0;
  uint32_t close_total = 0;
  uint32_t close_reason = 0;
  uint32_t close_ms = 0;
  uint32_t close_result = 0;
  uint32_t pending_id = 0;
  uint32_t tx_offset = 0;
  uint32_t queued = 0;
  // First local transport observation since boot; immutable across reconnect.
  uint32_t first[8] = {};  // reason, ms, epoch, RX, ACK generated/sent, TX, result
};

// Dedicated, bounded Host control path. It deliberately carries only the
// connection anchor and CONTROL_ACK records; canonical telemetry remains on
// the independent evidence socket.
class WifiControlPlaneMailbox final : public Stream {
 public:
  static constexpr uint16_t kFrameCapacity = 64;
  static constexpr uint8_t kQueueRecords = 16;
  static constexpr uint16_t kRxCapacity = 1024;

  void configure(uint64_t boot_session_id);
  bool activate(uint64_t mono_us);
  void deactivate();

  bool offerAck(const uint8_t* payload, uint16_t length);
  bool copyAnchor(uint8_t* destination, uint16_t capacity,
                  uint16_t& length) const;
  bool peekTx(uint8_t* destination, uint16_t capacity,
              uint16_t& length) const;
  void consumeTx();
  bool pushRx(const uint8_t* bytes, uint16_t length);

  bool connected() const { return active_.load(std::memory_order_acquire); }
  uint32_t connectionEpoch() const {
    return connection_epoch_.load(std::memory_order_acquire);
  }
  uint32_t disconnectRequestSequence() const {
    return disconnect_request_sequence_.load(std::memory_order_acquire);
  }
  uint16_t queuedRecords() const;
  ControlTransportEvidence evidence() const;
  void noteReceive(int32_t result, uint32_t now_ms);
  void noteSend(int32_t result, uint32_t now_ms, uint32_t duration_us,
                uint32_t command_id, uint16_t offset, bool complete,
                bool would_block = false);
  void noteClose(uint32_t reason, int32_t result, uint32_t now_ms);

  int available() override;
  int read() override;
  int peek() override;
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t) override { return 0; }

 private:
  struct FrameSlot {
    uint8_t bytes[kFrameCapacity] = {};
    uint16_t length = 0;
  };

  uint64_t boot_session_id_ = 0;
  FrameSlot frames_[kQueueRecords] = {};
  uint8_t anchor_[kFrameCapacity] = {};
  uint16_t anchor_length_ = 0;
  uint8_t rx_[kRxCapacity] = {};
  std::atomic<uint32_t> generation_{0};
  std::atomic<uint32_t> head_{0};
  std::atomic<uint32_t> tail_{0};
  std::atomic<uint32_t> rx_head_{0};
  std::atomic<uint32_t> rx_tail_{0};
  std::atomic<uint32_t> publish_sequence_{0};
  std::atomic<uint32_t> connection_epoch_{0};
  std::atomic<uint32_t> disconnect_request_sequence_{0};
  std::atomic<bool> active_{false};
  // Bounded observation only; no diagnostic value is read by admission.
  std::atomic<uint32_t> rx_bytes_{0};
  std::atomic<uint32_t> rx_read_bytes_{0};
  std::atomic<uint32_t> rx_last_ms_{0};
  std::atomic<uint32_t> rx_high_water_{0};
  std::atomic<uint32_t> rx_overflow_{0};
  std::atomic<uint32_t> ack_generated_id_{0};
  std::atomic<uint32_t> ack_generated_ms_{0};
  std::atomic<uint32_t> ack_offered_{0};
  std::atomic<uint32_t> ack_admitted_{0};
  std::atomic<uint32_t> ack_rejected_{0};
  std::atomic<uint32_t> ack_high_water_{0};
  std::atomic<uint32_t> ack_sent_id_{0};
  std::atomic<uint32_t> ack_sent_ms_{0};
  std::atomic<uint32_t> ack_sent_total_{0};
  std::atomic<uint32_t> tx_bytes_{0};
  std::atomic<uint32_t> would_block_{0};
  std::atomic<uint32_t> send_max_us_{0};
  std::atomic<uint32_t> close_total_{0};
  std::atomic<uint32_t> close_reason_{0};
  std::atomic<uint32_t> close_ms_{0};
  std::atomic<uint32_t> close_result_{0};
  std::atomic<uint32_t> pending_id_{0};
  std::atomic<uint32_t> tx_offset_{0};
  std::atomic<bool> first_claimed_{false};
  std::atomic<bool> first_ready_{false};
  uint32_t first_[8] = {};
  void recordFirst(uint32_t reason, int32_t result, uint32_t now_ms);
};

}  // namespace csm::board::uplink
