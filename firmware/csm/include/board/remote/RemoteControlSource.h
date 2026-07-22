#pragma once

#include <stdint.h>

#include "board/control/OperatorCommand.h"
#include "board/remote/M4RemoteMailboxReader.h"

namespace csm::board::remote {

struct RemoteControlSourceConfig {
  uint8_t drive_channel_index = 1;     // CRSF CH2.
  uint8_t steering_channel_index = 3;  // CRSF CH4.
  uint8_t auxiliary_channel_index = 4; // CRSF CH5.
  uint16_t steering_deadband_permille = 20;
  uint16_t auxiliary_threshold_permille = 500;
  bool invert_drive = false;
  bool invert_steering = false;
};

class RemoteControlSource {
 public:
  void begin(uint32_t now_ms);
  bool configure(const RemoteControlSourceConfig& config);

  void update(uint32_t now_ms,
              const M4RemoteMailboxSnapshot& snapshot,
              bool handoff_qualified,
              bool takeover_request,
              bool release_request);

  const control::OperatorCommand& command() const { return command_; }
  RemoteLinkState linkState() const { return link_state_; }
  bool readyForTakeover() const { return ready_for_takeover_; }
  uint16_t rejectDetail() const { return reject_detail_; }

 private:
  void clearCommand(uint32_t now_ms);

  control::OperatorCommand command_ = {};
  RemoteLinkState link_state_ = RemoteLinkState::NotConfigured;
  bool ready_for_takeover_ = false;
  uint16_t reject_detail_ = 0;
  RemoteControlSourceConfig config_ = {};
};

}  // namespace csm::board::remote
