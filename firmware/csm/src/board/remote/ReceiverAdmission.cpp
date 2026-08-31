#include "board/remote/ReceiverAdmission.h"

namespace csm::board::remote {
namespace {

void saturatingIncrement(uint32_t* value) {
  if (value != nullptr && *value != UINT32_MAX) ++(*value);
}

}  // namespace

bool ReceiverAdmission::configure(const ReceiverAdmissionConfig& config) {
  if (!config.configured || config.receiver_address == 0u ||
      config.consecutive_frames_required == 0u ||
      config.required_channel_mask == 0u || config.rc_freshness_ms == 0u ||
      config.link_statistics_freshness_ms == 0u) {
    config_ = {};
    state_ = {};
    return false;
  }
  config_ = config;
  state_ = {};
  state_.last_reject_detail =
      static_cast<uint16_t>(ReceiverAdmissionRejectDetail::Qualifying);
  last_rc_frame_ms_ = 0u;
  rc_frame_seen_ = false;
  return true;
}

void ReceiverAdmission::reset(ReceiverAdmissionRejectDetail detail) {
  state_.receiver_qualified = false;
  state_.consecutive_frames = 0u;
  state_.last_reject_detail = static_cast<uint16_t>(detail);
  saturatingIncrement(&state_.reset_count);
  rc_frame_seen_ = false;
  last_rc_frame_ms_ = 0u;
}

bool ReceiverAdmission::observeRcFrame(uint32_t now_ms, uint8_t address,
                                       uint16_t channel_valid_mask) {
  if (!config_.configured) {
    reset(ReceiverAdmissionRejectDetail::NotConfigured);
    return false;
  }
  if (address != config_.receiver_address) {
    reset(ReceiverAdmissionRejectDetail::BadAddress);
    return false;
  }
  if ((channel_valid_mask & config_.required_channel_mask) !=
      config_.required_channel_mask) {
    reset(ReceiverAdmissionRejectDetail::MissingControlChannels);
    return false;
  }
  if (!rc_frame_seen_ || now_ms - last_rc_frame_ms_ > config_.rc_freshness_ms) {
    state_.consecutive_frames = 1u;
  } else if (state_.consecutive_frames < config_.consecutive_frames_required) {
    ++state_.consecutive_frames;
  }
  rc_frame_seen_ = true;
  last_rc_frame_ms_ = now_ms;
  state_.receiver_qualified =
      state_.consecutive_frames >= config_.consecutive_frames_required;
  state_.last_reject_detail = static_cast<uint16_t>(
      state_.receiver_qualified ? ReceiverAdmissionRejectDetail::None
                                : ReceiverAdmissionRejectDetail::Qualifying);
  return state_.receiver_qualified;
}

bool ReceiverAdmission::usable(uint32_t now_ms,
                               bool link_statistics_observed,
                               uint8_t link_quality,
                               uint32_t link_statistics_age_ms) {
  if (!config_.configured) {
    state_.last_reject_detail =
        static_cast<uint16_t>(ReceiverAdmissionRejectDetail::NotConfigured);
    return false;
  }
  if (!rc_frame_seen_ || now_ms - last_rc_frame_ms_ > config_.rc_freshness_ms) {
    state_.receiver_qualified = false;
    state_.consecutive_frames = 0u;
    state_.last_reject_detail =
        static_cast<uint16_t>(ReceiverAdmissionRejectDetail::RcStale);
    return false;
  }
  if (!state_.receiver_qualified) return false;
  if (link_statistics_observed) {
    if (link_statistics_age_ms > config_.link_statistics_freshness_ms) {
      state_.last_reject_detail = static_cast<uint16_t>(
          ReceiverAdmissionRejectDetail::LinkStatisticsStale);
      return false;
    }
    if (link_quality == 0u) {
      state_.last_reject_detail = static_cast<uint16_t>(
          ReceiverAdmissionRejectDetail::LinkQualityZero);
      return false;
    }
  }
  state_.last_reject_detail =
      static_cast<uint16_t>(ReceiverAdmissionRejectDetail::None);
  return true;
}

}  // namespace csm::board::remote
