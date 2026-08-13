#pragma once

#include <stdint.h>

namespace csm::board::control {

struct PeriodicRelease {
  bool due = false;
  uint32_t scheduled_ms = 0;
  uint32_t sequence = 0;
  uint32_t missed_releases = 0;
};

struct ControlReleaseBatch {
  PeriodicRelease drive = {};
  PeriodicRelease steering = {};
  PeriodicRelease brake = {};
};

class ControlReleaseSchedule {
 public:
  // Wrap-safe comparisons require at least one poll within each 2^31 ms span.
  bool begin(uint32_t phase_ms, uint32_t drive_period_ms,
             uint32_t steering_period_ms, uint32_t brake_period_ms = 0,
             uint32_t brake_phase_offset_ms = 0);
  ControlReleaseBatch poll(uint32_t now_ms);
  // An asynchronous safety frame participates in the drive-lane spacing
  // contract without re-anchoring its absolute periodic phase.
  void noteDriveDispatch(uint32_t now_ms);

 private:
  struct Lane {
    uint32_t period_ms = 0;
    uint32_t next_release_ms = 0;
    uint32_t sequence = 0;
    uint32_t last_dispatch_ms = 0;
    bool has_dispatch = false;
  };

  static PeriodicRelease pollLane(uint32_t now_ms, Lane* lane);
  static void noteDispatch(uint32_t now_ms, Lane* lane);

  Lane drive_ = {};
  Lane steering_ = {};
  Lane brake_ = {};
  bool started_ = false;
};

}  // namespace csm::board::control
