#include "board/control/ControlReleaseSchedule.h"

namespace csm::board::control {
namespace {

bool timeReached(uint32_t now_ms, uint32_t release_ms) {
  return (now_ms - release_ms) < 0x80000000u;
}

}  // namespace

bool ControlReleaseSchedule::begin(uint32_t phase_ms,
                                   uint32_t drive_period_ms,
                                   uint32_t steering_period_ms) {
  if (drive_period_ms == 0 || steering_period_ms == 0 ||
      drive_period_ms >= 0x80000000u ||
      steering_period_ms >= 0x80000000u) {
    drive_ = {};
    steering_ = {};
    started_ = false;
    return false;
  }

  drive_ = {};
  steering_ = {};
  drive_.period_ms = drive_period_ms;
  drive_.next_release_ms = phase_ms;
  steering_.period_ms = steering_period_ms;
  steering_.next_release_ms = phase_ms;
  started_ = true;
  return true;
}

ControlReleaseBatch ControlReleaseSchedule::poll(uint32_t now_ms) {
  ControlReleaseBatch releases;
  if (!started_) return releases;
  releases.drive = pollLane(now_ms, &drive_);
  releases.steering = pollLane(now_ms, &steering_);
  return releases;
}

void ControlReleaseSchedule::noteDriveDispatch(uint32_t now_ms) {
  if (!started_) return;
  noteDispatch(now_ms, &drive_);
}

PeriodicRelease ControlReleaseSchedule::pollLane(uint32_t now_ms, Lane* lane) {
  PeriodicRelease release;
  if (lane == nullptr || !timeReached(now_ms, lane->next_release_ms)) {
    return release;
  }

  const uint32_t elapsed_ms = now_ms - lane->next_release_ms;
  const uint32_t elapsed_periods = elapsed_ms / lane->period_ms;
  const uint32_t reached_releases = elapsed_periods + 1u;
  release.scheduled_ms =
      lane->next_release_ms + elapsed_periods * lane->period_ms;
  lane->sequence += reached_releases;
  release.sequence = lane->sequence;
  lane->next_release_ms += reached_releases * lane->period_ms;

  // A late dispatch must not be followed by a catch-up burst at the next
  // absolute phase. Consume every reached deadline, retain the phase and
  // report the suppressed release as missed.
  if (lane->has_dispatch &&
      static_cast<uint32_t>(now_ms - lane->last_dispatch_ms) <
          lane->period_ms) {
    release.missed_releases = reached_releases;
    return release;
  }

  release.due = true;
  release.missed_releases = elapsed_periods;
  noteDispatch(now_ms, lane);
  return release;
}

void ControlReleaseSchedule::noteDispatch(uint32_t now_ms, Lane* lane) {
  if (lane == nullptr) return;
  lane->last_dispatch_ms = now_ms;
  lane->has_dispatch = true;
}

}  // namespace csm::board::control
