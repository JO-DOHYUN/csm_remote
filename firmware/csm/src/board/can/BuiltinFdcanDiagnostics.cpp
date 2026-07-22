#include "board/can/BuiltinFdcanDiagnostics.h"

#if BOARD_ENABLE_RUNTIME_DIAGNOSTICS

namespace csm::board::can {

bool BuiltinFdcanDiagnostics::attach(BuiltinFdcanDiagnosticCan& can) {
  FDCAN_HandleTypeDef* const candidate = can.diagnosticHandle();
#if defined(FDCAN1)
  if (candidate == nullptr || candidate->Instance != FDCAN1) {
    handle_ = nullptr;
    return false;
  }
  handle_ = candidate;
  return true;
#else
  (void)candidate;
  handle_ = nullptr;
  return false;
#endif
}

BuiltinFdcanSnapshot BuiltinFdcanDiagnostics::snapshot() const {
  BuiltinFdcanSnapshot value;
  if (handle_ == nullptr || handle_->Instance == nullptr) return value;

  FDCAN_GlobalTypeDef* const regs = handle_->Instance;
  value.valid = true;
  value.cccr = regs->CCCR;
  value.psr = regs->PSR;
  value.ecr = regs->ECR;
  value.txfqs = regs->TXFQS;
  value.txbrp = regs->TXBRP;
  value.txbto = regs->TXBTO;
  value.txbcf = regs->TXBCF;
  value.ir = regs->IR;
  value.hal_state = static_cast<uint32_t>(handle_->State);
  value.hal_error = handle_->ErrorCode;
  value.latest_tx_request_mask = HAL_FDCAN_GetLatestTxFifoQRequestBuffer(handle_);
  return value;
}

}  // namespace csm::board::can

#endif
