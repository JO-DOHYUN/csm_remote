#include "board/remote/M4RemoteMailboxContract.h"

namespace csm::board::remote {
namespace {

static_assert(kM4RemoteMailboxFrameBytes == 64,
              "mailbox frame size is part of the M4-M7 contract");

uint16_t crc16CcittUpdate(uint16_t crc, uint8_t byte) {
  crc ^= static_cast<uint16_t>(byte) << 8;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x8000u) ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                          : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

void crcAppendU8(uint16_t* crc, uint8_t value) {
  *crc = crc16CcittUpdate(*crc, value);
}

void crcAppendU16(uint16_t* crc, uint16_t value) {
  crcAppendU8(crc, static_cast<uint8_t>(value & 0xFFu));
  crcAppendU8(crc, static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void crcAppendI16(uint16_t* crc, int16_t value) {
  crcAppendU16(crc, static_cast<uint16_t>(value));
}

void crcAppendU32(uint16_t* crc, uint32_t value) {
  crcAppendU8(crc, static_cast<uint8_t>(value & 0xFFu));
  crcAppendU8(crc, static_cast<uint8_t>((value >> 8) & 0xFFu));
  crcAppendU8(crc, static_cast<uint8_t>((value >> 16) & 0xFFu));
  crcAppendU8(crc, static_cast<uint8_t>((value >> 24) & 0xFFu));
}

bool toRcSampleState(uint8_t raw, RcSampleState* state) {
  switch (static_cast<RcSampleState>(raw)) {
    case RcSampleState::Lost:
    case RcSampleState::Ok:
    case RcSampleState::Failsafe:
    case RcSampleState::Stale:
    case RcSampleState::CrcBad:
    case RcSampleState::ProtocolFault:
      if (state != nullptr) {
        *state = static_cast<RcSampleState>(raw);
      }
      return true;
  }
  return false;
}

M4RemoteMailboxDecodeResult reject(RemoteLinkState state,
                                   M4RemoteMailboxRejectDetail detail,
                                   uint32_t published_sequence = 0) {
  M4RemoteMailboxDecodeResult result;
  result.link_state = state;
  result.reject_detail = static_cast<uint16_t>(detail);
  result.published_sequence = published_sequence;
  return result;
}

}  // namespace

uint16_t computeM4RemoteMailboxCrc(const M4RemoteMailboxFrame& frame) {
  uint16_t crc = 0xFFFFu;

  crcAppendU32(&crc, frame.magic);
  crcAppendU8(&crc, frame.version);
  crcAppendU8(&crc, frame.sample_state);
  crcAppendU16(&crc, frame.frame_size);
  crcAppendU16(&crc, frame.rc_seq);
  crcAppendU16(&crc, frame.flags);
  crcAppendU32(&crc, frame.m4_time_ms);
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    crcAppendI16(&crc, frame.ch[i]);
  }
  crcAppendU16(&crc, frame.switch_bits);
  crcAppendU8(&crc, frame.link_quality);
  crcAppendU8(&crc, frame.rssi_hint);
  crcAppendU16(&crc, frame.malformed_count);

  return crc;
}

M4RemoteMailboxDecodeResult decodeM4RemoteMailboxFrame(
    const M4RemoteMailboxFrame& frame) {
  if (frame.sequence_begin == 0 && frame.sequence_end == 0) {
    return reject(RemoteLinkState::NoFrame, M4RemoteMailboxRejectDetail::NoFrame);
  }
  if (frame.sequence_begin != frame.sequence_end || (frame.sequence_begin & 1u) != 0) {
    return reject(RemoteLinkState::Malformed,
                  M4RemoteMailboxRejectDetail::TornWrite,
                  frame.sequence_begin);
  }
  if (frame.magic != kM4RemoteMailboxMagic) {
    return reject(RemoteLinkState::ProtocolFault,
                  M4RemoteMailboxRejectDetail::BadMagic,
                  frame.sequence_begin);
  }
  if (frame.version != kM4RemoteMailboxVersion) {
    return reject(RemoteLinkState::ProtocolFault,
                  M4RemoteMailboxRejectDetail::BadVersion,
                  frame.sequence_begin);
  }
  if (frame.frame_size != kM4RemoteMailboxFrameBytes) {
    return reject(RemoteLinkState::ProtocolFault,
                  M4RemoteMailboxRejectDetail::BadFrameSize,
                  frame.sequence_begin);
  }

  RcSampleState sample_state = RcSampleState::Lost;
  if (!toRcSampleState(frame.sample_state, &sample_state)) {
    return reject(RemoteLinkState::ProtocolFault,
                  M4RemoteMailboxRejectDetail::BadSampleState,
                  frame.sequence_begin);
  }

  if (frame.crc != computeM4RemoteMailboxCrc(frame)) {
    return reject(RemoteLinkState::Malformed,
                  M4RemoteMailboxRejectDetail::CrcMismatch,
                  frame.sequence_begin);
  }

  M4RemoteMailboxDecodeResult result;
  result.sample_present = true;
  result.integrity_ok = true;
  result.link_state = remoteLinkStateForRcSampleState(sample_state);
  result.reject_detail = static_cast<uint16_t>(M4RemoteMailboxRejectDetail::None);
  result.published_sequence = frame.sequence_begin;
  result.sample.magic = kRcSampleMagic;
  result.sample.version = kRcSampleVersion;
  result.sample.sample_state = sample_state;
  result.sample.seq = frame.rc_seq;
  result.sample.m4_time_ms = frame.m4_time_ms;
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    result.sample.ch[i] = frame.ch[i];
  }
  result.sample.switch_bits = frame.switch_bits;
  result.sample.link_quality = frame.link_quality;
  result.sample.rssi_hint = frame.rssi_hint;
  result.sample.malformed_count = frame.malformed_count;
  result.sample.flags = frame.flags;
  result.sample.crc = frame.crc;
  return result;
}

}  // namespace csm::board::remote
