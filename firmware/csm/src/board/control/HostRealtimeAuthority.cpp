#include "board/control/HostRealtimeAuthority.h"

#include "board/control_island/ControlIslandContract.h"
#include "protocol/ControlProtocol.h"

namespace csm::board::control {
namespace {
void incrementSaturating(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) ++(*value);
}
}  // namespace

bool HostRealtimeAuthority::begin(uint64_t boot_session_id,
                                  uint32_t liveness_timeout_ms) {
  configured_ = boot_session_id != 0u && liveness_timeout_ms != 0u &&
                liveness_timeout_ms < 0x80000000u;
  boot_session_id_ = boot_session_id;
  timeout_ms_ = configured_ ? liveness_timeout_ms : 0u;
  transaction_seen_ = false;
  last_transaction_command_id_ = 0u;
  activation_seen_ = false;
  last_activation_epoch_ = 0u;
  active_ = false;
  authority_epoch_ = 0u;
  arm_ms_ = 0u;
  realtime_sequence_seen_ = false;
  highest_rx_sequence_ = 0u;
  proof_sequence_ = 0u;
  last_echoed_proof_ = 0u;
  prearm_proof_valid_ = false;
  prearm_proof_ms_ = 0u;
  active_forward_valid_ = false;
  active_forward_ms_ = 0u;
  active_proof_valid_ = false;
  active_proof_ms_ = 0u;
  last_applied_generation_ = 0u;
  last_state_applied_ = false;
  max_forward_gap_ms_ = 0u;
  max_proof_ref_gap_ms_ = 0u;
  timeout_total_ = 0u;
  replay_total_ = 0u;
  proof_mismatch_total_ = 0u;
  proof_ok_total_ = 0u;
  proof_status_ = csm::kRealtimeProofStatusPreArm;
  proof_reason_ = csm::kRealtimeProofReasonOk;
  return configured_;
}

void HostRealtimeAuthority::resetTransactionEpoch() {
  transaction_seen_ = false;
  last_transaction_command_id_ = 0u;
  // A disconnected transaction lane cannot reset live UDP sequence truth.
  // While inactive, a new anchored TCP client may establish a new sender
  // sequence domain for PRE-ARM qualification.
  if (!active_) {
    realtime_sequence_seen_ = false;
    highest_rx_sequence_ = 0u;
    prearm_proof_valid_ = false;
    prearm_proof_ms_ = 0u;
    last_echoed_proof_ = 0u;
  }
}

bool HostRealtimeAuthority::consumeTransactionCommand(uint32_t command_id) {
  if (!configured_ || command_id == 0u ||
      (transaction_seen_ && !newer(last_transaction_command_id_, command_id))) {
    incrementSaturating(&replay_total_);
    return false;
  }
  transaction_seen_ = true;
  last_transaction_command_id_ = command_id;
  return true;
}

bool HostRealtimeAuthority::arm(uint32_t authority_epoch, uint32_t now_ms,
                                bool backend_ready, bool authority_allowed,
                                uint8_t* reason) {
  uint8_t result = csm::ControlReasonOk;
  if (!configured_ || !preArmQualified(now_ms)) {
    result = csm::ControlReasonHostProofRequired;
  } else if (!backend_ready) {
    result = csm::ControlReasonCanNotReady;
  } else if (!authority_allowed || active_) {
    result = csm::ControlReasonAuthorityDenied;
  } else if (authority_epoch == 0u ||
             (activation_seen_ &&
              !newer(last_activation_epoch_, authority_epoch))) {
    result = csm::ControlReasonReplay;
  }
  if (result == csm::ControlReasonOk) {
    active_ = true;
    authority_epoch_ = authority_epoch;
    activation_seen_ = true;
    last_activation_epoch_ = authority_epoch;
    arm_ms_ = now_ms;
    active_forward_valid_ = false;
    active_forward_ms_ = now_ms;
    active_proof_valid_ = false;
    active_proof_ms_ = now_ms;
    last_applied_generation_ = 0u;
    last_state_applied_ = false;
    proof_status_ = csm::kRealtimeProofStatusActive;
    proof_reason_ = csm::kRealtimeProofReasonOk;
  }
  if (reason != nullptr) *reason = result;
  return result == csm::ControlReasonOk;
}

void HostRealtimeAuthority::disarm() {
  active_ = false;
  authority_epoch_ = 0u;
  active_forward_valid_ = false;
  active_proof_valid_ = false;
  last_applied_generation_ = 0u;
  last_state_applied_ = false;
  proof_status_ = csm::kRealtimeProofStatusPreArm;
  proof_reason_ = csm::kRealtimeProofReasonOk;
}

bool HostRealtimeAuthority::update(uint32_t now_ms) {
  if (!active_) return false;
  const bool first_packet_expired =
      !active_forward_valid_ && now_ms - arm_ms_ > timeout_ms_;
  const bool forward_expired = active_forward_valid_ &&
      now_ms - active_forward_ms_ > timeout_ms_;
  const bool proof_expired = active_proof_valid_
      ? now_ms - active_proof_ms_ > timeout_ms_
      : now_ms - arm_ms_ > timeout_ms_;
  if (!first_packet_expired && !forward_expired && !proof_expired) return false;
  active_ = false;
  authority_epoch_ = 0u;
  active_forward_valid_ = false;
  active_proof_valid_ = false;
  last_state_applied_ = false;
  incrementSaturating(&timeout_total_);
  proof_status_ = csm::kRealtimeProofStatusExpired;
  proof_reason_ = csm::kRealtimeProofReasonLivenessExpired;
  return true;
}

HostRealtimeAdmission HostRealtimeAuthority::accept(
    const csm::HostRealtimeStateV1& state, uint32_t arrival_ms) {
  last_state_applied_ = false;
  if (!configured_ || state.boot_session_id != boot_session_id_) {
    issueProof(csm::kRealtimeProofStatusRejected,
               csm::kRealtimeProofReasonBootMismatch);
    return HostRealtimeAdmission::BootMismatch;
  }
  if (state.realtime_sequence == 0u ||
      (realtime_sequence_seen_ &&
       !newer(highest_rx_sequence_, state.realtime_sequence))) {
    incrementSaturating(&replay_total_);
    proof_reason_ = csm::kRealtimeProofReasonSequence;
    return HostRealtimeAdmission::DuplicateOrReordered;
  }
  realtime_sequence_seen_ = true;
  highest_rx_sequence_ = state.realtime_sequence;

  if (state.control_contract_id != csm::kHostControlStateContractId) {
    issueProof(csm::kRealtimeProofStatusRejected,
               csm::kRealtimeProofReasonContract);
    return HostRealtimeAdmission::ContractMismatch;
  }
  if ((state.valid_mask & csm::board::control_island::kAllLanePermitMask) !=
          csm::board::control_island::kAllLanePermitMask ||
      state.state_generation == 0u) {
    issueProof(csm::kRealtimeProofStatusRejected,
               csm::kRealtimeProofReasonState);
    return HostRealtimeAdmission::StateInvalid;
  }

  const bool active_packet =
      state.mode == csm::kHostRealtimeModeActive && active_ &&
      state.authority_epoch == authority_epoch_;
  const bool prearm_packet =
      state.mode == csm::kHostRealtimeModePreArm &&
      state.authority_epoch == 0u && !active_;
  if (!active_packet && !prearm_packet) {
    issueProof(active_ ? csm::kRealtimeProofStatusActive
                       : csm::kRealtimeProofStatusExpired,
               active_ ? csm::kRealtimeProofReasonAuthorityEpoch
                       : csm::kRealtimeProofReasonNotArmed);
    return HostRealtimeAdmission::AuthorityMismatch;
  }

  proof_reason_ = csm::kRealtimeProofReasonOk;
  observeProofRef(state.proof_ref, arrival_ms, active_packet);
  if (active_packet) {
    if (active_forward_valid_) {
      const uint32_t gap = arrival_ms - active_forward_ms_;
      if (gap > max_forward_gap_ms_) max_forward_gap_ms_ = gap;
    }
    active_forward_valid_ = true;
    active_forward_ms_ = arrival_ms;
    issueProof(csm::kRealtimeProofStatusActive,
               csm::kRealtimeProofReasonOk);
    return HostRealtimeAdmission::AcceptedActive;
  }

  issueProof(csm::kRealtimeProofStatusPreArm,
             csm::kRealtimeProofReasonOk);
  return HostRealtimeAdmission::AcceptedPreArm;
}

void HostRealtimeAuthority::noteStateApplied(uint32_t state_generation,
                                             bool applied) {
  last_state_applied_ = applied;
  if (applied) last_applied_generation_ = state_generation;
  if (!applied) proof_reason_ = csm::kRealtimeProofReasonState;
}

bool HostRealtimeAuthority::healthy(uint32_t now_ms) const {
  return configured_ && active_ && active_forward_valid_ &&
      active_proof_valid_ && now_ms - active_forward_ms_ <= timeout_ms_ &&
      now_ms - active_proof_ms_ <= timeout_ms_;
}

bool HostRealtimeAuthority::preArmQualified(uint32_t now_ms) const {
  return configured_ && !active_ && prearm_proof_valid_ &&
      now_ms - prearm_proof_ms_ <= timeout_ms_;
}

uint32_t HostRealtimeAuthority::forwardAgeMs(uint32_t now_ms) const {
  return active_forward_valid_ ? now_ms - active_forward_ms_ : UINT32_MAX;
}

uint32_t HostRealtimeAuthority::proofRefAgeMs(uint32_t now_ms) const {
  if (active_) return active_proof_valid_ ? now_ms - active_proof_ms_ : UINT32_MAX;
  return prearm_proof_valid_ ? now_ms - prearm_proof_ms_ : UINT32_MAX;
}

uint16_t HostRealtimeAuthority::proofFlags(uint32_t now_ms) const {
  uint16_t flags = 0u;
  if (active_forward_valid_ && now_ms - active_forward_ms_ <= timeout_ms_) {
    flags |= csm::kRealtimeProofFlagForwardFresh;
  }
  const bool return_fresh = active_
      ? active_proof_valid_ && now_ms - active_proof_ms_ <= timeout_ms_
      : prearm_proof_valid_ && now_ms - prearm_proof_ms_ <= timeout_ms_;
  if (return_fresh) flags |= csm::kRealtimeProofFlagReturnFresh;
  if (preArmQualified(now_ms)) flags |= csm::kRealtimeProofFlagPreArmQualified;
  if (active_) flags |= csm::kRealtimeProofFlagAuthorityActive;
  if (last_state_applied_) flags |= csm::kRealtimeProofFlagStateApplied;
  return flags;
}

bool HostRealtimeAuthority::newer(uint32_t previous, uint32_t current) {
  const uint32_t delta = current - previous;
  return delta != 0u && delta < 0x80000000u;
}

bool HostRealtimeAuthority::validProofRef(uint32_t proof_ref) const {
  if (proof_ref == 0u || proof_sequence_ == 0u ||
      proof_ref == last_echoed_proof_ ||
      (last_echoed_proof_ != 0u && !newer(last_echoed_proof_, proof_ref))) {
    return false;
  }
  // Candidate may equal the newest issued proof or trail it; it may never be
  // ahead in the wrap-safe sequence domain.
  return proof_ref == proof_sequence_ || newer(proof_ref, proof_sequence_);
}

void HostRealtimeAuthority::observeProofRef(uint32_t proof_ref,
                                             uint32_t now_ms,
                                             bool active_path) {
  if (!validProofRef(proof_ref)) {
    if (proof_ref != 0u && proof_ref != last_echoed_proof_) {
      incrementSaturating(&proof_mismatch_total_);
      proof_reason_ = csm::kRealtimeProofReasonProofRef;
    }
    return;
  }
  if (active_path) {
    if (active_proof_valid_) {
      const uint32_t gap = now_ms - active_proof_ms_;
      if (gap > max_proof_ref_gap_ms_) max_proof_ref_gap_ms_ = gap;
    }
    active_proof_valid_ = true;
    active_proof_ms_ = now_ms;
  } else {
    prearm_proof_valid_ = true;
    prearm_proof_ms_ = now_ms;
  }
  last_echoed_proof_ = proof_ref;
  incrementSaturating(&proof_ok_total_);
}

void HostRealtimeAuthority::issueProof(uint8_t status, uint8_t reason) {
  ++proof_sequence_;
  if (proof_sequence_ == 0u) ++proof_sequence_;
  proof_status_ = status;
  if (proof_reason_ != csm::kRealtimeProofReasonProofRef ||
      reason != csm::kRealtimeProofReasonOk) {
    proof_reason_ = reason;
  }
}

}  // namespace csm::board::control
