#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol/TypedFrame.h"

namespace csm::board::uplink {

struct ProductDownlinkRouterCounters {
  uint32_t frame_total = 0;
  uint32_t legacy_ack_ignored_total = 0;
  uint32_t legacy_ack_malformed_total = 0;
  uint32_t passthrough_total = 0;
  uint32_t crc_failure_total = 0;
  uint32_t framing_failure_total = 0;
  uint32_t passthrough_overflow_total = 0;
};

// Dedicated receive router. Legacy APP_RX_COMMIT_ACK record 21 is reserved and
// discarded; it never affects TX storage and never enters the motion parser.
// Every other valid typed frame is forwarded byte-identically.
class ProductDownlinkRouter {
 public:
  using PassthroughHandler = bool (*)(void* context, const uint8_t* bytes,
                                      uint16_t length);

  void begin(PassthroughHandler passthrough_handler, void* context);
  bool feed(const uint8_t* bytes, uint16_t length);
  void reset();

  const ProductDownlinkRouterCounters& counters() const { return counters_; }

 private:
  static constexpr uint16_t kBufferCapacity =
      static_cast<uint16_t>(csm::encoded_typed_frame_len(csm::kMaxPayloadLen));

  PassthroughHandler passthrough_handler_ = nullptr;
  void* context_ = nullptr;
  uint8_t buffer_[kBufferCapacity] = {};
  uint16_t length_ = 0;
  ProductDownlinkRouterCounters counters_;

  bool process();
  void drop(uint16_t count);
};

}  // namespace csm::board::uplink
