#include "board/control_island/ControlSourceManager.h"

#include <string.h>

namespace csm::board::control_island {

void ControlSourceManager::begin(uint32_t m7_boot_id) {
  m7_boot_id_ = m7_boot_id == 0u ? 1u : m7_boot_id;
  authority_epoch_ = 1u;
  active_source_ = ControlSource::None;
  host_ = {};
  remote_ = {};
}

bool ControlSourceManager::acceptHostState(
    uint32_t state_generation, uint8_t valid_mask, const uint8_t data005[8],
    const uint8_t data007[8], const uint8_t data364[8],
    uint32_t lease_sequence) {
  if (state_generation == 0u || data005 == nullptr || data007 == nullptr ||
      data364 == nullptr || (valid_mask & kAllLanePermitMask) !=
          kAllLanePermitMask ||
      (host_.image_generation != 0u &&
       !sequenceNewer(state_generation, host_.image_generation))) {
    return false;
  }
  host_.valid = true;
  host_.image_generation = state_generation;
  host_.lease_sequence = lease_sequence;
  const uint8_t* source[kLaneCount] = {data005, data007, data364};
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    host_.lanes[lane].value_generation = state_generation;
    host_.lanes[lane].valid = 1u;
    memcpy(host_.lanes[lane].data, source[lane], 8u);
  }
  // A coherent state replacement cancels any previous event primitive. A new
  // strict-N transaction must be explicitly admitted after this state.
  host_.transaction = {};
  return true;
}

bool ControlSourceManager::acceptHostNShot(
    uint32_t transaction_id, uint8_t lane, uint16_t successful_tx_count,
    uint32_t payload_generation, const uint8_t data[8]) {
  if (!host_.valid || transaction_id == 0u || lane >= kLaneCount ||
      successful_tx_count == 0u || payload_generation == 0u || data == nullptr ||
      (host_.transaction.active != 0u &&
       !sequenceNewer(transaction_id, host_.transaction.transaction_id))) {
    return false;
  }
  host_.transaction = {};
  host_.transaction.transaction_id = transaction_id;
  host_.transaction.payload_generation = payload_generation;
  host_.transaction.requested_success_count = successful_tx_count;
  host_.transaction.lane_index = lane;
  host_.transaction.active = 1u;
  memcpy(host_.transaction.data, data, 8u);
  return true;
}

void ControlSourceManager::renewHostLease(uint32_t lease_sequence) {
  if (host_.valid && lease_sequence != 0u) {
    host_.lease_sequence = lease_sequence;
  }
}

void ControlSourceManager::clearHost() {
  host_ = {};
  if (active_source_ == ControlSource::Host) select(ControlSource::None);
}

void ControlSourceManager::updateRemote(
    uint32_t image_generation, uint32_t lease_sequence,
    const LaneExecutionImage lanes[kLaneCount], bool valid) {
  if (!valid || lanes == nullptr || image_generation == 0u) {
    clearRemote();
    return;
  }
  remote_.valid = true;
  remote_.image_generation = image_generation;
  remote_.lease_sequence = lease_sequence;
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    remote_.lanes[lane] = lanes[lane];
  }
  remote_.transaction = {};
}

void ControlSourceManager::clearRemote() {
  remote_ = {};
  if (active_source_ == ControlSource::Remote) select(ControlSource::None);
}

void ControlSourceManager::select(ControlSource source) {
  if ((source == ControlSource::Host && !host_.valid) ||
      (source == ControlSource::Remote && !remote_.valid)) {
    source = ControlSource::None;
  }
  if (active_source_ == source) return;
  active_source_ = source;
  ++authority_epoch_;
  if (authority_epoch_ == 0u) authority_epoch_ = 1u;
}

FinalControlSnapshotPayload ControlSourceManager::snapshot(
    uint32_t permit_mask, uint32_t safety_epoch) const {
  FinalControlSnapshotPayload result;
  result.m7_boot_id = m7_boot_id_;
  result.authority_epoch = authority_epoch_;
  result.safety_epoch = safety_epoch;
  result.active_source = static_cast<uint32_t>(active_source_);
  const SourceImage* source = nullptr;
  if (active_source_ == ControlSource::Host && host_.valid) source = &host_;
  if (active_source_ == ControlSource::Remote && remote_.valid) source = &remote_;
  if (source == nullptr) return result;
  result.source_image_generation = source->image_generation;
  result.source_lease_sequence = source->lease_sequence;
  result.permit_mask = permit_mask & kAllLanePermitMask;
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    result.lanes[lane] = source->lanes[lane];
  }
  result.transaction = source->transaction;
  return result;
}

bool ControlSourceManager::sequenceNewer(uint32_t candidate,
                                         uint32_t current) {
  const uint32_t delta = candidate - current;
  return delta != 0u && delta < 0x80000000u;
}

}  // namespace csm::board::control_island
