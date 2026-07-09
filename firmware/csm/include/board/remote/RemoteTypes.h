#pragma once

#include <stdint.h>

namespace csm::board::remote {

static constexpr uint16_t kRcSampleMagic = 0x4352;  // "RC" little-endian marker.
static constexpr uint8_t kRcSampleVersion = 1;
static constexpr uint8_t kRcChannelCount = 16;
static constexpr uint8_t kRemoteMetricUnknown = 0xFF;
static constexpr int16_t kRcNormalizedChannelMin = -1000;
static constexpr int16_t kRcNormalizedChannelMax = 1000;

enum class RemoteLinkState : uint8_t {
  NotConfigured = 0,
  NoFrame = 1,
  Searching = 2,
  Valid = 3,
  Stale = 4,
  Failsafe = 5,
  Malformed = 6,
  ProtocolFault = 7,
};

enum class RcSampleState : uint8_t {
  Lost = 0,
  Ok = 1,
  Failsafe = 2,
  Stale = 3,
  CrcBad = 4,
  ProtocolFault = 5,
};

struct RcSample {
  uint16_t magic = kRcSampleMagic;
  uint8_t version = kRcSampleVersion;
  RcSampleState sample_state = RcSampleState::Lost;
  uint16_t seq = 0;
  uint32_t m4_time_ms = 0;
  int16_t ch[kRcChannelCount] = {};
  uint16_t switch_bits = 0;
  uint8_t link_quality = kRemoteMetricUnknown;
  uint8_t rssi_hint = kRemoteMetricUnknown;
  uint16_t malformed_count = 0;
  uint16_t flags = 0;
  uint16_t crc = 0;
};

constexpr bool isUsableRemoteLink(RemoteLinkState state) {
  return state == RemoteLinkState::Valid;
}

constexpr bool isUsableRcSampleState(RcSampleState state) {
  return state == RcSampleState::Ok;
}

constexpr RemoteLinkState remoteLinkStateForRcSampleState(RcSampleState state) {
  switch (state) {
    case RcSampleState::Lost:
      return RemoteLinkState::Searching;
    case RcSampleState::Ok:
      return RemoteLinkState::Valid;
    case RcSampleState::Failsafe:
      return RemoteLinkState::Failsafe;
    case RcSampleState::Stale:
      return RemoteLinkState::Stale;
    case RcSampleState::CrcBad:
      return RemoteLinkState::Malformed;
    case RcSampleState::ProtocolFault:
      return RemoteLinkState::ProtocolFault;
  }
  return RemoteLinkState::ProtocolFault;
}

}  // namespace csm::board::remote
