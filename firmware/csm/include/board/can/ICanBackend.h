#pragma once

#include <stdint.h>

#include "board/can/CanTypes.h"

namespace csm::board::can {

class ICanBackend {
 public:
  virtual ~ICanBackend() = default;
  virtual bool begin() = 0;
  virtual void pollRx() = 0;
  virtual bool submitTx(const CanFrame8& frame, uint32_t command_id) = 0;
  virtual void serviceTxAudit() = 0;
  virtual CanBackendState state() const = 0;
  virtual CanBackendHealth health() const = 0;
};

}  // namespace csm::board::can
