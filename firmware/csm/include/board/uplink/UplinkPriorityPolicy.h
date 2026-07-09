#pragma once

#include <stdint.h>

#include "board/uplink/UplinkPriority.h"
#include "protocol/TypedFrame.h"

namespace csm::board::uplink {

UplinkPriority default_priority_for_record(csm::RecordType type);
UplinkPriority priority_for_board_event(uint16_t event_code);

}  // namespace csm::board::uplink
