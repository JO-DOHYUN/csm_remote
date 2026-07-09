#pragma once

#include <Arduino.h>

namespace csm::board {

class StatusLed {
 public:
  void begin();
  void service(bool backend_ok);

 private:
  void set(bool backend_ok, bool on);

  uint32_t last_toggle_ms_ = 0;
  bool on_ = false;
};

}  // namespace csm::board
