#pragma once

#include <stdint.h>

#include "board/remote/CrsfParser.h"
#include "board/remote/R16smReceiverProfile.h"
#include "board/remote/RemoteTypes.h"

namespace csm::board::remote {

static constexpr uint16_t kCrsfRawDefaultMin = 172;
static constexpr uint16_t kCrsfRawDefaultMid = 992;
static constexpr uint16_t kCrsfRawDefaultMax = 1811;
enum class RcNormalizeRejectDetail : uint16_t {
  None = 0,
  NotConfigured = 1,
  BadCalibration = 2,
  MissingChannels = 3,
  RawOutOfRange = 4,
};

struct RcChannelCalibration {
  uint16_t raw_min = kCrsfRawDefaultMin;
  uint16_t raw_mid = kCrsfRawDefaultMid;
  uint16_t raw_max = kCrsfRawDefaultMax;
};

struct RcNormalizerConfig {
  bool configured = false;
  RcChannelCalibration channel[kRcChannelCount] = {};
  int16_t deadband_permille = 0;
  uint16_t required_channel_mask = 0;
};

struct RcNormalizeResult {
  bool accepted = false;
  RcSample sample = {};
  uint16_t reject_detail = static_cast<uint16_t>(RcNormalizeRejectDetail::NotConfigured);
};

class RcNormalizer {
 public:
  bool configure(const RcNormalizerConfig& config);
  void clearConfig();

  RcNormalizeResult normalizeCrsfChannels(uint32_t now_ms,
                                          uint16_t seq,
                                          const CrsfRcChannels& channels,
                                          uint8_t link_quality,
                                          uint8_t rssi_hint,
                                          uint16_t flags) const;

  bool configured() const { return config_.configured; }

 private:
  static bool isValidConfig(const RcNormalizerConfig& config);
  bool isWithinRawRange(const CrsfRcChannels& channels) const;
  bool calibrationValid(uint8_t channel) const;
  bool rawWithinCalibration(uint8_t channel, uint16_t raw) const;
  int16_t normalizeRawChannel(uint8_t channel, uint16_t raw) const;

  RcNormalizerConfig config_ = {};
};

}  // namespace csm::board::remote
