#pragma once

#include "board/control_island/ControlIslandContract.h"

namespace csm::board::control_island {

// Single CSM-local predicate: telemetry consumers report this result, never
// independently apply their arrival clock to M4 runtime readiness.
inline uint8_t localReadyReason(bool present, uint32_t age_ms,
                                uint32_t timeout_ms, uint32_t flags) {
  if (!present) return 1;
  if (timeout_ms == 0u) return 2;
  if (age_ms > timeout_ms) return 3;
  if (flags & kHealthFlagBusOff) return 4;
  if (flags & kHealthFlagErrorPassive) return 5;
  if (flags & kHealthFlagTrackingFault) return 6;
  if (!(flags & kHealthFlagClockContractOk)) return 7;
  if (!(flags & kHealthFlagM7Fresh)) return 8;
  if (!(flags & kHealthFlagReady)) return 9;
  return 0;
}

// M7 foreground only. Evidence latch, not authority. First observation remains
// available after DISARM/reconnect; only boot initializes it. No logs/queues.
struct ControlPathFirstFailure {
  uint32_t reason = 0;
  uint32_t observed_ms = 0;
  uint32_t context[18] = {};
  bool record(uint32_t failure, uint32_t now_ms, const uint32_t (&values)[18]) {
    if (reason != 0u || failure == 0u) return false;
    observed_ms = now_ms;
    for (unsigned i = 0; i < 18; ++i) context[i] = values[i];
    reason = failure;
    return true;
  }
};

}  // namespace csm::board::control_island
