#pragma once

#include <stdint.h>

#include "board/control/OperatorCommand.h"
#include "board/remote/M4RemoteMailboxReader.h"

namespace csm::board::remote {

class RemoteControlSource {
 public:
  void begin(uint32_t now_ms);

  void update(uint32_t now_ms,
              const M4RemoteMailboxSnapshot& snapshot,
              bool neutral,
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
};

}  // namespace csm::board::remote

