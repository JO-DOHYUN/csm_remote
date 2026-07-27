#pragma once

#include <stdint.h>

namespace csm::board::uplink {

static constexpr uint8_t kNoReadyCanRxQueue = 0xFF;

inline uint8_t select_can_rx_queue_index(bool queue0_ready,
                                         uint64_t queue0_capture_seq,
                                         bool queue1_ready,
                                         uint64_t queue1_capture_seq) {
  if (!queue0_ready) {
    return queue1_ready ? 1 : kNoReadyCanRxQueue;
  }
  if (!queue1_ready) {
    return 0;
  }
  return queue1_capture_seq < queue0_capture_seq ? 1 : 0;
}

}  // namespace csm::board::uplink
