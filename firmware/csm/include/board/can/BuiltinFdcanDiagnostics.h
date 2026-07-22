#pragma once

#include <stdint.h>

#ifndef BOARD_ENABLE_RUNTIME_DIAGNOSTICS
#define BOARD_ENABLE_RUNTIME_DIAGNOSTICS 0
#endif

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS
#include "drivers/CAN.h"
#endif

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

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS

// Debug builds construct this object explicitly from setup(). That makes a
// reset in the Mbed/FDCAN constructor observable instead of occurring before
// the first retained boot checkpoint.
class BuiltinFdcanDiagnosticCan : public mbed::CAN {
 public:
  BuiltinFdcanDiagnosticCan(PinName rd, PinName td) : mbed::CAN(rd, td) {}

  FDCAN_HandleTypeDef* diagnosticHandle() { return &_can.CanHandle; }
};

class BuiltinFdcanDiagnostics {
 public:
  bool attach(BuiltinFdcanDiagnosticCan& can);
  bool valid() const { return handle_ != nullptr; }
  BuiltinFdcanSnapshot snapshot() const;

 private:
  FDCAN_HandleTypeDef* handle_ = nullptr;
};

#else

class BuiltinFdcanDiagnostics {
 public:
  bool valid() const { return false; }
  BuiltinFdcanSnapshot snapshot() const { return {}; }
};

#endif

}  // namespace csm::board::can
