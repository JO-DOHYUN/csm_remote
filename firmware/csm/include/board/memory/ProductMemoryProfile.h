#pragma once

#include <stdint.h>

namespace csm::board::memory {

struct ProductMemoryProfileStatus {
  bool required = false;
  bool configured = false;
  bool verified = false;
  uint8_t region = 0;
  uint32_t rbar = 0;
  uint32_t rasr = 0;
};

// Installs the product D3 override before lwIP/WHD startup. The highest MPU
// region wins over Mbed's broad cacheable SRAM region.
bool configureProductMemoryProfile();

const ProductMemoryProfileStatus& productMemoryProfileStatus();

}  // namespace csm::board::memory
