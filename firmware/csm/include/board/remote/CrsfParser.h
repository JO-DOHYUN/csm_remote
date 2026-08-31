#pragma once

#include <stdint.h>

#include "board/remote/RemoteTypes.h"
#include "board/remote/R16smReceiverProfile.h"

namespace csm::board::remote {

static constexpr uint8_t kCrsfMaxFrameBytes = 64;
static constexpr uint8_t kCrsfFrameOverheadBytes = 4;
static constexpr uint8_t kCrsfMaxPayloadBytes =
    kCrsfMaxFrameBytes - kCrsfFrameOverheadBytes;
static constexpr uint8_t kCrsfMinLengthField = 2;  // type + crc.
static constexpr uint8_t kCrsfMaxLengthField =
    kCrsfMaxFrameBytes - 2;  // excludes address + length.
static constexpr uint8_t kCrsfFrameTypeRcChannelsPacked = 0x16;
static constexpr uint8_t kCrsfFrameTypeSubsetRcChannelsPacked = 0x17;
static constexpr uint8_t kCrsfFrameTypeHeartbeat = 0x0B;
static constexpr uint8_t kCrsfFrameTypeLinkStatistics = 0x14;
static constexpr uint8_t kCrsfFrameTypeLinkStatisticsRx = 0x1C;
static constexpr uint8_t kCrsfFrameTypeLinkStatisticsTx = 0x1D;
static constexpr uint8_t kCrsfFrameTypeFlightMode = 0x21;
static constexpr uint8_t kCrsfRcChannelsPackedPayloadBytes = 22;
static constexpr uint8_t kCrsfLinkStatisticsPayloadBytes = 10;
static constexpr uint16_t kCrsfRawChannelMax = 0x07FF;
static constexpr uint32_t kCrsfDefaultBaud = kR16smConfiguredBaud;
static constexpr uint32_t kCrsfInterByteTimeoutUs = 2000;

enum class CrsfParseStatus : uint8_t {
  Waiting = 0,
  FrameReady = 1,
  RejectedLength = 2,
  RejectedCrc = 3,
  RejectedAddress = 4,
};

enum class CrsfDecodeStatus : uint8_t {
  Ok = 0,
  WrongType = 1,
  BadLength = 2,
  NullOutput = 3,
  UnsupportedEncoding = 4,
  BadValue = 5,
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
  uint16_t valid_mask = 0;
  uint8_t count = 0;
};

enum class CrsfLinkStatisticsKind : uint8_t {
  None = 0,
  Legacy = 1,
  Receiver = 2,
  Transmitter = 3,
};

struct CrsfLinkStatistics {
  CrsfLinkStatisticsKind kind = CrsfLinkStatisticsKind::None;
  uint8_t uplink_rssi_ant1_dbm_magnitude = 0;
  uint8_t uplink_rssi_ant2_dbm_magnitude = 0;
  uint8_t uplink_link_quality = 0;
  int8_t uplink_snr_db = 0;
  uint8_t active_antenna = 0;
  uint8_t rf_profile = 0;
  uint8_t uplink_rf_power = 0;
  uint8_t downlink_rssi_dbm_magnitude = 0;
  uint8_t downlink_link_quality = 0;
  int8_t downlink_snr_db = 0;
};

uint8_t computeCrsfFrameCrc(const uint8_t* type_and_payload, uint8_t len);
CrsfDecodeStatus decodeCrsfRcChannelsPacked(const CrsfFrame& frame,
                                            CrsfRcChannels* channels);
CrsfDecodeStatus decodeCrsfSubsetRcChannelsPacked(
    const CrsfFrame& frame, CrsfRcChannels* channels);
CrsfDecodeStatus decodeCrsfRcChannels(const CrsfFrame& frame,
                                      CrsfRcChannels* channels);
CrsfDecodeStatus decodeCrsfLinkStatistics(const CrsfFrame& frame,
                                          CrsfLinkStatistics* statistics);
uint8_t buildCrsfBroadcastFrame(uint8_t frame_type,
                                const uint8_t* payload,
                                uint8_t payload_len,
                                uint8_t* output,
                                uint8_t output_capacity,
                                uint8_t sync_byte = 0xC8);

class CrsfParser {
 public:
  void reset();
  CrsfParseResult ingest(uint8_t byte);

  uint8_t bufferedBytes() const { return pos_; }
  uint16_t malformedTotal() const { return malformed_total_; }
  uint32_t rejectedAddressTotal() const { return rejected_address_total_; }
  uint32_t rejectedLengthTotal() const { return rejected_length_total_; }
  uint32_t rejectedCrcTotal() const { return rejected_crc_total_; }

 private:
  CrsfParseResult evaluate();
  void discardPrefix(uint8_t count);
  void noteReject(CrsfParseStatus status);

  uint8_t buffer_[kCrsfMaxFrameBytes] = {};
  uint8_t pos_ = 0;
  uint16_t malformed_total_ = 0;
  uint32_t rejected_address_total_ = 0;
  uint32_t rejected_length_total_ = 0;
  uint32_t rejected_crc_total_ = 0;
};

}  // namespace csm::board::remote
