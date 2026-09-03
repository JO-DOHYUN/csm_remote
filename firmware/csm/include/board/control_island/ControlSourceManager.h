#pragma once

#include <stdint.h>

#include "board/control_island/ControlIslandContract.h"

namespace csm::board::control_island {

struct SourceImage {
  bool valid = false;
  uint32_t image_generation = 0;
  uint32_t lease_sequence = 0;
  LaneExecutionImage lanes[kLaneCount] = {};
  BoundedTxTransaction transaction = {};
};

class ControlSourceManager {
 public:
  void begin(uint32_t m7_boot_id);
  bool acceptHostState(uint32_t state_generation, uint8_t valid_mask,
                       const uint8_t data005[8], const uint8_t data007[8],
                       const uint8_t data364[8], uint32_t lease_sequence);
  bool acceptHostNShot(uint32_t transaction_id, uint8_t lane,
                       uint16_t successful_tx_count,
                       uint32_t payload_generation, const uint8_t data[8]);
  // Only a new Host transport epoch (or begin/M7 boot) may reopen the
  // transaction-id domain. Ordinary state replacement, DISARM and re-ARM do
  // not make an already consumed transaction reusable.
  void resetHostTransportEpoch();
  void renewHostLease(uint32_t lease_sequence);
  void clearHost();
  void updateRemote(uint32_t image_generation, uint32_t lease_sequence,
                    const LaneExecutionImage lanes[kLaneCount], bool valid);
  void clearRemote();
  void select(ControlSource source);
  FinalControlSnapshotPayload snapshot(uint32_t permit_mask,
                                       uint32_t activation_epoch) const;

  ControlSource activeSource() const { return active_source_; }
  uint32_t sourceEpoch() const { return source_epoch_; }
  const SourceImage& host() const { return host_; }
  const SourceImage& remote() const { return remote_; }

 private:
  static bool sequenceNewer(uint32_t candidate, uint32_t current);

  uint32_t m7_boot_id_ = 0;
  uint32_t source_epoch_ = 1;
  ControlSource active_source_ = ControlSource::None;
  SourceImage host_ = {};
  SourceImage remote_ = {};
  uint32_t last_host_transaction_id_ = 0;
  bool host_transaction_seen_ = false;
};

}  // namespace csm::board::control_island
