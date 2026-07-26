#include "board/can/BuiltinFdcanDiagnostics.h"

namespace csm::board::can {

bool BuiltinFdcanDiagnostics::attach(BuiltinFdcanCan& can) {
  FDCAN_HandleTypeDef* const candidate = can.fdcanHandle();
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

int32_t BuiltinFdcanDiagnostics::abortTxRequest(uint32_t request_mask) {
  if (handle_ == nullptr || handle_->Instance == nullptr ||
      request_mask == 0u || (request_mask & ~0x07u) != 0u) {
    return static_cast<int32_t>(HAL_ERROR);
  }
  return static_cast<int32_t>(
      HAL_FDCAN_AbortTxRequest(handle_, request_mask));
}

}  // namespace csm::board::can
