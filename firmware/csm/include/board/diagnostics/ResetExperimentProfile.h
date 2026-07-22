#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef BOARD_RESET_EXPERIMENT_PROFILE
#define BOARD_RESET_EXPERIMENT_PROFILE 0
#endif

namespace csm::board::diagnostics {

enum class ResetExperimentWifiRuntimeMode : uint8_t {
  Off = 0,
  ApOnly = 1,
  Full = 2,
};

struct ResetExperimentProfile {
  uint8_t selector;
  bool watchdog_enabled;
  ResetExperimentWifiRuntimeMode wifi_runtime_mode;
  const char* label;
};

// This table is the sole selector-to-runtime mapping. PlatformIO profiles set
// only BOARD_RESET_EXPERIMENT_PROFILE; runtime code consumes the selected row.
constexpr ResetExperimentProfile kResetExperimentProfiles[] = {
    {0u, true, ResetExperimentWifiRuntimeMode::Full, "REF"},
    {1u, true, ResetExperimentWifiRuntimeMode::Off, "A"},
    {2u, false, ResetExperimentWifiRuntimeMode::Full, "B"},
    {3u, true, ResetExperimentWifiRuntimeMode::ApOnly, "C"},
};

constexpr size_t kResetExperimentProfileCount =
    sizeof(kResetExperimentProfiles) / sizeof(kResetExperimentProfiles[0]);

static_assert(BOARD_RESET_EXPERIMENT_PROFILE >= 0,
              "BOARD_RESET_EXPERIMENT_PROFILE must be in [0, 3]");
static_assert(
    static_cast<size_t>(BOARD_RESET_EXPERIMENT_PROFILE) <
        kResetExperimentProfileCount,
    "BOARD_RESET_EXPERIMENT_PROFILE must be in [0, 3]");

static constexpr const ResetExperimentProfile& kResetExperimentProfile =
    kResetExperimentProfiles[BOARD_RESET_EXPERIMENT_PROFILE];
constexpr bool kResetExperimentWatchdogEnabled =
    kResetExperimentProfile.watchdog_enabled;
constexpr uint8_t kResetExperimentWifiRuntimeMode =
    static_cast<uint8_t>(kResetExperimentProfile.wifi_runtime_mode);
constexpr const char* kResetExperimentProfileLabel =
    kResetExperimentProfile.label;

static_assert(kResetExperimentProfiles[0].selector == 0u &&
                  kResetExperimentProfiles[1].selector == 1u &&
                  kResetExperimentProfiles[2].selector == 2u &&
                  kResetExperimentProfiles[3].selector == 3u,
              "reset experiment table must be ordered by selector");
static_assert(static_cast<uint8_t>(ResetExperimentWifiRuntimeMode::Off) == 0u &&
                  static_cast<uint8_t>(
                      ResetExperimentWifiRuntimeMode::ApOnly) == 1u &&
                  static_cast<uint8_t>(ResetExperimentWifiRuntimeMode::Full) ==
                      2u,
              "Wi-Fi runtime mode wire values are fixed at 0/1/2");

}  // namespace csm::board::diagnostics
