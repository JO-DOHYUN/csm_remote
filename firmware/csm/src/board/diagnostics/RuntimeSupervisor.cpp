#include "board/diagnostics/RuntimeSupervisor.h"

namespace csm::board::diagnostics {

RuntimeSupervisor::RuntimeSupervisor(
    void* retained_storage, std::size_t retained_storage_bytes,
    BootRecoveryConfig config, RetainedStorageAdapter adapter)
    : recovery_(retained_storage, retained_storage_bytes, config, adapter) {}

RuntimeSupervisorDecision RuntimeSupervisor::begin(
    const RuntimeSupervisorBootInfo& info) {
  RuntimeSupervisorBootInfo effective = info;
  // Persist one stable source/config identity without changing the retained
  // layout. Exact artifact rebuilds retain history; source, resolved runtime
  // contract, or REF/A/B/C selector changes get one clean trial.
  effective.firmware.source_id = composeRecoverySourceIdentity(
      info.firmware, kResetExperimentProfile.selector);

  BootStartInfo boot;
  boot.firmware = effective.firmware;
  boot.reset_cause_bits = effective.reset_cause_bits;
  boot.uptime_ms = effective.uptime_ms;
  decision_.recovery_ready = recovery_.beginBoot(boot);
  decision_.watchdog_requested =
      effective.watchdog_compiled && kResetExperimentProfile.watchdog_enabled;
  decision_.requested_wifi_mode = kResetExperimentProfile.wifi_runtime_mode;
  decision_.effective_wifi_mode = decision_.requested_wifi_mode;
  last_progress_ms_ = effective.uptime_ms;
  last_wifi_call_sequence_ = 0;
  stable_reported_ = false;
  return decision_;
}

bool RuntimeSupervisor::recordProgress(BootProgress progress, uint32_t detail,
                                       uint32_t uptime_ms) {
  if (!decision_.recovery_ready) return false;
  return recovery_.recordProgress(bootProgressId(progress), detail, uptime_ms);
}

void RuntimeSupervisor::service(
    const RuntimeSupervisorObservation& observation) {
  if (!decision_.recovery_ready) return;

  const ProductRecoverySnapshot recovery = recovery_.snapshot();
  uint32_t detail = decision_.watchdog_requested ? (1u << 11) : 0u;
  if (recovery.wifi_quarantined) detail |= (1u << 12);
  if (observation.watchdog_effective) detail |= (1u << 13);
  if (observation.watchdog_timeout_matches) detail |= (1u << 14);
  if (observation.watchdog_start_succeeded) detail |= (1u << 15);

  if (observation.wifi_present) {
    detail |= static_cast<uint32_t>(observation.wifi_call_phase);
    if (observation.wifi_call_in_progress) detail |= (1u << 8);
    detail |= (static_cast<uint32_t>(decision_.effective_wifi_mode) & 0x03u)
              << 9;
    uint32_t heartbeat_age = observation.wifi_worker_heartbeat_age_ms;
    if (heartbeat_age > 0xFFFFu) heartbeat_age = 0xFFFFu;
    detail |= heartbeat_age << 16;

    if (observation.wifi_call_in_progress && observation.wifi_call_slow &&
        observation.wifi_call_sequence != last_wifi_call_sequence_) {
      last_wifi_call_sequence_ = observation.wifi_call_sequence;
      const uint32_t call_detail =
          (static_cast<uint32_t>(observation.wifi_call_phase) << 24) |
          (observation.wifi_call_sequence & 0x00FFFFFFu);
      recordProgress(BootProgress::WifiWorkerCallObserved, call_detail,
                     observation.now_ms);
    }
  }

  if (static_cast<uint32_t>(observation.now_ms - last_progress_ms_) >= 1000u) {
    last_progress_ms_ = observation.now_ms;
    recordProgress(BootProgress::MainLoopAlive, detail, observation.now_ms);
  }

  if (!stable_reported_ && recovery_.markStable(observation.now_ms)) {
    stable_reported_ = true;
  }
}

}  // namespace csm::board::diagnostics
