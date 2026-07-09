#include "board/ControlPolicy.h"

#include "protocol/TypedRecords.h"

namespace csm::board {

bool ControlPolicy::isAllowedStandardId(uint32_t can_id) const {
  return can_id == kHostCanTxAllowedPrimaryId ||
         (can_id >= kHostCanTxAllowedRangeStart && can_id <= kHostCanTxAllowedRangeEnd);
}

bool ControlPolicy::isAllowedExtendedId(uint32_t) const {
  return false;
}

}  // namespace csm::board
