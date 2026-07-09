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
  if (channels.count != kRcChannelCount) {
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
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    result.sample.ch[i] = normalizeRawChannel(channels.raw[i]);
  }
  result.sample.link_quality = link_quality;
  result.sample.rssi_hint = rssi_hint;
  result.sample.flags = flags;
  return result;
}

bool RcNormalizer::isValidConfig(const RcNormalizerConfig& config) {
  return config.configured &&
         config.raw_min < config.raw_mid &&
         config.raw_mid < config.raw_max &&
         config.raw_max <= kCrsfRawChannelMax &&
         config.deadband_permille >= 0 &&
         config.deadband_permille <= kRcNormalizedChannelMax;
}

bool RcNormalizer::isWithinRawRange(const CrsfRcChannels& channels) const {
  for (uint8_t i = 0; i < kRcChannelCount; ++i) {
    if (channels.raw[i] < config_.raw_min || channels.raw[i] > config_.raw_max) {
      return false;
    }
  }
  return true;
}

int16_t RcNormalizer::normalizeRawChannel(uint16_t raw) const {
  int32_t normalized = 0;
  if (raw >= config_.raw_mid) {
    normalized = (static_cast<int32_t>(raw) - config_.raw_mid) * 1000 /
                 (config_.raw_max - config_.raw_mid);
  } else {
    normalized = -((static_cast<int32_t>(config_.raw_mid) - raw) * 1000 /
                   (config_.raw_mid - config_.raw_min));
  }

  const int16_t clamped = clampPermille(normalized);
  if (absolutePermille(clamped) <= static_cast<uint16_t>(config_.deadband_permille)) {
    return 0;
  }
  return clamped;
}

}  // namespace csm::board::remote
