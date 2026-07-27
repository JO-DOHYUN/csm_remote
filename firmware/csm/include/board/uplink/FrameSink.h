#pragma once

#include <stdint.h>

#include "board/uplink/UplinkPriority.h"
#include "protocol/TypedFrame.h"

namespace csm::board::uplink {

struct PublishedFrameView {
  const uint8_t* bytes = nullptr;
  uint16_t length = 0;
  uint64_t publish_seq = 0;
  csm::RecordType type = static_cast<csm::RecordType>(0);
  UplinkPriority priority = UplinkPriority::Normal;
  UplinkDeliveryClass delivery = UplinkDeliveryClass::Batchable;
};

enum class SinkOfferResult : uint8_t {
  Accepted = 0,
  Disabled,
  Disconnected,
  Overflow,
  Invalid,
};

struct SinkServiceResult {
  uint32_t actual_bytes = 0;
  uint32_t frames_completed = 0;
  bool backpressure_event = false;
  uint32_t backpressure_duration_ms = 0;
  bool epoch_changed = false;
  bool queue_pressure_event = false;
};

class IFrameSink {
 public:
  virtual ~IFrameSink() = default;
  virtual bool enabled() const = 0;
  virtual bool connected() const = 0;
  virtual SinkOfferResult offer(const PublishedFrameView& frame) = 0;
};

}  // namespace csm::board::uplink
