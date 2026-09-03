#pragma once

#include <stdint.h>

namespace csm::board::remote {

// Scheduling bound, not receiver qualification. One foreground turn may
// consume at most one maximum CRSF frame or 500 us, then control IPC and
// health publication must run before UART draining resumes.
static constexpr uint16_t kCrsfForegroundByteBudget = 64u;
static constexpr uint32_t kCrsfForegroundTimeBudgetUs = 500u;

class CrsfForegroundBudget {
 public:
  explicit CrsfForegroundBudget(uint32_t started_us)
      : started_us_(started_us) {}

  bool mayConsume(uint32_t now_us) const {
    return consumed_ < kCrsfForegroundByteBudget &&
           now_us - started_us_ < kCrsfForegroundTimeBudgetUs;
  }
  void noteConsumed() {
    if (consumed_ != UINT16_MAX) ++consumed_;
  }
  uint16_t consumed() const { return consumed_; }

 private:
  uint32_t started_us_ = 0;
  uint16_t consumed_ = 0;
};

}  // namespace csm::board::remote
