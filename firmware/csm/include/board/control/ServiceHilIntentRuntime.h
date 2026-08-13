#pragma once

#include <stdint.h>

#include "board/control/CommandLimiter.h"
#include "board/control/ControlReleaseSchedule.h"
#include "board/control/VehicleCommandMapper.h"

namespace csm::board::control {

struct ServiceHilIntentResult {
  bool accepted = false;
  authority::ControlDecisionCode decision =
      authority::ControlDecisionCode::RejectedFramePolicy;
  uint16_t detail = 0;
};

static constexpr uint32_t kServiceHilDrivePeriodMs = 5;
static constexpr uint32_t kServiceHilSteeringPeriodMs = 20;
static constexpr uint32_t kServiceHilEhbPeriodMs = 20;
// Stagger EHB five milliseconds after steering so a 20 ms boundary never
// asks the three-slot FDCAN FIFO to accept drive, steering and brake together.
static constexpr uint32_t kServiceHilEhbPhaseOffsetMs = 5;
static constexpr uint32_t kServiceHilIntentFreshnessMs = 300;
static constexpr uint32_t kServiceHilEhbCanId = 0x364;
static constexpr uint8_t kServiceHilEhbNeutral = 0;
static constexpr uint8_t kServiceHilEhbMinimum = 1;
static constexpr uint8_t kServiceHilEhbMaximum = 150;
static constexpr uint8_t kServiceHilReleaseMaxFrames = 3;

struct ServiceHilReleaseItem {
  uint32_t command_id = 0;
  CanFrameRequest frame = {};
};

struct ServiceHilReleaseBatch {
  uint8_t count = 0;
  ServiceHilReleaseItem items[kServiceHilReleaseMaxFrames] = {};
  uint32_t drive_missed_releases = 0;
  uint32_t steering_missed_releases = 0;
  uint32_t ehb_missed_releases = 0;
  bool drive_stale = true;
  bool steering_stale = true;
  bool ehb_stale = true;
  authority::ControlDecisionCode decision =
      authority::ControlDecisionCode::Accepted;
  uint16_t detail = 0;
};

struct ServiceHilIntentRuntimeStatus {
  bool active = false;
  bool drive_present = false;
  bool steering_present = false;
  bool ehb_present = false;
  bool drive_stale = true;
  bool steering_stale = true;
  bool ehb_stale = true;
  uint32_t drive_missed_releases = 0;
  uint32_t steering_missed_releases = 0;
  uint32_t ehb_missed_releases = 0;
  uint32_t release_batches = 0;
  uint32_t released_frames = 0;
};

// Compatibility ingress for the current Android Service/HIL record. The wire
// carries a legacy 0x005/0x007-shaped operator intent. Admission only updates
// the latest per-axis semantic target; it never submits CAN. M7 owns limiter
// progression and the absolute 5/20 ms release timeline below, so TCP arrival
// batching cannot become a CAN clock.
class ServiceHilIntentRuntime {
 public:
  bool begin(uint32_t now_ms, uint8_t bus, uint16_t policy_id);
  bool activate(uint32_t now_ms);
  void reset(uint32_t now_ms);
  ServiceHilIntentResult accept(uint32_t now_ms, uint32_t command_id,
                                uint32_t can_id, uint8_t dlc,
                                const uint8_t data[8]);
  ServiceHilReleaseBatch poll(uint32_t now_ms);

  bool active() const { return status_.active; }
  const ServiceHilIntentRuntimeStatus& status() const { return status_; }

 private:
  bool decode(uint32_t can_id, uint8_t dlc, const uint8_t data[8],
              OperatorCommand* command) const;
  bool laneFresh(bool present, uint32_t last_update_ms,
                 uint32_t now_ms) const;
  void addRelease(const CanFrameRequest& frame, uint32_t command_id,
                  ServiceHilReleaseBatch* batch);

  uint8_t bus_ = authority::kAuthorityNoBus;
  uint16_t policy_id_ = 0;
  OperatorCommand requested_ = {};
  OperatorCommand limited_ = {};
  CommandLimiter limiter_ = {};
  VehicleCommandMapper mapper_ = {};
  ControlReleaseSchedule release_schedule_ = {};
  ServiceHilIntentRuntimeStatus status_ = {};
  uint32_t drive_command_id_ = 0;
  uint32_t steering_command_id_ = 0;
  uint32_t ehb_command_id_ = 0;
  uint32_t drive_updated_ms_ = 0;
  uint32_t steering_updated_ms_ = 0;
  uint32_t ehb_updated_ms_ = 0;
  uint8_t ehb_request_ = kServiceHilEhbNeutral;
  bool configured_ = false;
};

}  // namespace csm::board::control
