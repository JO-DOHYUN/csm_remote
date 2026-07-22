#include "board/remote/M4RemoteMailboxReader.h"

namespace csm::board::remote {
namespace {

constexpr uint16_t kDetailBadMagic =
    static_cast<uint16_t>(M4RemoteMailboxRejectDetail::BadMagic);
constexpr uint16_t kDetailBadVersion =
    static_cast<uint16_t>(M4RemoteMailboxRejectDetail::BadVersion);
constexpr uint16_t kDetailIntegrityFailed =
    static_cast<uint16_t>(M4RemoteMailboxRejectDetail::IntegrityFailed);
constexpr uint16_t kDetailSampleNotUsable =
    static_cast<uint16_t>(M4RemoteMailboxRejectDetail::SampleNotUsable);
constexpr uint16_t kDetailStale =
    static_cast<uint16_t>(M4RemoteMailboxRejectDetail::Stale);

}  // namespace

void M4RemoteMailboxReader::begin(uint32_t now_ms) {
  snapshot_ = {};
  last_published_sequence_ = 0;
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
    reject(remoteLinkStateForRcSampleState(sample.sample_state), kDetailSampleNotUsable);
    return false;
  }

  snapshot_.link_state = RemoteLinkState::Valid;
  snapshot_.reject_detail = 0;
  return true;
}

bool M4RemoteMailboxReader::updateFromMailboxFrame(uint32_t now_ms,
                                                   const M4RemoteMailboxFrame& frame) {
  const M4RemoteMailboxDecodeResult decoded = decodeM4RemoteMailboxFrame(frame);
  if (!decoded.integrity_ok) {
    snapshot_.sample = decoded.sample;
    snapshot_.sample_present = decoded.sample_present;
    snapshot_.integrity_ok = false;
    snapshot_.received_m7_ms = now_ms;
    snapshot_.age_ms = 0;
    snapshot_.link_state = decoded.link_state;
    snapshot_.reject_detail = decoded.reject_detail;
    return false;
  }

  if (decoded.published_sequence == last_published_sequence_) {
    update(now_ms, kDefaultRcSampleStaleMs);
    return hasFreshUsableSample();
  }

  last_published_sequence_ = decoded.published_sequence;
  snapshot_.published_sequence = decoded.published_sequence;
  return updateFromSample(now_ms, decoded.sample, true);
}

void M4RemoteMailboxReader::clear() {
  snapshot_ = {};
  last_published_sequence_ = 0;
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
