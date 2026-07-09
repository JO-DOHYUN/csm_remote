#pragma once

#include <stdint.h>

namespace csm::board::can {

struct CanFrame8 {
  uint32_t can_id_flags = 0;
  uint8_t bus = 0;
  uint8_t dlc = 0;
  uint8_t data[8] = {};
};

struct CanBackendState {
  bool ready = false;
  bool tx_busy = false;
  bool error_passive = false;
  bool bus_off = false;
};

struct CanBackendHealth {
  uint32_t rx_total = 0;
  uint32_t rx_dropped_total = 0;
  uint32_t tx_total = 0;
  uint32_t tx_failed_total = 0;
  uint32_t spi_error_total = 0;
  uint32_t error_flag_total = 0;
  uint8_t last_canintf = 0;
  uint8_t last_eflg = 0;
  uint8_t last_canctrl = 0;
  uint8_t last_error = 0;
};

}  // namespace csm::board::can
