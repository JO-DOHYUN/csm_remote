#pragma once

#include <stdint.h>

#include "board/control_island/ControlIslandContract.h"

namespace csm::board::control_island {

class M4LaneDriver {
 public:
  virtual ~M4LaneDriver() = default;
  // Physical transport availability only; it is deliberately independent of
  // source authority, permit, lease and motion permission.
  virtual void consumeLatchedEvents() {}
  virtual bool ready() const = 0;
  virtual bool errorWarning() const { return false; }
  virtual bool errorPassive() const = 0;
  virtual bool busOff() const = 0;
  virtual TxRequestResult request(uint8_t lane, const uint8_t data[8]) = 0;
  virtual bool cancel(uint8_t lane) = 0;
  virtual bool pending(uint8_t lane) const = 0;
  virtual FdcanRawSnapshot rawSnapshot() const = 0;
};

class M4StaticCyclicExecutor {
 public:
  void begin(uint32_t m4_boot_id, uint32_t publish_timeout_us,
             M4LaneDriver* driver);
  bool stageSnapshot(const FinalControlSnapshotPayload& snapshot,
                     uint32_t observed_at_us);
  void stageIpcIntegrityFailure();
  void latchTerminalEvent(uint8_t lane, bool transmitted, bool cancelled);
  void latchTrackingFault(uint8_t lane);
  void onFiveMillisecondSlot(uint32_t now_us);
  void markTimebaseConfigured(bool configured) { timebase_configured_ = configured; }

  const ControlHealthPayload& health() const { return health_; }
  uint32_t diagnosticTickTotal() const {
    // Aligned single-word read: TIM4 remains the sole writer.
    const volatile uint32_t* tick = &health_.tim4_tick_total;
    return *tick;
  }
  bool healthSnapshot(uint32_t now_us, ControlHealthPayload* result) const;
  bool hasActiveControl() const { return active_valid_; }

 private:
  void consumeIngress(uint32_t now_us);
  void consumeTerminalEvents();
  void applyStagedSnapshot(uint32_t now_us);
  void activateStaged();
  void releaseLane(uint8_t lane);
  void releaseSafeLane(uint8_t lane);
  void cancelLane(uint8_t lane);
  void cancelActivePending();
  bool activeMotionAllowed() const;
  bool snapshotHasActiveMotion(const FinalControlSnapshotPayload& snapshot) const;
  bool laneOwnedByActiveSource(uint8_t lane) const;
  void revokeActive(bool require_rearm);
  void latchFault(uint8_t lane);
  void publishCoherentHealth(uint32_t now_us);
  void saturatingIncrement(uint32_t* value);

  M4LaneDriver* driver_ = nullptr;
  FinalControlSnapshotPayload active_ = {};
  FinalControlSnapshotPayload staged_ = {};
  ControlHealthPayload health_ = {};
  LaneState lane_state_[kLaneCount] = {LaneState::Free, LaneState::Free,
                                       LaneState::Free};
  bool cancel_issued_[kLaneCount] = {};
  uint32_t last_publish_seen_us_ = 0;
  uint32_t publish_timeout_us_ = 0;
  uint8_t next_slot_ = 0;
  bool active_valid_ = false;
  bool activation_pending_ = false;
  volatile uint32_t staged_generation_ = 0;
  uint32_t consumed_staged_generation_ = 0;
  uint32_t staged_observed_at_us_ = 0;
  volatile uint32_t terminal_tx_mask_ = 0;
  volatile uint32_t terminal_cancel_mask_ = 0;
  volatile uint32_t tracking_fault_mask_ = 0;
  volatile uint32_t ipc_integrity_events_ = 0;
  bool stale_latched_ = true;
  bool rearm_required_ = true;
  bool tracking_fault_active_ = false;
  bool fault_closing_ = false;
  bool error_warning_seen_ = false;
  bool error_passive_seen_ = false;
  bool bus_off_seen_ = false;
  uint32_t rejected_activation_epoch_ = 0;
  volatile uint32_t health_write_sequence_ = 0;
  bool timebase_configured_ = false;
};

}  // namespace csm::board::control_island
