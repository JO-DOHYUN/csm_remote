#pragma once

#include <Arduino.h>
#include <atomic>
#include <stdint.h>

#include "board/uplink/UplinkPriority.h"
#include "protocol/TypedFrame.h"

namespace csm::board::uplink {

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
};

}  // namespace csm::board::uplink
