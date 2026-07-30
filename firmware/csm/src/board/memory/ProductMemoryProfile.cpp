#include "board/memory/ProductMemoryProfile.h"

#include <mbed.h>

#ifndef BOARD_USE_PINNED_PRODUCT_MBED
#define BOARD_USE_PINNED_PRODUCT_MBED 0
#endif

namespace csm::board::memory {
namespace {

constexpr uint32_t kD3Base = 0x38000000u;
constexpr uint8_t kProductD3Region = 15u;
ProductMemoryProfileStatus status;

}  // namespace

bool configureProductMemoryProfile() {
  status = {};
  status.required = BOARD_USE_PINNED_PRODUCT_MBED != 0;
  status.region = kProductD3Region;

#if BOARD_USE_PINNED_PRODUCT_MBED && defined(__MPU_PRESENT) && \
    (__MPU_PRESENT == 1U)
  const uint32_t region_count =
      (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
  if (region_count <= kProductD3Region) {
    return false;
  }

  constexpr uint32_t expected_rasr = ARM_MPU_RASR(
      1,                    // execute never
      ARM_MPU_AP_FULL,      // privileged and unprivileged read/write
      1,                    // TEX: normal memory, non-cacheable
      1,                    // shareable
      0,                    // non-cacheable
      0,                    // non-bufferable
      0,                    // all subregions enabled
      ARM_MPU_REGION_SIZE_64KB);

  __DMB();
  const uint32_t previous_region = MPU->RNR;
  // This is the first product-owned D3 configuration, before Wi-Fi starts.
  // Do not disable the global MPU after Mbed has started: doing so briefly
  // removes protection/attributes from every active region and can race the
  // scheduler or an interrupt. Region 15 has the highest priority and only
  // covers the product-owned D3 window.
  MPU->RNR = kProductD3Region;
  MPU->RBAR = kD3Base;
  MPU->RASR = expected_rasr;
  __DSB();
  __ISB();

  MPU->RNR = kProductD3Region;
  status.rbar = MPU->RBAR;
  status.rasr = MPU->RASR;
  status.configured = true;
  status.verified =
      (status.rbar & MPU_RBAR_ADDR_Msk) == kD3Base &&
      status.rasr == expected_rasr &&
      (MPU->CTRL & MPU_CTRL_ENABLE_Msk) != 0;
  MPU->RNR = previous_region;
  return status.verified;
#else
  status.configured = !status.required;
  status.verified = !status.required;
  return status.verified;
#endif
}

const ProductMemoryProfileStatus& productMemoryProfileStatus() {
  return status;
}

}  // namespace csm::board::memory
