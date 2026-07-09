#pragma once

#include <stdint.h>

#include "board/remote/RemoteTypes.h"

namespace csm::board::remote {

static constexpr uint32_t kM4RemoteMailboxMagic = 0x344D4352u;  // "RCM4".
static constexpr uint8_t kM4RemoteMailboxVersion = 1;
static constexpr uint16_t kM4RemoteMailboxFrameBytes = 64;

enum class M4RemoteMailboxRejectDetail : uint16_t {
  None = 0,
  NoFrame = 1,
  TornWrite = 2,
  BadMagic = 3,
  BadVersion = 4,
  BadFrameSize = 5,
  BadSampleState = 6,
  CrcMismatch = 7,
  IntegrityFailed = 8,
  SampleNotUsable = 9,
  Stale = 10,
};

struct M4RemoteMailboxFrame {
  uint32_t sequence_begin = 0;
  uint32_t magic = kM4RemoteMailboxMagic;
  uint8_t version = kM4RemoteMailboxVersion;
  uint8_t sample_state = static_cast<uint8_t>(RcSampleState::Lost);
  uint16_t frame_size = kM4RemoteMailboxFrameBytes;
  uint16_t rc_seq = 0;
  uint16_t flags = 0;
  uint32_t m4_time_ms = 0;
  int16_t ch[kRcChannelCount] = {};
  uint16_t switch_bits = 0;
  uint8_t link_quality = kRemoteMetricUnknown;
  uint8_t rssi_hint = kRemoteMetricUnknown;
  uint16_t malformed_count = 0;
  uint16_t crc = 0;
  uint32_t sequence_end = 0;
};

static_assert(sizeof(M4RemoteMailboxFrame) == kM4RemoteMailboxFrameBytes,
              "M4RemoteMailboxFrame wire size must remain fixed");

struct M4RemoteMailboxDecodeResult {
  bool sample_present = false;
  bool integrity_ok = false;
  RemoteLinkState link_state = RemoteLinkState::NotConfigured;
  RcSample sample = {};
  uint16_t reject_detail = static_cast<uint16_t>(M4RemoteMailboxRejectDetail::NoFrame);
  uint32_t published_sequence = 0;
};

uint16_t computeM4RemoteMailboxCrc(const M4RemoteMailboxFrame& frame);
M4RemoteMailboxDecodeResult decodeM4RemoteMailboxFrame(
    const M4RemoteMailboxFrame& frame);

}  // namespace csm::board::remote
