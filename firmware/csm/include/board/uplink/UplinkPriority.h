#pragma once

#include <stdint.h>

namespace csm::board::uplink {

enum class UplinkPriority : uint8_t {
  Diagnostic = 0,
  Normal = 1,
  CanTruth = 2,
  Critical = 3,
};

}  // namespace csm::board::uplink
