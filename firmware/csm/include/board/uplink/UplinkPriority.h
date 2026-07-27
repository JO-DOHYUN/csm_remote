#pragma once

#include <stdint.h>

namespace csm::board::uplink {

enum class UplinkPriority : uint8_t {
  Diagnostic = 0,
  Normal = 1,
  CanTruth = 2,
  Critical = 3,
};

// Admission importance and delivery latency are independent contracts.
// Critical evidence may still be batchable (for example periodic CAN_TX_RAW),
// while an ordinary event can require a bounded delivery delay.
enum class UplinkDeliveryClass : uint8_t {
  Batchable = 0,
  LatencyBounded = 1,
};

}  // namespace csm::board::uplink
