#pragma once

#include <stdint.h>

#include "protocol/RealtimeControl.h"

namespace csm::board::control {

enum class HostRealtimeAdmission : uint8_t {
  AcceptedPreArm = 0,
  AcceptedActive = 1,
  DuplicateOrReordered = 2,
  BootMismatch = 3,
  ContractMismatch = 4,
  StateInvalid = 5,
  AuthorityMismatch = 6,
};

// Sole M7 owner of Host transaction replay protection, explicit activation
// identity, and receiver-local bidirectional realtime liveness. It owns no
// vehicle semantics and no physical scheduler.
class HostRealtimeAuthority {
 public:
  bool begin(uint64_t boot_session_id, uint32_t liveness_timeout_ms);

  // A TCP transaction epoch resets only its command-ID domain. It never
  // resurrects or revokes an already accepted realtime authority epoch.
  void resetTransactionEpoch();
  bool consumeTransactionCommand(uint32_t command_id);

  bool arm(uint32_t authority_epoch, uint32_t now_ms,
           bool backend_ready, bool authority_allowed, uint8_t* reason);
  void bindM4(uint32_t m4_boot_id, uint32_t activation_epoch);
  bool m4AuthorityRevoked(uint32_t m4_boot_id,
                          uint32_t activation_epoch,
                          bool control_active,
                          uint32_t applied_generation);
  void disarm();
  bool update(uint32_t now_ms);

  HostRealtimeAdmission accept(const csm::HostRealtimeStateV1& state,
                               uint32_t arrival_ms);
  void noteStateApplied(uint32_t state_generation, bool applied);

  bool active() const { return active_; }
  bool healthy(uint32_t now_ms) const;
  bool preArmQualified(uint32_t now_ms) const;
  uint32_t authorityEpoch() const { return active_ ? authority_epoch_ : 0u; }
  uint32_t proofAuthorityEpoch() const { return proof_authority_epoch_; }
  uint32_t boundM4BootId() const { return bound_m4_boot_id_; }
  uint32_t boundM4ActivationEpoch() const {
    return active_ ? bound_m4_activation_epoch_ : 0u;
  }
  uint32_t proofSequence() const { return proof_sequence_; }
  uint32_t lastProofRef() const { return last_echoed_proof_; }
  uint32_t highestRxSequence() const { return highest_rx_sequence_; }
  uint32_t lastAppliedGeneration() const { return last_applied_generation_; }
  uint32_t forwardAgeMs(uint32_t now_ms) const;
  uint32_t proofRefAgeMs(uint32_t now_ms) const;
  uint32_t maxForwardGapMs() const { return max_forward_gap_ms_; }
  uint32_t maxProofRefGapMs() const { return max_proof_ref_gap_ms_; }
  uint32_t livenessTimeoutMs() const { return timeout_ms_; }
  uint32_t proofOkTotal() const { return proof_ok_total_; }
  uint32_t timeoutTotal() const { return timeout_total_; }
  uint32_t replayTotal() const { return replay_total_; }
  uint32_t proofMismatchTotal() const { return proof_mismatch_total_; }
  uint8_t proofStatus() const { return proof_status_; }
  uint8_t proofReason() const { return proof_reason_; }
  uint16_t proofFlags(uint32_t now_ms) const;

 private:
  static bool newer(uint32_t previous, uint32_t current);
  bool validProofRef(uint32_t proof_ref) const;
  void observeProofRef(uint32_t proof_ref, uint32_t now_ms, bool active_path);
  void issueProof(uint8_t status, uint8_t reason);
  void requireNewPreArmChallenge();
  void resetPreArmSequenceDomain();

  uint64_t boot_session_id_ = 0;
  uint32_t timeout_ms_ = 0;
  bool configured_ = false;
  bool transaction_seen_ = false;
  uint32_t last_transaction_command_id_ = 0;
  bool activation_seen_ = false;
  uint32_t last_activation_epoch_ = 0;
  bool active_ = false;
  uint32_t authority_epoch_ = 0;
  uint32_t proof_authority_epoch_ = 0;
  uint32_t bound_m4_boot_id_ = 0;
  uint32_t bound_m4_activation_epoch_ = 0;
  bool bound_m4_active_seen_ = false;
  uint32_t bound_m4_applied_generation_ = 0;
  uint32_t arm_ms_ = 0;
  bool realtime_sequence_seen_ = false;
  uint32_t highest_rx_sequence_ = 0;
  uint32_t proof_sequence_ = 0;
  uint32_t last_echoed_proof_ = 0;
  bool prearm_proof_valid_ = false;
  uint32_t prearm_proof_ms_ = 0;
  bool prearm_challenge_required_ = true;
  uint32_t prearm_challenge_sequence_ = 0;
  bool active_forward_valid_ = false;
  uint32_t active_forward_ms_ = 0;
  bool active_proof_valid_ = false;
  uint32_t active_proof_ms_ = 0;
  uint32_t last_applied_generation_ = 0;
  bool last_state_applied_ = false;
  uint32_t max_forward_gap_ms_ = 0;
  uint32_t max_proof_ref_gap_ms_ = 0;
  uint32_t timeout_total_ = 0;
  uint32_t replay_total_ = 0;
  uint32_t proof_mismatch_total_ = 0;
  uint32_t proof_ok_total_ = 0;
  uint8_t proof_status_ = csm::kRealtimeProofStatusPreArm;
  uint8_t proof_reason_ = csm::kRealtimeProofReasonOk;
};

}  // namespace csm::board::control
