#include "board/remote/M4RemoteMailboxWriter.h"

namespace csm::board::remote {
namespace {

void copySampleToFramePayload(const RcSample& sample, M4RemoteMailboxFrame* frame) {
  frame->magic = kM4RemoteMailboxMagic;
  frame->version = kM4RemoteMailboxVersion;
  frame->sample_state = static_cast<uint8_t>(sample.sample_state);
  frame->frame_size = kM4RemoteMailboxFrameBytes;
  frame->rc_seq = sample.seq;
  frame->flags = sample.flags;
  frame->m4_time_ms = sample.m4_time_ms;
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    frame->ch[i] = sample.ch[i];
  }
  frame->switch_bits = sample.switch_bits;
  frame->link_quality = sample.link_quality;
  frame->rssi_hint = sample.rssi_hint;
  frame->malformed_count = sample.malformed_count;
}

}  // namespace

void M4RemoteMailboxWriter::reset() {
  last_published_sequence_ = 0;
}

void M4RemoteMailboxWriter::clearFrame(M4RemoteMailboxFrame* frame) const {
  if (frame == nullptr) {
    return;
  }
  *frame = {};
}

M4RemoteMailboxWriteResult M4RemoteMailboxWriter::publishSample(
    const RcSample& sample,
    M4RemoteMailboxFrame* frame) {
  if (frame == nullptr) {
    return reject(M4RemoteMailboxWriteDetail::NullFrame);
  }
  if (sample.magic != kRcSampleMagic) {
    return reject(M4RemoteMailboxWriteDetail::BadSampleMagic);
  }
  if (sample.version != kRcSampleVersion) {
    return reject(M4RemoteMailboxWriteDetail::BadSampleVersion);
  }
  if (!isKnownSampleState(sample.sample_state)) {
    return reject(M4RemoteMailboxWriteDetail::BadSampleState);
  }
  if (!hasValidChannelRange(sample)) {
    return reject(M4RemoteMailboxWriteDetail::ChannelOutOfRange);
  }

  const uint32_t sequence = nextEvenSequence();
  frame->sequence_begin = sequence | 1u;
  frame->sequence_end = 0;
  frame->crc = 0;
  copySampleToFramePayload(sample, frame);
  frame->crc = computeM4RemoteMailboxCrc(*frame);
  frame->sequence_end = sequence;
  frame->sequence_begin = sequence;

  M4RemoteMailboxWriteResult result;
  result.accepted = true;
  result.detail = static_cast<uint16_t>(M4RemoteMailboxWriteDetail::None);
  result.published_sequence = sequence;
  return result;
}

bool M4RemoteMailboxWriter::isKnownSampleState(RcSampleState state) {
  switch (state) {
    case RcSampleState::Lost:
    case RcSampleState::Ok:
    case RcSampleState::Failsafe:
    case RcSampleState::Stale:
    case RcSampleState::CrcBad:
    case RcSampleState::ProtocolFault:
      return true;
  }
  return false;
}

bool M4RemoteMailboxWriter::hasValidChannelRange(const RcSample& sample) {
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    if (sample.ch[i] < kRcNormalizedChannelMin ||
        sample.ch[i] > kRcNormalizedChannelMax) {
      return false;
    }
  }
  return true;
}

M4RemoteMailboxWriteResult M4RemoteMailboxWriter::reject(
    M4RemoteMailboxWriteDetail detail) {
  M4RemoteMailboxWriteResult result;
  result.detail = static_cast<uint16_t>(detail);
  return result;
}

uint32_t M4RemoteMailboxWriter::nextEvenSequence() {
  last_published_sequence_ += 2u;
  if (last_published_sequence_ == 0) {
    last_published_sequence_ = 2u;
  }
  return last_published_sequence_;
}

}  // namespace csm::board::remote
