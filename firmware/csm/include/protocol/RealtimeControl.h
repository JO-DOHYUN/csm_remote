#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace csm {

struct HostRealtimeStateV1 {
  uint8_t mode = kHostRealtimeModePreArm;
  uint16_t flags = 0;
  uint64_t boot_session_id = 0;
  uint32_t authority_epoch = 0;
  uint32_t realtime_sequence = 0;
  uint32_t state_generation = 0;
  uint32_t proof_ref = 0;
  uint32_t host_mono_ms = 0;
  uint32_t control_contract_id = 0;
  uint8_t valid_mask = 0;
  uint8_t data005[8] = {};
  uint8_t data007[8] = {};
  uint8_t data364[8] = {};
};

enum class RealtimeFrameDecodeResult : uint8_t {
  Accepted = 0,
  BadLength = 1,
  BadSof = 2,
  BadVersion = 3,
  BadType = 4,
  BadPayloadLength = 5,
  BadCrc = 6,
  BadSchema = 7,
};

RealtimeFrameDecodeResult decode_host_realtime_datagram(
    const uint8_t* frame, size_t length, HostRealtimeStateV1* state);

size_t encode_realtime_proof_v1(
    uint8_t* frame, size_t capacity, uint16_t frame_sequence,
    uint64_t mono_us, uint64_t boot_session_id, uint32_t authority_epoch,
    uint32_t proof_sequence, uint32_t highest_rx_sequence,
    uint32_t state_generation, uint8_t status, uint8_t reason,
    uint16_t flags);

}  // namespace csm
