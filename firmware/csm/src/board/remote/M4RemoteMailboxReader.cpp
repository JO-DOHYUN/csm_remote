#include "board/remote/M4RemoteMailboxReader.h"

namespace csm::board::remote {
namespace {

constexpr uint16_t kDetailBadMagic = 1;
constexpr uint16_t kDetailBadVersion = 2;
constexpr uint16_t kDetailIntegrityFailed = 3;
constexpr uint16_t kDetailSampleNotUsable = 4;
constexpr uint16_t kDetailStale = 5;

}  // namespace

void M4RemoteMailboxReader::begin(uint32_t now_ms) {
  snapshot_ = {};
  snapshot_.received_m7_ms = now_ms;
  snapshot_.link_state = RemoteLinkState::NoFrame;
}

void M4RemoteMailboxReader::update(uint32_t now_ms, uint32_t stale_timeout_ms) {
  if (!snapshot_.sample_present) {
    snapshot_.link_state = RemoteLinkState::NoFrame;
    snapshot_.age_ms = 0;
    return;
  }

  snapshot_.age_ms = now_ms - snapshot_.received_m7_ms;
  if (snapshot_.age_ms > stale_timeout_ms) {
    reject(RemoteLinkState::Stale, kDetailStale);
  }
}

bool M4RemoteMailboxReader::updateFromSample(uint32_t now_ms,
                                             const RcSample& sample,
                                             bool integrity_ok) {
  uint16_t detail = 0;
  snapshot_.sample = sample;
  snapshot_.sample_present = true;
  snapshot_.integrity_ok = integrity_ok;
  snapshot_.received_m7_ms = now_ms;
  snapshot_.age_ms = 0;

  if (!validateSampleHeader(sample, &detail)) {
    reject(RemoteLinkState::ProtocolFault, detail);
    return false;
  }
  if (!integrity_ok) {
    reject(RemoteLinkState::Malformed, kDetailIntegrityFailed);
    return false;
  }
  if (!isUsableRcSampleState(sample.sample_state)) {
    reject(sample.sample_state == RcSampleState::Failsafe ? RemoteLinkState::Failsafe
                                                          : RemoteLinkState::Stale,
           kDetailSampleNotUsable);
    return false;
  }

  snapshot_.link_state = RemoteLinkState::Valid;
  snapshot_.reject_detail = 0;
  return true;
}

void M4RemoteMailboxReader::clear() {
  snapshot_ = {};
  snapshot_.link_state = RemoteLinkState::NoFrame;
}

bool M4RemoteMailboxReader::hasFreshUsableSample() const {
  return snapshot_.sample_present &&
         snapshot_.integrity_ok &&
         isUsableRemoteLink(snapshot_.link_state) &&
         isUsableRcSampleState(snapshot_.sample.sample_state);
}

bool M4RemoteMailboxReader::validateSampleHeader(const RcSample& sample,
                                                 uint16_t* detail) const {
  if (sample.magic != kRcSampleMagic) {
    if (detail != nullptr) *detail = kDetailBadMagic;
    return false;
  }
  if (sample.version != kRcSampleVersion) {
    if (detail != nullptr) *detail = kDetailBadVersion;
    return false;
  }
  return true;
}

void M4RemoteMailboxReader::reject(RemoteLinkState state, uint16_t detail) {
  snapshot_.link_state = state;
  snapshot_.reject_detail = detail;
}

}  // namespace csm::board::remote

