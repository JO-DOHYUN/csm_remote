#include "board/control_island/M4StaticCyclicExecutor.h"

#include <string.h>

namespace csm::board::control_island {

void M4StaticCyclicExecutor::begin(uint32_t m4_boot_id,
                                   uint32_t publish_timeout_us,
                                   M4LaneDriver* driver) {
  driver_ = driver;
  active_ = {};
  candidate_ = {};
  health_ = {};
  health_.m4_boot_id = m4_boot_id;
  publish_timeout_us_ = publish_timeout_us;
  last_publish_seen_us_ = 0;
  next_slot_ = 0;
  active_valid_ = false;
  candidate_valid_ = false;
  transition_pending_ = false;
  stale_latched_ = true;
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    lane_state_[lane] = LaneState::Free;
    health_.lanes[lane].state = static_cast<uint8_t>(LaneState::Free);
  }
}

bool M4StaticCyclicExecutor::acceptSnapshot(
    const FinalControlSnapshotPayload& snapshot, uint32_t observed_at_us) {
  if (snapshot.schema_id != kControlIslandSchemaId ||
      snapshot.wire_contract_id != kHno1WireContractId ||
      snapshot.memory_layout_id != kControlMemoryLayoutId ||
      snapshot.m7_boot_id == 0u || snapshot.publish_sequence == 0u) {
    saturatingIncrement(&health_.ipc_integrity_miss);
    return false;
  }
  last_publish_seen_us_ = observed_at_us;
  health_.m7_publish_sequence_seen = snapshot.publish_sequence;
  stale_latched_ = false;

  const bool authority_change = active_valid_ &&
      (snapshot.m7_boot_id != active_.m7_boot_id ||
       snapshot.authority_epoch != active_.authority_epoch ||
       snapshot.active_source != active_.active_source);
  if (!active_valid_ || authority_change) {
    candidate_ = snapshot;
    candidate_valid_ = true;
    transition_pending_ = active_valid_;
    if (transition_pending_) {
      cancelAllPending();
      serviceTransition();
    } else {
      activateCandidate();
    }
    return true;
  }

  const uint32_t previous_transaction = active_.transaction.transaction_id;
  // Same authority: coherent latest state replaces the previous image.
  active_ = snapshot;
  if (active_.transaction.active != 0u &&
      active_.transaction.transaction_id != 0u &&
      active_.transaction.transaction_id != previous_transaction) {
    health_.transaction_id = active_.transaction.transaction_id;
    health_.transaction_requested =
        active_.transaction.requested_success_count;
    health_.transaction_completed = 0;
    health_.transaction_state =
        static_cast<uint8_t>(TransactionState::Active);
  } else if (active_.transaction.active == 0u &&
             previous_transaction != 0u) {
    health_.transaction_id = 0;
    health_.transaction_requested = 0;
    health_.transaction_completed = 0;
    health_.transaction_state =
        static_cast<uint8_t>(TransactionState::None);
  }
  return true;
}

void M4StaticCyclicExecutor::onFiveMillisecondSlot(uint32_t now_us) {
  if (driver_ == nullptr) return;
  health_.current = driver_->rawSnapshot();
  if (transition_pending_) {
    serviceTransition();
    next_slot_ = static_cast<uint8_t>((next_slot_ + 1u) & 0x03u);
    return;
  }
  if (!globalExecutionAllowed(now_us)) {
    cancelAllPending();
    next_slot_ = static_cast<uint8_t>((next_slot_ + 1u) & 0x03u);
    return;
  }

  if (next_slot_ == 0u) {
    releaseLane(kLane005);
    releaseLane(kLane007);
    releaseLane(kLane364);
  } else {
    releaseLane(kLane005);
  }
  next_slot_ = static_cast<uint8_t>((next_slot_ + 1u) & 0x03u);
}

void M4StaticCyclicExecutor::onTerminal(uint8_t lane, bool transmitted,
                                        bool cancelled) {
  if (lane >= kLaneCount) return;
  if (lane_state_[lane] != LaneState::Pending &&
      lane_state_[lane] != LaneState::CancelRequested) {
    latchFault(lane);
    return;
  }
  if (!transmitted && !cancelled) {
    latchFault(lane);
    return;
  }
  if (transmitted) {
    saturatingIncrement(&health_.lanes[lane].tx_success);
    if (cancelled) {
      saturatingIncrement(&health_.lanes[lane].cancel_race_count);
    }
    if (active_.transaction.active != 0u &&
        active_.transaction.lane_index == lane &&
        active_.transaction.transaction_id != 0u &&
        health_.transaction_state ==
            static_cast<uint8_t>(TransactionState::Active)) {
      if (health_.transaction_completed <
          active_.transaction.requested_success_count) {
        ++health_.transaction_completed;
      }
      if (health_.transaction_completed >=
          active_.transaction.requested_success_count) {
        health_.transaction_state =
            static_cast<uint8_t>(TransactionState::Complete);
      }
    }
  } else {
    saturatingIncrement(&health_.lanes[lane].cancel_count);
  }
  lane_state_[lane] = LaneState::Free;
  health_.lanes[lane].state = static_cast<uint8_t>(LaneState::Free);
  serviceTransition();
}

void M4StaticCyclicExecutor::onTrackingFault(uint8_t lane) {
  if (lane < kLaneCount) latchFault(lane);
}

void M4StaticCyclicExecutor::noteIpcIntegrityFailure() {
  saturatingIncrement(&health_.ipc_integrity_miss);
}

ControlHealthPayload M4StaticCyclicExecutor::healthForPublish(uint32_t now_us) {
  health_.flags = 0;
  if (driver_ != nullptr && driver_->ready() && publish_timeout_us_ != 0u) {
    health_.flags |= kHealthFlagReady | kHealthFlagClockContractOk;
  }
  if (driver_ != nullptr && driver_->hardInhibitActive()) {
    health_.flags |= kHealthFlagHardInhibit;
    health_.hard_inhibit_state = 1u;
  } else {
    health_.hard_inhibit_state = 0u;
  }
  health_.hard_input_bits = driver_ == nullptr ? 0u : driver_->hardInputBits();
  if (driver_ != nullptr && driver_->busOff()) health_.flags |= kHealthFlagBusOff;
  if (driver_ != nullptr && driver_->errorPassive()) {
    health_.flags |= kHealthFlagErrorPassive;
  }
  if (publish_timeout_us_ != 0u && last_publish_seen_us_ != 0u &&
      !elapsedAtLeast(now_us, last_publish_seen_us_, publish_timeout_us_)) {
    health_.flags |= kHealthFlagM7Fresh;
  }
  if (active_valid_ && active_.permit_mask != 0u) {
    health_.flags |= kHealthFlagControlActive;
  }
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if (lane_state_[lane] == LaneState::Faulted) {
      health_.flags |= kHealthFlagTrackingFault;
    }
  }
  health_.m7_publish_age_local_ms = last_publish_seen_us_ == 0u
      ? UINT32_MAX
      : static_cast<uint32_t>(now_us - last_publish_seen_us_) / 1000u;
  health_.authority_epoch_seen = active_.authority_epoch;
  health_.active_source_seen = active_.active_source;
  health_.current = driver_ == nullptr ? FdcanRawSnapshot{}
                                       : driver_->rawSnapshot();
  return health_;
}

void M4StaticCyclicExecutor::activateCandidate() {
  if (!candidate_valid_) return;
  active_ = candidate_;
  candidate_valid_ = false;
  transition_pending_ = false;
  active_valid_ = true;
  health_.authority_epoch_seen = active_.authority_epoch;
  health_.active_source_seen = active_.active_source;
  if (active_.transaction.active != 0u &&
      active_.transaction.transaction_id != 0u &&
      active_.transaction.requested_success_count != 0u &&
      active_.transaction.lane_index < kLaneCount) {
    health_.transaction_id = active_.transaction.transaction_id;
    health_.transaction_requested =
        active_.transaction.requested_success_count;
    health_.transaction_completed = 0;
    health_.transaction_state =
        static_cast<uint8_t>(TransactionState::Active);
  } else {
    health_.transaction_id = 0;
    health_.transaction_requested = 0;
    health_.transaction_completed = 0;
    health_.transaction_state =
        static_cast<uint8_t>(TransactionState::None);
  }
}

void M4StaticCyclicExecutor::serviceTransition() {
  if (transition_pending_ && allLanesFree()) activateCandidate();
}

void M4StaticCyclicExecutor::releaseLane(uint8_t lane) {
  LaneHealth& lane_health = health_.lanes[lane];
  saturatingIncrement(&lane_health.release_due);
  if (lane_state_[lane] == LaneState::Pending) {
    saturatingIncrement(&lane_health.deadline_miss);
    cancelLane(lane);
    return;
  }
  if (lane_state_[lane] != LaneState::Free) {
    saturatingIncrement(&lane_health.suppressed);
    return;
  }
  if ((active_.permit_mask & (1u << lane)) == 0u ||
      active_.lanes[lane].valid == 0u) {
    saturatingIncrement(&lane_health.suppressed);
    return;
  }

  const uint8_t* data = active_.lanes[lane].data;
  if (active_.transaction.active != 0u &&
      active_.transaction.lane_index == lane) {
    if (health_.transaction_id != active_.transaction.transaction_id ||
        health_.transaction_state !=
            static_cast<uint8_t>(TransactionState::Active)) {
      saturatingIncrement(&lane_health.suppressed);
      return;
    }
    data = active_.transaction.data;
  }
  if (!driver_->request(lane, data)) {
    latchFault(lane);
    return;
  }
  lane_state_[lane] = LaneState::Pending;
  lane_health.state = static_cast<uint8_t>(LaneState::Pending);
  lane_health.last_value_generation = active_.lanes[lane].value_generation;
}

void M4StaticCyclicExecutor::cancelLane(uint8_t lane) {
  if (lane_state_[lane] != LaneState::Pending &&
      lane_state_[lane] != LaneState::CancelRequested) {
    return;
  }
  // One initial request plus one bounded API retry after a pending resnapshot.
  bool accepted = driver_->cancel(lane);
  if (!accepted && driver_->pending(lane)) accepted = driver_->cancel(lane);
  if (!accepted && driver_->pending(lane)) {
    latchFault(lane);
    return;
  }
  lane_state_[lane] = LaneState::CancelRequested;
  health_.lanes[lane].state =
      static_cast<uint8_t>(LaneState::CancelRequested);
}

void M4StaticCyclicExecutor::cancelAllPending() {
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) cancelLane(lane);
}

bool M4StaticCyclicExecutor::allLanesFree() const {
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if (lane_state_[lane] != LaneState::Free) return false;
  }
  return true;
}

bool M4StaticCyclicExecutor::globalExecutionAllowed(uint32_t now_us) {
  if (!active_valid_ || driver_ == nullptr || !driver_->ready() ||
      driver_->hardInhibitActive() || driver_->busOff() ||
      driver_->errorPassive() || publish_timeout_us_ == 0u ||
      active_.active_source == static_cast<uint32_t>(ControlSource::None)) {
    return false;
  }
  if (last_publish_seen_us_ == 0u ||
      elapsedAtLeast(now_us, last_publish_seen_us_, publish_timeout_us_)) {
    if (!stale_latched_) {
      stale_latched_ = true;
      saturatingIncrement(&health_.m7_stale_count);
    }
    return false;
  }
  stale_latched_ = false;
  return true;
}

void M4StaticCyclicExecutor::latchFault(uint8_t lane) {
  lane_state_[lane] = LaneState::Faulted;
  health_.lanes[lane].state = static_cast<uint8_t>(LaneState::Faulted);
  saturatingIncrement(&health_.lanes[lane].tracking_fault);
  if (health_.first_fault.psr == 0u && health_.first_fault.ecr == 0u &&
      health_.first_fault.ir == 0u) {
    health_.first_fault = driver_ == nullptr ? FdcanRawSnapshot{}
                                             : driver_->rawSnapshot();
  }
  health_.last_fault = driver_ == nullptr ? FdcanRawSnapshot{}
                                          : driver_->rawSnapshot();
  if (health_.transaction_state ==
      static_cast<uint8_t>(TransactionState::Active)) {
    health_.transaction_state =
        static_cast<uint8_t>(TransactionState::Faulted);
  }
}

void M4StaticCyclicExecutor::saturatingIncrement(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) ++(*value);
}

}  // namespace csm::board::control_island
