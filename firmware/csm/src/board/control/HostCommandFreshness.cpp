#include "board/control/HostCommandFreshness.h"

namespace csm::board::control {
namespace {

void incrementSaturating(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) {
    ++(*value);
  }
}

}  // namespace

bool HostCommandFreshness::begin(const HostCommandFreshnessConfig& config) {
  configured_ = config.proof_timeout_ms > 0u &&
                config.proof_timeout_ms < 0x80000000u;
  config_ = configured_ ? config : HostCommandFreshnessConfig{};
  proof_ok_total_ = 0u;
  proof_mismatch_total_ = 0u;
  proof_timeout_total_ = 0u;
  replay_total_ = 0u;
  max_proof_gap_ms_ = 0u;
  resetTransportEpoch();
  return configured_;
}

void HostCommandFreshness::resetTransportEpoch() {
  invalidateProof();
  consumed_valid_ = false;
  last_consumed_command_id_ = 0u;
  last_host_mono_ms_ = 0u;
}

void HostCommandFreshness::invalidateProof() {
  proof_qualified_ = false;
  pending_valid_ = false;
  pending_heartbeat_id_ = 0u;
  pending_since_ms_ = 0u;
  last_proven_heartbeat_id_ = 0u;
  last_proof_ms_ = 0u;
}

void HostCommandFreshness::update(uint32_t now_ms) {
  (void)expireProof(now_ms);
}

HostFreshnessResult HostCommandFreshness::acceptHeartbeat(
    uint32_t command_id, uint32_t ack_ref, uint32_t host_mono_ms,
    uint32_t arrival_ms) {
  if (!configured_) {
    return HostFreshnessResult::NotConfigured;
  }
  (void)expireProof(arrival_ms);
  const HostFreshnessResult consumed = consumeCommand(command_id);
  if (consumed != HostFreshnessResult::Accepted) {
    return consumed;
  }
  last_host_mono_ms_ = host_mono_ms;
  if (!pending_valid_) {
    pending_valid_ = true;
    pending_heartbeat_id_ = command_id;
    pending_since_ms_ = arrival_ms;
    return HostFreshnessResult::BootstrapAccepted;
  }
  if (ack_ref != pending_heartbeat_id_) {
    incrementSaturating(&proof_mismatch_total_);
    return HostFreshnessResult::ProofMismatch;
  }
  if (proof_qualified_) {
    const uint32_t gap = arrival_ms - last_proof_ms_;
    if (gap > max_proof_gap_ms_) {
      max_proof_gap_ms_ = gap;
    }
  }
  proof_qualified_ = true;
  last_proven_heartbeat_id_ = pending_heartbeat_id_;
  last_proof_ms_ = arrival_ms;
  pending_heartbeat_id_ = command_id;
  pending_since_ms_ = arrival_ms;
  incrementSaturating(&proof_ok_total_);
  return HostFreshnessResult::Accepted;
}

HostFreshnessResult HostCommandFreshness::acceptCommand(uint32_t command_id,
                                                        uint32_t arrival_ms) {
  const HostFreshnessResult consumed = consumeCommand(command_id);
  if (consumed != HostFreshnessResult::Accepted) {
    return consumed;
  }
  if (expireProof(arrival_ms)) {
    return HostFreshnessResult::ProofExpired;
  }
  return proofAlive(arrival_ms) ? HostFreshnessResult::Accepted
                                : HostFreshnessResult::ProofRequired;
}

HostFreshnessResult HostCommandFreshness::consumeCommand(uint32_t command_id) {
  if (!configured_) {
    return HostFreshnessResult::NotConfigured;
  }
  if (command_id == 0u ||
      (consumed_valid_ && !forward(last_consumed_command_id_, command_id))) {
    incrementSaturating(&replay_total_);
    return HostFreshnessResult::Replay;
  }
  consumed_valid_ = true;
  last_consumed_command_id_ = command_id;
  return HostFreshnessResult::Accepted;
}

bool HostCommandFreshness::proofAlive(uint32_t now_ms) const {
  return configured_ && proof_qualified_ && pending_valid_ &&
         now_ms - last_proof_ms_ <= config_.proof_timeout_ms;
}

bool HostCommandFreshness::forward(uint32_t previous, uint32_t current) {
  const uint32_t delta = current - previous;
  return delta != 0u && delta < 0x80000000u;
}

bool HostCommandFreshness::expireProof(uint32_t now_ms) {
  if (!pending_valid_ ||
      now_ms - pending_since_ms_ <= config_.proof_timeout_ms) {
    return false;
  }
  const bool was_qualified = proof_qualified_;
  invalidateProof();
  if (was_qualified) {
    incrementSaturating(&proof_timeout_total_);
  }
  return true;
}

}  // namespace csm::board::control
