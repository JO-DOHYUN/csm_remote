#pragma once

#include <stdint.h>

namespace csm::board {

class ControlPolicy {
 public:
  bool isAllowedStandardId(uint32_t can_id) const;
  bool isAllowedExtendedId(uint32_t can_id) const;
  uint32_t policyHash() const { return 0; }
};

}  // namespace csm::board
