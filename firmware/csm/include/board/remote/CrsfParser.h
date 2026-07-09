#pragma once

#include <stdint.h>

#include "board/remote/RemoteTypes.h"

namespace csm::board::remote {

static constexpr uint8_t kCrsfMaxFrameBytes = 64;
static constexpr uint8_t kCrsfFrameOverheadBytes = 4;
static constexpr uint8_t kCrsfMaxPayloadBytes =
    kCrsfMaxFrameBytes - kCrsfFrameOverheadBytes;
static constexpr uint8_t kCrsfMinLengthField = 2;  // type + crc.
static constexpr uint8_t kCrsfMaxLengthField =
    kCrsfMaxFrameBytes - 2;  // excludes address + length.
static constexpr uint8_t kCrsfFrameTypeRcChannelsPacked = 0x16;
static constexpr uint8_t kCrsfRcChannelsPackedPayloadBytes = 22;
static constexpr uint16_t kCrsfRawChannelMax = 0x07FF;

enum class CrsfParseStatus : uint8_t {
  Waiting = 0,
  FrameReady = 1,
  RejectedLength = 2,
  RejectedCrc = 3,
};

enum class CrsfDecodeStatus : uint8_t {
  Ok = 0,
  WrongType = 1,
  BadLength = 2,
  NullOutput = 3,
};

struct CrsfFrame {
  uint8_t address = 0;
  uint8_t length = 0;
  uint8_t type = 0;
  uint8_t payload_len = 0;
  uint8_t payload[kCrsfMaxPayloadBytes] = {};
  uint8_t crc = 0;
};

struct CrsfParseResult {
  CrsfParseStatus status = CrsfParseStatus::Waiting;
  CrsfFrame frame = {};
  uint16_t malformed_total = 0;
};

struct CrsfRcChannels {
  uint16_t raw[kRcChannelCount] = {};
  uint8_t count = 0;
};

uint8_t computeCrsfFrameCrc(const uint8_t* type_and_payload, uint8_t len);
CrsfDecodeStatus decodeCrsfRcChannelsPacked(const CrsfFrame& frame,
                                            CrsfRcChannels* channels);

class CrsfParser {
 public:
  void reset();
  CrsfParseResult ingest(uint8_t byte);

  uint8_t bufferedBytes() const { return pos_; }
  uint16_t malformedTotal() const { return malformed_total_; }

 private:
  CrsfParseResult reject(CrsfParseStatus status);
  CrsfParseResult emitFrame();

  uint8_t buffer_[kCrsfMaxFrameBytes] = {};
  uint8_t pos_ = 0;
  uint8_t expected_total_ = 0;
  uint16_t malformed_total_ = 0;
};

}  // namespace csm::board::remote
