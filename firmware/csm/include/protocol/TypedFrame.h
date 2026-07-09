#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace csm {

static constexpr uint8_t kFrameSof0 = 0xA5;
static constexpr uint8_t kFrameSof1 = 0x5A;
static constexpr uint8_t kProtocolVersion = 1;
static constexpr uint16_t kMaxPayloadLen = 512;

enum class RecordType : uint8_t {
  CanRxRaw = 1,
  CanTxRaw = 2,
  EncEdgeRaw = 3,
  EncDerived = 4,
  AdcSample = 5,
  ControlAck = 6,
  BoardEvent = 7,
  BoardHealth = 8,
  Capability = 9,
  HostCanTxRequest = 10,
  HostHeartbeat = 11,
  HostControlSession = 12,
  HostSetControlPolicy = 13,
  HostQueryCapability = 14,
  HostClearFaultLockout = 15,
  CanRxSegment = 16,
};

void wr_u16_le(uint8_t* p, uint16_t v);
void wr_u32_le(uint8_t* p, uint32_t v);
void wr_i32_le(uint8_t* p, int32_t v);
void wr_u64_le(uint8_t* p, uint64_t v);
void wr_i64_le(uint8_t* p, int64_t v);

uint16_t rd_u16_le(const uint8_t* p);
uint32_t rd_u32_le(const uint8_t* p);

uint16_t crc16_ccitt(const uint8_t* data, size_t len);
uint64_t mono64_us();

static constexpr size_t kTypedFrameOverheadLen = 11;
constexpr size_t encoded_typed_frame_len(uint16_t payload_len) {
  return kTypedFrameOverheadLen + static_cast<size_t>(payload_len);
}
bool encode_typed_frame(uint8_t* frame, size_t capacity, RecordType type,
                        const uint8_t* payload, uint16_t len, uint16_t seq,
                        uint8_t flags, size_t* written);

bool emit_typed_record(Stream& serial, RecordType type, const uint8_t* payload,
                       uint16_t len, uint16_t& seq, uint8_t flags = 0);

}  // namespace csm
