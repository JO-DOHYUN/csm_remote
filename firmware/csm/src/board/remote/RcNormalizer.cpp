#include "board/remote/RcNormalizer.h"

namespace csm::board::remote {
namespace {

int16_t clampPermille(int32_t value) {
  if (value > kRcNormalizedChannelMax) {
    return kRcNormalizedChannelMax;
  }
  if (value < kRcNormalizedChannelMin) {
    return kRcNormalizedChannelMin;
  }
  return static_cast<int16_t>(value);
}

uint16_t absolutePermille(int16_t value) {
  return static_cast<uint16_t>(value < 0 ? -value : value);
}

RcNormalizeResult reject(RcNormalizeRejectDetail detail) {
  RcNormalizeResult result;
  result.reject_detail = static_cast<uint16_t>(detail);
  result.sample.sample_state = detail == RcNormalizeRejectDetail::NotConfigured
                                   ? RcSampleState::Lost
                                   : RcSampleState::ProtocolFault;
  return result;
}

}  // namespace

bool RcNormalizer::configure(const RcNormalizerConfig& config) {
  if (!isValidConfig(config)) {
    config_ = {};
    return false;
  }
  config_ = config;
  config_.configured = true;
  return true;
}

void RcNormalizer::clearConfig() {
  config_ = {};
}

RcNormalizeResult RcNormalizer::normalizeCrsfChannels(uint32_t now_ms,
                                                      uint16_t seq,
                                                      const CrsfRcChannels& channels,
                                                      uint8_t link_quality,
                                                      uint8_t rssi_hint,
                                                      uint16_t flags) const {
  if (!config_.configured) {
    return reject(RcNormalizeRejectDetail::NotConfigured);
  }
  if ((channels.valid_mask & config_.required_channel_mask) !=
      config_.required_channel_mask) {
    return reject(RcNormalizeRejectDetail::MissingChannels);
  }
  if (!isWithinRawRange(channels)) {
    return reject(RcNormalizeRejectDetail::RawOutOfRange);
  }

  RcNormalizeResult result;
  result.accepted = true;
  result.reject_detail = static_cast<uint16_t>(RcNormalizeRejectDetail::None);
  result.sample.magic = kRcSampleMagic;
  result.sample.version = kRcSampleVersion;
  result.sample.sample_state = RcSampleState::Ok;
  result.sample.seq = seq;
  result.sample.m4_time_ms = now_ms;
  uint16_t usable_mask = channels.valid_mask;
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    if ((usable_mask & (1u << i)) == 0u) continue;
    if (!rawWithinCalibration(i, channels.raw[i])) {
      // Optional functions are disabled, never fabricated from an invalid raw
      // value. Required controls were rejected above by isWithinRawRange().
      usable_mask &= static_cast<uint16_t>(~(1u << i));
      continue;
    }
    result.sample.ch[i] = normalizeRawChannel(i, channels.raw[i]);
  }
  result.sample.channel_valid_mask = usable_mask;
  result.sample.link_quality = link_quality;
  result.sample.rssi_hint = rssi_hint;
  result.sample.flags = flags;
  return result;
}

bool RcNormalizer::isValidConfig(const RcNormalizerConfig& config) {
  return config.configured &&
         config.required_channel_mask != 0 &&
         config.deadband_permille >= 0 &&
         config.deadband_permille <= kRcNormalizedChannelMax &&
         [&config]() {
           for (uint8_t i = 0; i < kRcChannelCount; ++i) {
             if ((config.required_channel_mask & (1u << i)) == 0u) continue;
             const RcChannelCalibration& calibration = config.channel[i];
             if (!(calibration.raw_min < calibration.raw_mid &&
                   calibration.raw_mid < calibration.raw_max &&
                   calibration.raw_max <= kCrsfRawChannelMax)) return false;
           }
           return true;
         }();
}

bool RcNormalizer::isWithinRawRange(const CrsfRcChannels& channels) const {
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    if ((config_.required_channel_mask & (1u << i)) != 0 &&
        !rawWithinCalibration(i, channels.raw[i])) {
      return false;
    }
  }
  return true;
}

bool RcNormalizer::calibrationValid(uint8_t channel) const {
  if (channel >= kRcChannelCount) return false;
  const RcChannelCalibration& calibration = config_.channel[channel];
  return calibration.raw_min < calibration.raw_mid &&
      calibration.raw_mid < calibration.raw_max &&
      calibration.raw_max <= kCrsfRawChannelMax;
}

bool RcNormalizer::rawWithinCalibration(uint8_t channel, uint16_t raw) const {
  if (!calibrationValid(channel)) return false;
  const RcChannelCalibration& calibration = config_.channel[channel];
  return raw >= calibration.raw_min && raw <= calibration.raw_max;
}

int16_t RcNormalizer::normalizeRawChannel(uint8_t channel, uint16_t raw) const {
  const RcChannelCalibration& calibration = config_.channel[channel];
  int32_t normalized = 0;
  if (raw >= calibration.raw_mid) {
    normalized = (static_cast<int32_t>(raw) - calibration.raw_mid) * 1000 /
                 (calibration.raw_max - calibration.raw_mid);
  } else {
    normalized = -((static_cast<int32_t>(calibration.raw_mid) - raw) * 1000 /
                   (calibration.raw_mid - calibration.raw_min));
  }

  const int16_t clamped = clampPermille(normalized);
  if (absolutePermille(clamped) <= static_cast<uint16_t>(config_.deadband_permille)) {
    return 0;
  }
  return clamped;
}

}  // namespace csm::board::remote
