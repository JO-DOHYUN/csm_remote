#pragma once

#include <stdint.h>

namespace csm::board::diagnostics {

// Stable product diagnostic IDs. They describe ownership boundaries, not
// source-line locations, so retained evidence remains meaningful after a
// refactor. Add IDs; do not renumber existing ones.
enum class BootProgress : uint32_t {
  None = 0,
  SetupEntered = 1,
  ResetEvidenceCaptured = 2,
  WatchdogConfigured = 3,
  UsbSinkReady = 4,
  WifiStartRequested = 5,
  WifiStartReturned = 6,
  CanonicalPublisherReady = 7,
  McpFrontendInitEntered = 8,
  McpFrontendInitReturned = 9,
  BuiltinCanInitEntered = 10,
  BuiltinCanInitReturned = 11,
  RemoteRuntimeInitEntered = 12,
  RemoteRuntimeInitReturned = 13,
  SetupCompleted = 14,
  MainLoopAlive = 15,
  WifiWorkerCallObserved = 16,
  RuntimeStable = 17,
};

constexpr uint32_t bootProgressId(BootProgress progress) {
  return static_cast<uint32_t>(progress);
}

}  // namespace csm::board::diagnostics
