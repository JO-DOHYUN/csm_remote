#pragma once

#include <stdint.h>

#include "drivers/CAN.h"

namespace csm::board::can {

struct BuiltinFdcanSnapshot {
  bool valid = false;
  uint32_t cccr = 0;
  uint32_t psr = 0;
  uint32_t ecr = 0;
  uint32_t txfqs = 0;
  uint32_t txbrp = 0;
  uint32_t txbto = 0;
  uint32_t txbcf = 0;
  uint32_t ir = 0;
  uint32_t hal_state = 0;
  uint32_t hal_error = 0;
  uint32_t latest_tx_request_mask = 0;
};

// Product and diagnostic builds use the same wrapper. It preserves the Mbed
// CAN API while exposing the already-owned HAL handle for read-only completion
// snapshots; it does not write registers or acknowledge interrupts.
class BuiltinFdcanCan : public mbed::CAN {
 public:
  BuiltinFdcanCan(PinName rd, PinName td) : mbed::CAN(rd, td) {}

  FDCAN_HandleTypeDef* fdcanHandle() { return &_can.CanHandle; }
};

class BuiltinFdcanDiagnostics {
 public:
  bool attach(BuiltinFdcanCan& can);
  bool valid() const { return handle_ != nullptr; }
  BuiltinFdcanSnapshot snapshot() const;
  int32_t abortTxRequest(uint32_t request_mask);

 private:
  FDCAN_HandleTypeDef* handle_ = nullptr;
};

}  // namespace csm::board::can
