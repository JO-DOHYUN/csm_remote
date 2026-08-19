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
// Admission importance and the bounded delivery contract remain independent.
enum class UplinkDeliveryClass : uint8_t {
  Batchable = 0,
  LatencyBounded = 1,
};

}  // namespace csm::board::uplink
