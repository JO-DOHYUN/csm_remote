#include "board/StatusLed.h"

namespace csm::board {

void StatusLed::set(bool backend_ok, bool on) {
  digitalWrite(LEDR, (!backend_ok && on) ? LOW : HIGH);
  digitalWrite(LEDG, (backend_ok && on) ? LOW : HIGH);
  digitalWrite(LEDB, HIGH);
}

void StatusLed::begin() {
  pinMode(LEDR, OUTPUT);
  pinMode(LEDG, OUTPUT);
  pinMode(LEDB, OUTPUT);
  set(false, true);
  last_toggle_ms_ = millis();
}

void StatusLed::service(bool backend_ok) {
  const uint32_t now_ms = millis();
  if (now_ms - last_toggle_ms_ < 500) {
    return;
  }
  last_toggle_ms_ = now_ms;
  on_ = !on_;
  set(backend_ok, on_);
}

}  // namespace csm::board
