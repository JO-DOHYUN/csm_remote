#pragma once

#include <stdint.h>

#include "board/remote/M4RemoteMailboxContract.h"

namespace csm::board::remote {

enum class M4RemoteMailboxWriteDetail : uint16_t {
  None = 0,
  NullFrame = 1,
  BadSampleMagic = 2,
  BadSampleVersion = 3,
  BadSampleState = 4,
  ChannelOutOfRange = 5,
};

struct M4RemoteMailboxWriteResult {
  bool accepted = false;
  uint16_t detail = static_cast<uint16_t>(M4RemoteMailboxWriteDetail::NullFrame);
  uint32_t published_sequence = 0;
};

class M4RemoteMailboxWriter {
 public:
  void reset();
  void clearFrame(M4RemoteMailboxFrame* frame) const;

  M4RemoteMailboxWriteResult publishSample(const RcSample& sample,
                                           M4RemoteMailboxFrame* frame);

  uint32_t lastPublishedSequence() const { return last_published_sequence_; }

 private:
  static bool isKnownSampleState(RcSampleState state);
  static bool hasValidChannelRange(const RcSample& sample);
  static M4RemoteMailboxWriteResult reject(M4RemoteMailboxWriteDetail detail);

  uint32_t nextEvenSequence();

  uint32_t last_published_sequence_ = 0;
};

}  // namespace csm::board::remote
