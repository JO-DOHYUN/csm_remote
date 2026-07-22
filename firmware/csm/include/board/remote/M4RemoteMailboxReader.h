#pragma once

#include <stdint.h>

#include "board/remote/M4RemoteMailboxContract.h"
#include "board/remote/RemoteTypes.h"

namespace csm::board::remote {

static constexpr uint32_t kDefaultRcSampleStaleMs = 100;

struct M4RemoteMailboxSnapshot {
  RcSample sample = {};
  RemoteLinkState link_state = RemoteLinkState::NotConfigured;
  bool sample_present = false;
  bool integrity_ok = false;
  uint32_t received_m7_ms = 0;
  uint32_t age_ms = 0;
  uint32_t published_sequence = 0;
  uint16_t reject_detail = 0;
};

class M4RemoteMailboxReader {
 public:
  void begin(uint32_t now_ms);
  void update(uint32_t now_ms, uint32_t stale_timeout_ms = kDefaultRcSampleStaleMs);

  bool updateFromSample(uint32_t now_ms, const RcSample& sample, bool integrity_ok);
  bool updateFromMailboxFrame(uint32_t now_ms, const M4RemoteMailboxFrame& frame);
  void clear();

  const M4RemoteMailboxSnapshot& snapshot() const { return snapshot_; }
  RemoteLinkState linkState() const { return snapshot_.link_state; }
  bool hasFreshUsableSample() const;

 private:
  bool validateSampleHeader(const RcSample& sample, uint16_t* detail) const;
  void reject(RemoteLinkState state, uint16_t detail);

  M4RemoteMailboxSnapshot snapshot_ = {};
  uint32_t last_published_sequence_ = 0;
};

}  // namespace csm::board::remote
