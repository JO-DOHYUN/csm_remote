#include "board/control_island/M4StaticCyclicExecutor.h"

namespace csm::board::control_island {
namespace {
constexpr uint32_t laneBit(uint8_t lane) { return 1u << lane; }

void atomicAdd(volatile uint32_t* value, uint32_t increment) {
#if defined(_MSC_VER)
  *value += increment;
#else
  __atomic_fetch_add(value, increment, __ATOMIC_RELEASE);
#endif
}

void atomicOr(volatile uint32_t* value, uint32_t bits) {
#if defined(_MSC_VER)
  *value |= bits;
#else
  __atomic_fetch_or(value, bits, __ATOMIC_RELEASE);
#endif
}

uint32_t atomicExchangeZero(volatile uint32_t* value) {
#if defined(_MSC_VER)
  const uint32_t previous = *value;
  *value = 0u;
  return previous;
#else
  return __atomic_exchange_n(value, 0u, __ATOMIC_ACQ_REL);
#endif
}
}

void M4StaticCyclicExecutor::begin(uint32_t m4_boot_id,
                                   uint32_t publish_timeout_us,
                                   M4LaneDriver* driver) {
  driver_ = driver;
  active_ = {};
  staged_ = {};
  health_ = {};
  health_.m4_boot_id = m4_boot_id;
  publish_timeout_us_ = publish_timeout_us;
  last_publish_seen_us_ = 0;
  next_slot_ = 0;
  active_valid_ = false;
  activation_pending_ = false;
  staged_generation_ = consumed_staged_generation_ = 0;
  terminal_tx_mask_ = terminal_cancel_mask_ = tracking_fault_mask_ = 0;
  ipc_integrity_events_ = 0;
  stale_latched_ = true;
  rearm_required_ = true;
  tracking_fault_active_ = false;
  fault_closing_ = false;
  error_warning_seen_ = error_passive_seen_ = bus_off_seen_ = false;
  rejected_activation_epoch_ = 0;
  health_write_sequence_ = 0;
  timebase_configured_ = false;
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    lane_state_[lane] = LaneState::Free;
    cancel_issued_[lane] = false;
    health_.lanes[lane].state = static_cast<uint8_t>(LaneState::Free);
  }
}

bool M4StaticCyclicExecutor::stageSnapshot(
    const FinalControlSnapshotPayload& snapshot, uint32_t observed_at_us) {
  if (snapshot.schema_id != kControlIslandSchemaId ||
      snapshot.wire_contract_id != kHno1WireContractId ||
      snapshot.memory_layout_id != kControlMemoryLayoutId ||
      snapshot.m7_boot_id == 0u || snapshot.publish_sequence == 0u) {
    stageIpcIntegrityFailure();
    return false;
  }
  staged_ = snapshot;
  staged_observed_at_us_ = observed_at_us;
  uint32_t next = staged_generation_ + 1u;
  staged_generation_ = next == 0u ? 1u : next;
  return true;
}

void M4StaticCyclicExecutor::stageIpcIntegrityFailure() {
  atomicAdd(&ipc_integrity_events_, 1u);
}

void M4StaticCyclicExecutor::latchTerminalEvent(
    uint8_t lane, bool transmitted, bool cancelled) {
  if (lane >= kLaneCount) return;
  if (transmitted) atomicOr(&terminal_tx_mask_, laneBit(lane));
  if (cancelled) atomicOr(&terminal_cancel_mask_, laneBit(lane));
}

void M4StaticCyclicExecutor::latchTrackingFault(uint8_t lane) {
  if (lane < kLaneCount) {
    atomicOr(&tracking_fault_mask_, laneBit(lane));
  }
}

void M4StaticCyclicExecutor::onFiveMillisecondSlot(uint32_t now_us) {
  if (driver_ == nullptr) return;
  ++health_write_sequence_;
  saturatingIncrement(&health_.tim4_tick_total);
  if (health_.tim4_first_tick_us == 0u) health_.tim4_first_tick_us = now_us;
  if (health_.tim4_last_tick_us != 0u) {
    const uint32_t gap = now_us - health_.tim4_last_tick_us;
    if (gap > health_.tim4_max_gap_us) health_.tim4_max_gap_us = gap;
  }
  health_.tim4_last_tick_us = now_us;
  driver_->consumeLatchedEvents();
  const bool warning = driver_->errorWarning();
  const bool passive = driver_->errorPassive();
  const bool bus_off = driver_->busOff();
  if (warning && !error_warning_seen_) saturatingIncrement(&health_.error_warning_count);
  if (passive && !error_passive_seen_) saturatingIncrement(&health_.error_passive_count);
  if (bus_off && !bus_off_seen_) saturatingIncrement(&health_.bus_off_count);
  error_warning_seen_ = warning;
  error_passive_seen_ = passive;
  bus_off_seen_ = bus_off;
  health_.fdcan_state = static_cast<uint8_t>(driver_->rawSnapshot().hal_state);
  consumeIngress(now_us);
  if (driver_->busOff() || !driver_->ready()) revokeActive(true);
  if (driver_->errorPassive()) revokeActive(true);
  if (active_valid_ &&
      (last_publish_seen_us_ == 0u || publish_timeout_us_ == 0u ||
       elapsedAtLeast(now_us, last_publish_seen_us_, publish_timeout_us_))) {
    if (!stale_latched_) {
      stale_latched_ = true;
      saturatingIncrement(&health_.m7_stale_count);
    }
    revokeActive(true);
  }
  if (activation_pending_) activateStaged();
  if (next_slot_ == 0u) {
    releaseLane(kLane005);
    releaseLane(kLane007);
    releaseLane(kLane364);
  } else {
    releaseLane(kLane005);
  }
  next_slot_ = static_cast<uint8_t>((next_slot_ + 1u) & 3u);
  ++health_write_sequence_;
}

void M4StaticCyclicExecutor::consumeIngress(uint32_t now_us) {
  consumeTerminalEvents();
  const uint32_t integrity = atomicExchangeZero(&ipc_integrity_events_);
  if (integrity != 0u) {
    const uint32_t room = UINT32_MAX - health_.ipc_integrity_miss;
    health_.ipc_integrity_miss += integrity > room ? room : integrity;
    revokeActive(true);
  }
  const uint32_t faults = atomicExchangeZero(&tracking_fault_mask_);
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if ((faults & laneBit(lane)) != 0u) latchFault(lane);
  }
  if (staged_generation_ != consumed_staged_generation_) {
    consumed_staged_generation_ = staged_generation_;
    applyStagedSnapshot(now_us);
  }
}

void M4StaticCyclicExecutor::consumeTerminalEvents() {
  const uint32_t transmitted = atomicExchangeZero(&terminal_tx_mask_);
  const uint32_t cancelled = atomicExchangeZero(&terminal_cancel_mask_);
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    const uint32_t bit = laneBit(lane);
    if (((transmitted | cancelled) & bit) == 0u) continue;
    const LaneState kind = lane_state_[lane];
    if (kind == LaneState::Free) continue;
    if ((transmitted & bit) != 0u) {
      saturatingIncrement(&health_.lanes[lane].tx_success);
      if ((cancelled & bit) != 0u) {
        saturatingIncrement(&health_.lanes[lane].cancel_race_count);
      }
      if (kind == LaneState::PendingActive &&
          active_.transaction.active != 0u &&
          active_.transaction.lane_index == lane &&
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
    cancel_issued_[lane] = false;
    health_.lanes[lane].state = static_cast<uint8_t>(LaneState::Free);
  }
}

void M4StaticCyclicExecutor::applyStagedSnapshot(uint32_t) {
  if (last_publish_seen_us_ != 0u) {
    const uint32_t gap_ms =
        static_cast<uint32_t>(staged_observed_at_us_ - last_publish_seen_us_) /
        1000u;
    if (gap_ms > health_.m7_publish_max_gap_local_ms) {
      health_.m7_publish_max_gap_local_ms = gap_ms;
    }
  }
  last_publish_seen_us_ = staged_observed_at_us_;
  health_.m7_publish_sequence_seen = staged_.publish_sequence;
  stale_latched_ = false;
  if (!snapshotHasActiveMotion(staged_)) {
    revokeActive(true);
    return;
  }
  if (rearm_required_ &&
      staged_.activation_epoch <= rejected_activation_epoch_) return;
  const bool source_change = active_valid_ &&
      (staged_.m7_boot_id != active_.m7_boot_id ||
       staged_.source_epoch != active_.source_epoch ||
       staged_.active_source != active_.active_source);
  if (source_change) revokeActive(false);
  activation_pending_ = true;
}

void M4StaticCyclicExecutor::activateStaged() {
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if (lane_state_[lane] == LaneState::PendingActive) return;
  }
  if (!snapshotHasActiveMotion(staged_) ||
      (rearm_required_ &&
       staged_.activation_epoch <= rejected_activation_epoch_)) {
    activation_pending_ = false;
    return;
  }
  const uint32_t previous_transaction = active_.transaction.transaction_id;
  active_ = staged_;
  active_valid_ = true;
  activation_pending_ = false;
  rearm_required_ = false;
  tracking_fault_active_ = false;
  health_.source_epoch_seen = active_.source_epoch;
  health_.activation_epoch_seen = active_.activation_epoch;
  health_.active_source_seen = active_.active_source;
  if (active_.transaction.active != 0u &&
      active_.transaction.transaction_id != 0u &&
      active_.transaction.transaction_id != previous_transaction) {
    health_.transaction_id = active_.transaction.transaction_id;
    health_.transaction_requested = active_.transaction.requested_success_count;
    health_.transaction_completed = 0;
    health_.transaction_state = static_cast<uint8_t>(TransactionState::Active);
  } else if (active_.transaction.active == 0u) {
    health_.transaction_id = 0;
    health_.transaction_requested = 0;
    health_.transaction_completed = 0;
    health_.transaction_state = static_cast<uint8_t>(TransactionState::None);
  }
}

void M4StaticCyclicExecutor::releaseLane(uint8_t lane) {
  LaneHealth& health = health_.lanes[lane];
  saturatingIncrement(&health.schedule_due);
  if (lane_state_[lane] != LaneState::Free) {
    saturatingIncrement(&health.pending_blocked);
    cancelLane(lane);
    return;
  }
  if (driver_->pending(lane)) {
    saturatingIncrement(&health.pending_blocked);
    return;
  }
  if (!activeMotionAllowed() || !laneOwnedByActiveSource(lane)) {
    releaseSafeLane(lane);
    return;
  }
  const uint8_t* data = active_.lanes[lane].data;
  if (active_.transaction.active != 0u &&
      active_.transaction.lane_index == lane) {
    if (health_.transaction_id != active_.transaction.transaction_id ||
        health_.transaction_state !=
            static_cast<uint8_t>(TransactionState::Active)) {
      saturatingIncrement(&health.policy_suppressed);
      return;
    }
    data = active_.transaction.data;
  }
  if (!driver_->ready()) {
    saturatingIncrement(&health.transport_blocked);
    return;
  }
  saturatingIncrement(&health.request_attempt);
  const TxRequestResult result = driver_->request(lane, data);
  if (result != TxRequestResult::Accepted) {
    saturatingIncrement(&health.request_failed);
    if (result == TxRequestResult::EnableFailedAbortPending) {
      lane_state_[lane] = LaneState::PendingActive;
      cancel_issued_[lane] = true;
      health.state = static_cast<uint8_t>(LaneState::PendingActive);
      health.last_value_generation = active_.lanes[lane].value_generation;
      return;
    }
    if (result == TxRequestResult::AlreadyPending ||
        result == TxRequestResult::EnableFailedAbortFailed) latchFault(lane);
    return;
  }
  saturatingIncrement(&health.request_accepted);
  lane_state_[lane] = LaneState::PendingActive;
  cancel_issued_[lane] = false;
  health.state = static_cast<uint8_t>(LaneState::PendingActive);
  health.last_value_generation = active_.lanes[lane].value_generation;
}

void M4StaticCyclicExecutor::releaseSafeLane(uint8_t lane) {
  LaneHealth& health = health_.lanes[lane];
  const SafeWireFrame& safe = kLaneSafeWirePolicies[lane].idle_safe;
  if (safe.action != SafeWireAction::FixedSafeFrame) {
    saturatingIncrement(&health.policy_suppressed);
    return;
  }
  if (!driver_->ready()) {
    saturatingIncrement(&health.transport_blocked);
    return;
  }
  saturatingIncrement(&health.request_attempt);
  const TxRequestResult result = driver_->request(lane, safe.data);
  if (result != TxRequestResult::Accepted) {
    saturatingIncrement(&health.request_failed);
    if (result == TxRequestResult::EnableFailedAbortPending) {
      lane_state_[lane] = LaneState::PendingSafe;
      cancel_issued_[lane] = true;
      health.state = static_cast<uint8_t>(LaneState::PendingSafe);
      health.last_value_generation = 0u;
      return;
    }
    if (result == TxRequestResult::AlreadyPending ||
        result == TxRequestResult::EnableFailedAbortFailed) latchFault(lane);
    return;
  }
  saturatingIncrement(&health.request_accepted);
  lane_state_[lane] = LaneState::PendingSafe;
  cancel_issued_[lane] = false;
  health.state = static_cast<uint8_t>(LaneState::PendingSafe);
  health.last_value_generation = 0u;
}

void M4StaticCyclicExecutor::cancelLane(uint8_t lane) {
  if (lane_state_[lane] == LaneState::Free) return;
  if (!driver_->pending(lane)) {
    lane_state_[lane] = LaneState::Free;
    cancel_issued_[lane] = false;
    health_.lanes[lane].state = static_cast<uint8_t>(LaneState::Free);
    return;
  }
  if (cancel_issued_[lane]) return;
  cancel_issued_[lane] = true;
  if (!driver_->cancel(lane) && driver_->pending(lane) && !fault_closing_) {
    latchFault(lane);
  }
}

void M4StaticCyclicExecutor::cancelActivePending() {
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if (lane_state_[lane] == LaneState::PendingActive) cancelLane(lane);
  }
}

bool M4StaticCyclicExecutor::activeMotionAllowed() const {
  return active_valid_ && !rearm_required_ && !tracking_fault_active_ &&
      driver_ != nullptr && driver_->ready() && !driver_->errorPassive() &&
      !driver_->busOff();
}

bool M4StaticCyclicExecutor::snapshotHasActiveMotion(
    const FinalControlSnapshotPayload& snapshot) const {
  uint32_t owned = 0u;
  if (snapshot.active_source == static_cast<uint32_t>(ControlSource::Host)) {
    owned = kAllLanePermitMask;
  } else if (snapshot.active_source ==
             static_cast<uint32_t>(ControlSource::Remote)) {
    owned = laneBit(kLane005) | laneBit(kLane007);
  } else {
    return false;
  }
  if (snapshot.activation_epoch == 0u ||
      (snapshot.permit_mask & owned) != owned) return false;
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if ((owned & laneBit(lane)) != 0u && snapshot.lanes[lane].valid == 0u) {
      return false;
    }
  }
  return true;
}

bool M4StaticCyclicExecutor::laneOwnedByActiveSource(uint8_t lane) const {
  if (lane >= kLaneCount || (active_.permit_mask & laneBit(lane)) == 0u ||
      active_.lanes[lane].valid == 0u) return false;
  if (active_.active_source == static_cast<uint32_t>(ControlSource::Host)) {
    return true;
  }
  return active_.active_source == static_cast<uint32_t>(ControlSource::Remote) &&
      (lane == kLane005 || lane == kLane007);
}

void M4StaticCyclicExecutor::revokeActive(bool require_rearm) {
  uint32_t rejected = active_.activation_epoch;
  if (activation_pending_ && staged_.activation_epoch > rejected) {
    rejected = staged_.activation_epoch;
  }
  if (require_rearm && rejected != 0u) {
    rearm_required_ = true;
    if (rejected > rejected_activation_epoch_) rejected_activation_epoch_ = rejected;
  }
  active_valid_ = false;
  activation_pending_ = false;
  cancelActivePending();
  if (health_.transaction_state == static_cast<uint8_t>(TransactionState::Active)) {
    health_.transaction_state = static_cast<uint8_t>(TransactionState::Cancelled);
  }
}

void M4StaticCyclicExecutor::latchFault(uint8_t lane) {
  if (lane >= kLaneCount) return;
  const bool transaction_active =
      health_.transaction_state == static_cast<uint8_t>(TransactionState::Active);
  saturatingIncrement(&health_.lanes[lane].tracking_fault);
  tracking_fault_active_ = true;
  if (health_.first_fault.psr == 0u && health_.first_fault.ecr == 0u &&
      health_.first_fault.ir == 0u) {
    health_.first_fault = driver_ == nullptr ? FdcanRawSnapshot{}
                                             : driver_->rawSnapshot();
  }
  health_.last_fault = driver_ == nullptr ? FdcanRawSnapshot{}
                                          : driver_->rawSnapshot();
  fault_closing_ = true;
  revokeActive(true);
  fault_closing_ = false;
  if (transaction_active) {
    health_.transaction_state = static_cast<uint8_t>(TransactionState::Faulted);
  }
}

bool M4StaticCyclicExecutor::healthSnapshot(
    uint32_t now_us, ControlHealthPayload* output) const {
  if (output == nullptr) return false;
  ControlHealthPayload result;
  bool coherent = false;
  for (uint8_t attempt = 0; attempt < 3u; ++attempt) {
    const uint32_t begin = health_write_sequence_;
    if ((begin & 1u) != 0u) continue;
    result = health_;
    const uint32_t end = health_write_sequence_;
    if (begin == end && (end & 1u) == 0u) {
      coherent = true;
      break;
    }
  }
  if (!coherent) return false;
  result.flags = timebase_configured_ ? kHealthFlagTim4Configured : 0u;
  if (driver_ != nullptr && driver_->ready()) {
    result.flags |= kHealthFlagReady | kHealthFlagClockContractOk |
                    kHealthFlagTransportReady;
  }
  if (result.tim4_tick_total != 0u) result.flags |= kHealthFlagTim4Ticking;
  if (driver_ != nullptr && driver_->busOff()) result.flags |= kHealthFlagBusOff;
  if (driver_ != nullptr && driver_->errorPassive()) {
    result.flags |= kHealthFlagErrorPassive;
  }
  if (last_publish_seen_us_ != 0u && publish_timeout_us_ != 0u &&
      !elapsedAtLeast(now_us, last_publish_seen_us_, publish_timeout_us_)) {
    result.flags |= kHealthFlagM7Fresh;
  }
  if (activeMotionAllowed()) {
    result.flags |= kHealthFlagControlActive | kHealthFlagActiveMotion;
  }
  if (tracking_fault_active_) result.flags |= kHealthFlagTrackingFault;
  result.m7_publish_age_local_ms = last_publish_seen_us_ == 0u
      ? UINT32_MAX
      : static_cast<uint32_t>(now_us - last_publish_seen_us_) / 1000u;
  result.current = driver_ == nullptr ? FdcanRawSnapshot{}
                                      : driver_->rawSnapshot();
  *output = result;
  return true;
}

void M4StaticCyclicExecutor::saturatingIncrement(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) ++(*value);
}

}  // namespace csm::board::control_island
