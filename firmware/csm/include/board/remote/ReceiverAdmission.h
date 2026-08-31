#pragma once

#include <stdint.h>

namespace csm::board::remote {

enum class ReceiverAdmissionRejectDetail : uint16_t {
  None = 0,
  NotConfigured = 1,
  BadAddress = 2,
  MissingControlChannels = 3,
  SequenceBroken = 4,
  Qualifying = 5,
  RcStale = 6,
  LinkQualityZero = 7,
  LinkStatisticsStale = 8,
};

struct ReceiverAdmissionConfig {
  bool configured = false;
  uint8_t receiver_address = 0;
  uint8_t consecutive_frames_required = 0;
  uint16_t required_channel_mask = 0;
  uint32_t rc_freshness_ms = 0;
  uint32_t link_statistics_freshness_ms = 0;
};

struct ReceiverAdmissionState {
  bool receiver_qualified = false;
  uint8_t consecutive_frames = 0;
  uint16_t last_reject_detail =
      static_cast<uint16_t>(ReceiverAdmissionRejectDetail::NotConfigured);
  uint32_t reset_count = 0;
};

// M4-only, fixed-state admission. It owns no queue and never merges partial
// channel frames across time.
class ReceiverAdmission {
 public:
  bool configure(const ReceiverAdmissionConfig& config);
  void reset(ReceiverAdmissionRejectDetail detail);
  bool observeRcFrame(uint32_t now_ms, uint8_t address,
                      uint16_t channel_valid_mask);
  bool usable(uint32_t now_ms, bool link_statistics_observed,
              uint8_t link_quality, uint32_t link_statistics_age_ms);

  const ReceiverAdmissionState& state() const { return state_; }

 private:
  ReceiverAdmissionConfig config_ = {};
  ReceiverAdmissionState state_ = {};
  uint32_t last_rc_frame_ms_ = 0;
  bool rc_frame_seen_ = false;
};

}  // namespace csm::board::remote
