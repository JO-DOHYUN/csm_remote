#include "board/remote/RemoteControlSource.h"

namespace csm::board::remote {
namespace {

constexpr uint16_t kDetailNoUsableSample = 1;
constexpr uint16_t kDetailNotNeutral = 2;
constexpr uint16_t kDetailNoTakeover = 3;

}  // namespace

void RemoteControlSource::begin(uint32_t now_ms) {
  clearCommand(now_ms);
  link_state_ = RemoteLinkState::NoFrame;
  ready_for_takeover_ = false;
  reject_detail_ = 0;
}

void RemoteControlSource::update(uint32_t now_ms,
                                 const M4RemoteMailboxSnapshot& snapshot,
                                 bool neutral,
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

  if (!neutral) {
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
  reject_detail_ = 0;
}

void RemoteControlSource::clearCommand(uint32_t now_ms) {
  command_ = {};
  command_.source_time_ms = now_ms;
}

}  // namespace csm::board::remote

