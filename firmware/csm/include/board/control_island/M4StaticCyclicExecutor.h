#pragma once

#include <stdint.h>

#include "board/control_island/ControlIslandContract.h"

namespace csm::board::control_island {

class M4LaneDriver {
 public:
  virtual ~M4LaneDriver() = default;
  virtual bool ready() const = 0;
  virtual bool hardInhibitActive() const = 0;
  virtual uint8_t hardInputBits() const = 0;
  virtual bool errorPassive() const = 0;
  virtual bool busOff() const = 0;
  virtual bool request(uint8_t lane, const uint8_t data[8]) = 0;
  virtual bool cancel(uint8_t lane) = 0;
  virtual bool pending(uint8_t lane) const = 0;
  virtual FdcanRawSnapshot rawSnapshot() const = 0;
};

class M4StaticCyclicExecutor {
 public:
  void begin(uint32_t m4_boot_id, uint32_t publish_timeout_us,
             M4LaneDriver* driver);
  bool acceptSnapshot(const FinalControlSnapshotPayload& snapshot,
                      uint32_t observed_at_us);
  void onFiveMillisecondSlot(uint32_t now_us);
  void onTerminal(uint8_t lane, bool transmitted, bool cancelled);
  void onTrackingFault(uint8_t lane);
  void noteIpcIntegrityFailure();

  const ControlHealthPayload& health() const { return health_; }
  ControlHealthPayload healthForPublish(uint32_t now_us);
  bool hasActiveControl() const { return active_valid_; }

 private:
  void activateCandidate();
  void serviceTransition();
  void releaseLane(uint8_t lane);
  void cancelLane(uint8_t lane);
  void cancelAllPending();
  bool allLanesFree() const;
  bool globalExecutionAllowed(uint32_t now_us);
  void latchFault(uint8_t lane);
  void saturatingIncrement(uint32_t* value);

  M4LaneDriver* driver_ = nullptr;
  FinalControlSnapshotPayload active_ = {};
  FinalControlSnapshotPayload candidate_ = {};
  ControlHealthPayload health_ = {};
  LaneState lane_state_[kLaneCount] = {LaneState::Free, LaneState::Free,
                                       LaneState::Free};
  uint32_t last_publish_seen_us_ = 0;
  uint32_t publish_timeout_us_ = 0;
  uint8_t next_slot_ = 0;
  bool active_valid_ = false;
  bool candidate_valid_ = false;
  bool transition_pending_ = false;
  bool stale_latched_ = true;
};

}  // namespace csm::board::control_island
