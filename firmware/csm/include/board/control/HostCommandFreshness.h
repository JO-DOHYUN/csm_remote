#pragma once

#include <stdint.h>

namespace csm::board::control {

struct HostCommandFreshnessConfig {
  uint32_t proof_timeout_ms = 0;
};

enum class HostFreshnessResult : uint8_t {
  Accepted = 0,
  BootstrapAccepted = 1,
  ProofRequired = 2,
  ProofExpired = 3,
  ProofMismatch = 4,
  Replay = 5,
  NotConfigured = 6,
};

// Fixed-state causal ACK proof and replay protection. Host monotonic time is
// diagnostic-only; safety liveness is measured exclusively in the M7 domain.
class HostCommandFreshness {
 public:
  bool begin(const HostCommandFreshnessConfig& config);
  void resetTransportEpoch();
  void invalidateProof();
  void update(uint32_t now_ms);

  HostFreshnessResult acceptHeartbeat(uint32_t command_id,
                                      uint32_t ack_ref,
                                      uint32_t host_mono_ms,
                                      uint32_t arrival_ms);
  HostFreshnessResult acceptCommand(uint32_t command_id, uint32_t arrival_ms);
  HostFreshnessResult consumeCommand(uint32_t command_id);

  bool qualified() const { return proof_qualified_; }
  bool proofAlive(uint32_t now_ms) const;
  uint32_t proofTimeoutMs() const { return config_.proof_timeout_ms; }
  uint32_t proofOkTotal() const { return proof_ok_total_; }
  uint32_t proofMismatchTotal() const { return proof_mismatch_total_; }
  uint32_t proofTimeoutTotal() const { return proof_timeout_total_; }
  uint32_t replayTotal() const { return replay_total_; }
  uint32_t maxProofGapMs() const { return max_proof_gap_ms_; }
  uint32_t lastConsumedCommandId() const { return last_consumed_command_id_; }

 private:
  static bool forward(uint32_t previous, uint32_t current);
  bool expireProof(uint32_t now_ms);

  HostCommandFreshnessConfig config_ = {};
  bool configured_ = false;
  bool proof_qualified_ = false;
  bool pending_valid_ = false;
  bool consumed_valid_ = false;
  uint32_t pending_heartbeat_id_ = 0;
  uint32_t pending_since_ms_ = 0;
  uint32_t last_proven_heartbeat_id_ = 0;
  uint32_t last_proof_ms_ = 0;
  uint32_t last_consumed_command_id_ = 0;
  uint32_t last_host_mono_ms_ = 0;
  uint32_t proof_ok_total_ = 0;
  uint32_t proof_mismatch_total_ = 0;
  uint32_t proof_timeout_total_ = 0;
  uint32_t replay_total_ = 0;
  uint32_t max_proof_gap_ms_ = 0;
};

}  // namespace csm::board::control
