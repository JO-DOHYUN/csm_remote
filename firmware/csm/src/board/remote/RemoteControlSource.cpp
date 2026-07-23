#include "board/remote/RemoteControlSource.h"

namespace csm::board::remote {
namespace {

constexpr uint16_t kDetailNoUsableSample = 1;
constexpr uint16_t kDetailNotNeutral = 2;
constexpr uint16_t kDetailNoTakeover = 3;

int16_t applyDeadband(int16_t value, uint16_t deadband) {
  return value >= -static_cast<int16_t>(deadband) &&
         value <= static_cast<int16_t>(deadband) ? 0 : value;
}

int16_t quantizeAuxiliary(int16_t value, uint16_t threshold) {
  if (value <= -static_cast<int16_t>(threshold)) return -1000;
  if (value >= static_cast<int16_t>(threshold)) return 1000;
  return 0;
}

}  // namespace

void RemoteControlSource::begin(uint32_t now_ms) {
  clearCommand(now_ms);
  link_state_ = RemoteLinkState::NoFrame;
  ready_for_takeover_ = false;
  reject_detail_ = 0;
}

bool RemoteControlSource::configure(const RemoteControlSourceConfig& config) {
  const uint8_t channels[] = {
      config.drive_channel_index,
      config.steering_channel_index,
      config.auxiliary_channel_index,
      config.steering_overlay_channel_index,
      config.momentary_overlay_channel_index,
  };
  constexpr uint8_t channel_count = sizeof(channels) / sizeof(channels[0]);
  for (uint8_t index = 0; index < channel_count; ++index) {
    if (channels[index] >= kRcChannelCount) return false;
    for (uint8_t other = static_cast<uint8_t>(index + 1);
         other < channel_count; ++other) {
      if (channels[index] == channels[other]) return false;
    }
  }
  if (config.drive_deadband_permille > 100 ||
      config.steering_deadband_permille > 100 ||
      config.auxiliary_threshold_permille < 100 ||
      config.auxiliary_threshold_permille > 1000) {
    return false;
  }
  config_ = config;
  return true;
}

void RemoteControlSource::update(uint32_t now_ms,
                                 const M4RemoteMailboxSnapshot& snapshot,
                                 bool handoff_qualified,
                                 bool takeover_request,
                                 bool release_request) {
  link_state_ = snapshot.link_state;
  ready_for_takeover_ = false;

  if (release_request) {
    clearCommand(now_ms);
    reject_detail_ = kDetailNoTakeover;
    return;
  }

  if (!snapshot.sample_present || !snapshot.integrity_ok ||
      !isUsableRemoteLink(snapshot.link_state) ||
      !isUsableRcSampleState(snapshot.sample.sample_state)) {
    clearCommand(now_ms);
    reject_detail_ = kDetailNoUsableSample;
    return;
  }

  if (!handoff_qualified) {
    clearCommand(now_ms);
    reject_detail_ = kDetailNotNeutral;
    return;
  }

  ready_for_takeover_ = true;
  if (!takeover_request) {
    clearCommand(now_ms);
    reject_detail_ = kDetailNoTakeover;
    return;
  }

  command_ = {};
  command_.source = authority::ControlSourceId::Remote;
  command_.command_seq = snapshot.sample.seq;
  command_.source_time_ms = snapshot.sample.m4_time_ms;
  command_.takeover_request = true;
  command_.enable_request = true;
  const int16_t drive = snapshot.sample.ch[config_.drive_channel_index];
  const int16_t steering = snapshot.sample.ch[config_.steering_channel_index];
  const int16_t auxiliary = quantizeAuxiliary(
      snapshot.sample.ch[config_.auxiliary_channel_index],
      config_.auxiliary_threshold_permille);
  const int16_t steering_overlay = quantizeAuxiliary(
      snapshot.sample.ch[config_.steering_overlay_channel_index],
      config_.auxiliary_threshold_permille);
  const int16_t momentary_overlay = quantizeAuxiliary(
      snapshot.sample.ch[config_.momentary_overlay_channel_index],
      config_.auxiliary_threshold_permille);
  const int16_t directed_drive = config_.invert_drive ? -drive : drive;
  command_.throttle_permille = applyDeadband(
      directed_drive, config_.drive_deadband_permille);
  const int16_t directed_steering =
      config_.invert_steering ? -steering : steering;
  command_.steer_permille = applyDeadband(
      directed_steering, config_.steering_deadband_permille);
  command_.auxiliary_permille = auxiliary;
  command_.steering_overlay_permille = steering_overlay;
  command_.momentary_overlay_permille = momentary_overlay;
  if (auxiliary != 0) {
    command_.throttle_permille = 0;
    command_.steer_permille = 0;
  }
  command_.brake_permille = 0;
  command_.drive_mode = 2;
  command_.validity_flags = 0x0003;
  reject_detail_ = 0;
}

void RemoteControlSource::clearCommand(uint32_t now_ms) {
  command_ = {};
  command_.source_time_ms = now_ms;
}

}  // namespace csm::board::remote
