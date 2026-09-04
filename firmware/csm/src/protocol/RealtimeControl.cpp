#include "protocol/RealtimeControl.h"

#include <string.h>

namespace csm {

RealtimeFrameDecodeResult decode_host_realtime_datagram(
    const uint8_t* frame, size_t length, HostRealtimeStateV1* state) {
  const size_t expected = encoded_typed_frame_len(kHostRealtimeStateV1PayloadLen);
  if (frame == nullptr || state == nullptr || length != expected) {
    return RealtimeFrameDecodeResult::BadLength;
  }
  if (frame[0] != kFrameSof0 || frame[1] != kFrameSof1) {
    return RealtimeFrameDecodeResult::BadSof;
  }
  if (frame[2] != kProtocolVersion) {
    return RealtimeFrameDecodeResult::BadVersion;
  }
  if (frame[3] != static_cast<uint8_t>(RecordType::HostRealtimeStateV1)) {
    return RealtimeFrameDecodeResult::BadType;
  }
  if (rd_u16_le(frame + 7) != kHostRealtimeStateV1PayloadLen) {
    return RealtimeFrameDecodeResult::BadPayloadLength;
  }
  const uint16_t expected_crc = rd_u16_le(frame + length - 2);
  if (crc16_ccitt(frame + 2, length - 4) != expected_crc) {
    return RealtimeFrameDecodeResult::BadCrc;
  }
  const uint8_t* payload = frame + 9;
  if (payload[kHostRealtimeStateSchemaOffset] != kHostRealtimeStateSchema) {
    return RealtimeFrameDecodeResult::BadSchema;
  }
  state->mode = payload[kHostRealtimeStateModeOffset];
  state->flags = rd_u16_le(payload + kHostRealtimeStateFlagsOffset);
  state->boot_session_id =
      rd_u64_le(payload + kHostRealtimeStateBootSessionOffset);
  state->authority_epoch =
      rd_u32_le(payload + kHostRealtimeStateAuthorityEpochOffset);
  state->realtime_sequence =
      rd_u32_le(payload + kHostRealtimeStateSequenceOffset);
  state->state_generation =
      rd_u32_le(payload + kHostRealtimeStateGenerationOffset);
  state->proof_ref = rd_u32_le(payload + kHostRealtimeStateProofRefOffset);
  state->host_mono_ms =
      rd_u32_le(payload + kHostRealtimeStateHostMonoMsOffset);
  state->control_contract_id =
      rd_u32_le(payload + kHostRealtimeStateContractIdOffset);
  state->valid_mask = payload[kHostRealtimeStateValidMaskOffset];
  memcpy(state->data005, payload + kHostRealtimeStateData005Offset, 8);
  memcpy(state->data007, payload + kHostRealtimeStateData007Offset, 8);
  memcpy(state->data364, payload + kHostRealtimeStateData364Offset, 8);
  return RealtimeFrameDecodeResult::Accepted;
}

size_t encode_realtime_proof_v1(
    uint8_t* frame, size_t capacity, uint16_t frame_sequence,
    uint64_t mono_us, uint64_t boot_session_id, uint32_t authority_epoch,
    uint32_t proof_sequence, uint32_t highest_rx_sequence,
    uint32_t state_generation, uint8_t status, uint8_t reason,
    uint16_t flags) {
  uint8_t payload[kRealtimeProofV1PayloadLen] = {};
  wr_u64_le(payload + kRealtimeProofMonoUsOffset, mono_us);
  wr_u64_le(payload + kRealtimeProofBootSessionOffset, boot_session_id);
  wr_u32_le(payload + kRealtimeProofAuthorityEpochOffset, authority_epoch);
  wr_u32_le(payload + kRealtimeProofSequenceOffset, proof_sequence);
  wr_u32_le(payload + kRealtimeProofHighestRxSequenceOffset,
            highest_rx_sequence);
  wr_u32_le(payload + kRealtimeProofStateGenerationOffset,
            state_generation);
  payload[kRealtimeProofStatusOffset] = status;
  payload[kRealtimeProofReasonOffset] = reason;
  wr_u16_le(payload + kRealtimeProofFlagsOffset, flags);
  size_t written = 0;
  if (!encode_typed_frame(frame, capacity, RecordType::RealtimeProofV1,
                          payload, sizeof(payload), frame_sequence, 0,
                          &written)) {
    return 0;
  }
  return written;
}

}  // namespace csm
