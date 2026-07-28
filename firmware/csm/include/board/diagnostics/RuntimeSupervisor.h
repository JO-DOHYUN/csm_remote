#pragma once

#include <cstddef>
#include <cstdint>

#include "board/diagnostics/BootProgress.h"
#include "board/diagnostics/BootRecovery.h"
#include "board/diagnostics/ResetExperimentProfile.h"

namespace csm::board::diagnostics {

constexpr uint64_t mixRecoveryIdentity64(uint64_t value) {
  value ^= value >> 30u;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27u;
  value *= 0x94D049BB133111EBULL;
  value ^= value >> 31u;
  return value;
}

// BootRecovery has a fixed retained layout with one 64-bit source/config
// field. Compose the independently useful source-tree and resolved-runtime
// identities, plus the experiment selector, before crossing that boundary.
constexpr uint64_t composeRecoverySourceIdentity(
    const FirmwareIdentity& firmware, uint8_t experiment_selector) {
  const uint64_t source = mixRecoveryIdentity64(
      firmware.source_id ^ 0x43534D5F53524331ULL);  // "CSM_SRC1"
  const uint64_t contract = mixRecoveryIdentity64(
      firmware.runtime_contract_id ^ 0x43534D5F52544331ULL);  // "CSM_RTC1"
  const uint64_t selector = mixRecoveryIdentity64(
      static_cast<uint64_t>(experiment_selector) ^ 0x43534D5F45585031ULL);
  const uint64_t composed = mixRecoveryIdentity64(source ^ contract ^ selector);
  return composed == 0u ? 0x43534D5F494431ULL : composed;
}

struct RuntimeSupervisorBootInfo {
  FirmwareIdentity firmware{};
  uint32_t reset_cause_bits = 0;
  uint32_t uptime_ms = 0;
  bool watchdog_compiled = false;
};

struct RuntimeSupervisorDecision {
  bool recovery_ready = false;
  // Requested policy only. Hardware truth is sampled by the board adapter and
  // returned through RuntimeSupervisorObservation/BOARD_HEALTH.
  bool watchdog_requested = false;
  ResetExperimentWifiRuntimeMode requested_wifi_mode =
      ResetExperimentWifiRuntimeMode::Off;
  ResetExperimentWifiRuntimeMode effective_wifi_mode =
      ResetExperimentWifiRuntimeMode::Off;
};

struct RuntimeSupervisorObservation {
  uint32_t now_ms = 0;
  bool watchdog_effective = false;
  bool watchdog_start_succeeded = false;
  bool watchdog_timeout_matches = false;
  bool wifi_present = false;
  uint8_t wifi_call_phase = 0;
  bool wifi_call_in_progress = false;
  bool wifi_call_slow = false;
  uint32_t wifi_call_sequence = 0;
  uint32_t wifi_worker_heartbeat_age_ms = 0;
};

// Owns boot-loop classification and the always-on scalar black box. It has no
// Arduino, Wi-Fi, CAN, USB, or watchdog-driver dependency; main only adapts
// board observations and applies the returned startup decision.
class RuntimeSupervisor {
 public:
  RuntimeSupervisor(void* retained_storage, std::size_t retained_storage_bytes,
                    BootRecoveryConfig config,
                    RetainedStorageAdapter adapter);

  RuntimeSupervisorDecision begin(const RuntimeSupervisorBootInfo& info);
  bool recordProgress(BootProgress progress, uint32_t detail,
                      uint32_t uptime_ms);
  void service(const RuntimeSupervisorObservation& observation);

  const RuntimeSupervisorDecision& decision() const { return decision_; }
  ProductRecoverySnapshot recoverySnapshot() const {
    return recovery_.snapshot();
  }
  std::size_t readRecentEvents(BootRecoveryEvent* output,
                               std::size_t capacity) const {
    return recovery_.readRecentEvents(output, capacity);
  }
  bool wifiQuarantined() const { return false; }

 private:
  BootRecovery recovery_;
  RuntimeSupervisorDecision decision_{};
  uint32_t last_progress_ms_ = 0;
  uint32_t last_wifi_call_sequence_ = 0;
  bool stable_reported_ = false;
};

}  // namespace csm::board::diagnostics
