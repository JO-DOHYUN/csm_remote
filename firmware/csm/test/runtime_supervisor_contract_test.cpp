#include <array>
#include <cstdint>
#include <iostream>

#include "board/diagnostics/RuntimeSupervisor.h"

namespace {

using namespace csm::board::diagnostics;
using Storage = std::array<uint8_t, BootRecovery::kRequiredStorageBytes>;

int failures = 0;
#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition \
                << '\n';                                                     \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

RuntimeSupervisorBootInfo boot(uint64_t build_id, uint64_t source_id,
                               uint64_t runtime_contract_id = 0u) {
  RuntimeSupervisorBootInfo info;
  info.firmware =
      FirmwareIdentity{build_id, source_id, runtime_contract_id};
  info.watchdog_compiled = true;
  return info;
}

RuntimeSupervisor makeSupervisor(Storage& storage) {
  return RuntimeSupervisor(storage.data(), storage.size(),
                           BootRecoveryConfig{30000u, 2u}, {});
}

void verifySelectedProfile() {
  Storage storage{};
  RuntimeSupervisor supervisor = makeSupervisor(storage);
  const RuntimeSupervisorDecision decision = supervisor.begin(boot(1u, 11u));
  CHECK(decision.recovery_ready);
  CHECK(decision.requested_wifi_mode ==
        kResetExperimentProfile.wifi_runtime_mode);
  CHECK(decision.effective_wifi_mode == decision.requested_wifi_mode);
  CHECK(decision.watchdog_requested ==
        kResetExperimentProfile.watchdog_enabled);
  CHECK(supervisor.recoverySnapshot().firmware_source_id ==
        composeRecoverySourceIdentity(
            FirmwareIdentity{1u, 11u, 0u},
            kResetExperimentProfile.selector));

  RuntimeSupervisorObservation observation;
  observation.now_ms = 1000u;
  observation.watchdog_effective = true;
  observation.watchdog_start_succeeded = true;
  observation.watchdog_timeout_matches = true;
  supervisor.service(observation);
  const uint32_t detail = supervisor.recoverySnapshot().last_progress_detail;
  CHECK(((detail >> 11) & 1u) ==
        (kResetExperimentProfile.watchdog_enabled ? 1u : 0u));
  CHECK((detail & (1u << 13)) != 0u);
  CHECK((detail & (1u << 14)) != 0u);
  CHECK((detail & (1u << 15)) != 0u);
}

void verifyRuntimeContractIsolation() {
  Storage storage{};
  constexpr uint64_t kSource = 77u;
  constexpr uint64_t kContractOne = 1001u;
  constexpr uint64_t kContractTwo = 1002u;
  {
    RuntimeSupervisor first = makeSupervisor(storage);
    first.begin(boot(1u, kSource, kContractOne));
  }
  {
    RuntimeSupervisor rebuilt = makeSupervisor(storage);
    rebuilt.begin(boot(2u, kSource, kContractOne));
    const ProductRecoverySnapshot snapshot = rebuilt.recoverySnapshot();
    CHECK(snapshot.firmware_build_changed);
    CHECK(!snapshot.firmware_source_changed);
    CHECK(snapshot.consecutive_early_resets == 1u);
  }
  {
    RuntimeSupervisor repeated_reset = makeSupervisor(storage);
    const RuntimeSupervisorDecision decision =
        repeated_reset.begin(boot(3u, kSource, kContractOne));
    CHECK(!repeated_reset.recoverySnapshot().wifi_quarantined);
    CHECK(decision.effective_wifi_mode == decision.requested_wifi_mode);
  }
  {
    // A material resolved-runtime change gets a clean trial even when the
    // firmware source tree is byte-identical.
    RuntimeSupervisor changed_contract = makeSupervisor(storage);
    const RuntimeSupervisorDecision decision =
        changed_contract.begin(boot(4u, kSource, kContractTwo));
    const ProductRecoverySnapshot snapshot =
        changed_contract.recoverySnapshot();
    CHECK(snapshot.firmware_source_changed);
    CHECK(!snapshot.wifi_quarantined);
    CHECK(snapshot.consecutive_early_resets == 0u);
    CHECK(decision.effective_wifi_mode == decision.requested_wifi_mode);
    CHECK(snapshot.firmware_source_id == composeRecoverySourceIdentity(
        FirmwareIdentity{4u, kSource, kContractTwo},
        kResetExperimentProfile.selector));
  }
  {
    RuntimeSupervisor same_contract_rebuild = makeSupervisor(storage);
    same_contract_rebuild.begin(boot(5u, kSource, kContractTwo));
    CHECK(!same_contract_rebuild.recoverySnapshot().firmware_source_changed);
    CHECK(same_contract_rebuild.recoverySnapshot().consecutive_early_resets ==
          1u);
  }
}

void verifyEarlyResetEvidenceDoesNotDisableWifi() {
  Storage storage{};
  {
    RuntimeSupervisor first = makeSupervisor(storage);
    CHECK(first.begin(boot(1u, 22u)).effective_wifi_mode ==
          kResetExperimentProfile.wifi_runtime_mode);
  }
  {
    RuntimeSupervisor second = makeSupervisor(storage);
    CHECK(second.begin(boot(2u, 22u)).effective_wifi_mode ==
          kResetExperimentProfile.wifi_runtime_mode);
    CHECK(second.recoverySnapshot().consecutive_early_resets == 1u);
  }
  {
    RuntimeSupervisor third = makeSupervisor(storage);
    const RuntimeSupervisorDecision decision = third.begin(boot(3u, 22u));
    CHECK(decision.effective_wifi_mode == decision.requested_wifi_mode);
    CHECK(third.recoverySnapshot().consecutive_early_resets == 2u);
    CHECK(!third.recoverySnapshot().wifi_quarantined);
    CHECK(third.recoverySnapshot().wifi_start_allowed);

    RuntimeSupervisorObservation observation;
    observation.now_ms = 30000u;
    observation.wifi_present = true;
    observation.wifi_call_phase = 2u;
    observation.wifi_call_in_progress = true;
    observation.wifi_call_slow = true;
    observation.wifi_call_sequence = 7u;
    observation.wifi_worker_heartbeat_age_ms = 500u;
    third.service(observation);
    CHECK(third.recoverySnapshot().current_boot_stable);
  }
  {
    RuntimeSupervisor stable_followup = makeSupervisor(storage);
    const RuntimeSupervisorDecision decision =
        stable_followup.begin(boot(4u, 22u));
    CHECK(stable_followup.recoverySnapshot().consecutive_early_resets == 0u);
    CHECK(decision.effective_wifi_mode == decision.requested_wifi_mode);
  }
  {
    // A configuration/source change gets exactly one clean trial.
    RuntimeSupervisor new_source = makeSupervisor(storage);
    const RuntimeSupervisorDecision decision = new_source.begin(boot(5u, 23u));
    CHECK(!new_source.recoverySnapshot().wifi_quarantined);
    CHECK(decision.effective_wifi_mode == decision.requested_wifi_mode);
  }
}

}  // namespace

int main() {
  verifySelectedProfile();
  verifyEarlyResetEvidenceDoesNotDisableWifi();
  verifyRuntimeContractIsolation();
  if (failures != 0) return 1;
  std::cout << "Runtime supervisor contract PASS profile="
            << static_cast<unsigned>(kResetExperimentProfile.selector) << '\n';
  return 0;
}
