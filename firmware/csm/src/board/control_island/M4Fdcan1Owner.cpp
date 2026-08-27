#include "board/control_island/M4Fdcan1Owner.h"

#include <string.h>

#include "BoardPins.h"

#ifndef BOARD_HNO1_CAN1_BITRATE
#define BOARD_HNO1_CAN1_BITRATE 500000
#endif
#ifndef BOARD_M4_TIM4_IRQ_PRIORITY
#define BOARD_M4_TIM4_IRQ_PRIORITY 5
#endif
#ifndef BOARD_M4_FDCAN_IRQ_PRIORITY
#define BOARD_M4_FDCAN_IRQ_PRIORITY 6
#endif
#ifndef BOARD_M4_FDCAN_RX_DRAIN_PER_IRQ
#define BOARD_M4_FDCAN_RX_DRAIN_PER_IRQ 16
#endif

namespace csm::board::control_island {
namespace {

M4Fdcan1Owner* g_fdcan_owner = nullptr;
M4ControlTimebase* g_timebase = nullptr;

void fdcanIrqTrampoline() {
  if (g_fdcan_owner != nullptr) g_fdcan_owner->serviceInterrupt();
}

void tim4IrqTrampoline() {
  if (g_timebase != nullptr) g_timebase->serviceInterrupt();
}

uint8_t dlcBytes(uint32_t data_length) {
  const uint32_t dlc = (data_length >> 16u) & 0x0Fu;
  return dlc > 8u ? 8u : static_cast<uint8_t>(dlc);
}

}  // namespace

bool M4Fdcan1Owner::begin(uint32_t m4_boot_id,
                          M4StaticCyclicExecutor* executor) {
  fdcan_ready_ = false;
  timebase_ready_ = false;
  clock_contract_ok_ = false;
  protocol_fault_latched_ = false;
  error_status_latch_ = 0u;
  executor_ = executor;
  m4_boot_id_ = m4_boot_id;
  if (executor_ == nullptr || m4_boot_id_ == 0u) return false;
  // Mbed's low-level CAN bootstrap owns only pin/RCC discovery on M4. It is
  // immediately replaced by the sole direct-HAL dedicated-buffer contract;
  // no mbed::CAN object or cyclic can_write path exists in this image.
  can_init_freq(&can_, PB_8, PH_13, BOARD_HNO1_CAN1_BITRATE);
  handle_ = &can_.CanHandle;
  if (handle_->Instance != FDCAN1) return false;
  if (!configureDirectHal()) {
    (void)HAL_FDCAN_Stop(handle_);
    (void)HAL_FDCAN_DeInit(handle_);
    return false;
  }

  g_fdcan_owner = this;
  if (!configureInterrupts()) {
    g_fdcan_owner = nullptr;
    NVIC_DisableIRQ(FDCAN1_IT0_IRQn);
    NVIC_DisableIRQ(FDCAN1_IT1_IRQn);
    (void)HAL_FDCAN_Stop(handle_);
    (void)HAL_FDCAN_DeInit(handle_);
    return false;
  }
  fdcan_ready_ = true;
  updateProtocolState();
  return true;
}

bool M4Fdcan1Owner::configureDirectHal() {
  const FDCAN_InitTypeDef discovered_timing = handle_->Init;
  (void)HAL_FDCAN_Stop(handle_);
  (void)HAL_FDCAN_DeInit(handle_);
  memset(&handle_->Init, 0, sizeof(handle_->Init));
  handle_->Instance = FDCAN1;
  handle_->Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  handle_->Init.Mode = FDCAN_MODE_NORMAL;
  handle_->Init.AutoRetransmission = ENABLE;
  handle_->Init.TransmitPause = DISABLE;
  handle_->Init.ProtocolException = ENABLE;
  handle_->Init.NominalPrescaler = discovered_timing.NominalPrescaler;
  handle_->Init.NominalSyncJumpWidth = discovered_timing.NominalSyncJumpWidth;
  handle_->Init.NominalTimeSeg1 = discovered_timing.NominalTimeSeg1;
  handle_->Init.NominalTimeSeg2 = discovered_timing.NominalTimeSeg2;
  handle_->Init.DataPrescaler = discovered_timing.DataPrescaler;
  handle_->Init.DataSyncJumpWidth = discovered_timing.DataSyncJumpWidth;
  handle_->Init.DataTimeSeg1 = discovered_timing.DataTimeSeg1;
  handle_->Init.DataTimeSeg2 = discovered_timing.DataTimeSeg2;
  handle_->Init.MessageRAMOffset = 0;
  handle_->Init.StdFiltersNbr = 1;
  handle_->Init.ExtFiltersNbr = 0;
  handle_->Init.RxFifo0ElmtsNbr = 64;
  handle_->Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
  handle_->Init.RxFifo1ElmtsNbr = 0;
  handle_->Init.RxBuffersNbr = 0;
  handle_->Init.TxEventsNbr = 0;
  handle_->Init.TxBuffersNbr = 3;
  handle_->Init.TxFifoQueueElmtsNbr = 0;
  handle_->Init.TxElmtSize = FDCAN_DATA_BYTES_8;
  handle_->Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(handle_) != HAL_OK) return false;

  FDCAN_FilterTypeDef filter = {};
  filter.IdType = FDCAN_STANDARD_ID;
  filter.FilterIndex = 0;
  filter.FilterType = FDCAN_FILTER_MASK;
  filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  filter.FilterID1 = 0;
  filter.FilterID2 = 0;
  if (HAL_FDCAN_ConfigFilter(handle_, &filter) != HAL_OK) return false;
  if (HAL_FDCAN_ConfigGlobalFilter(
          handle_, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT,
          FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK) {
    return false;
  }
  if (HAL_FDCAN_Start(handle_) != HAL_OK) return false;
  clock_contract_ok_ = handle_->Init.NominalPrescaler != 0u &&
      handle_->Init.NominalTimeSeg1 != 0u &&
      handle_->Init.NominalTimeSeg2 != 0u;
  return clock_contract_ok_;
}

bool M4Fdcan1Owner::configureInterrupts() {
  const uint32_t notifications =
      FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST |
      FDCAN_IT_TX_COMPLETE | FDCAN_IT_TX_ABORT_COMPLETE |
      FDCAN_IT_ERROR_WARNING | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_BUS_OFF |
      FDCAN_IT_ARB_PROTOCOL_ERROR | FDCAN_IT_DATA_PROTOCOL_ERROR;
  if (HAL_FDCAN_ConfigInterruptLines(handle_, notifications,
                                     FDCAN_INTERRUPT_LINE0) != HAL_OK) {
    return false;
  }
  if (HAL_FDCAN_ActivateNotification(handle_, notifications,
                                     FDCAN_TX_BUFFER0 |
                                         FDCAN_TX_BUFFER1 |
                                         FDCAN_TX_BUFFER2) != HAL_OK) {
    return false;
  }
  NVIC_SetVector(FDCAN1_IT0_IRQn,
                 reinterpret_cast<uint32_t>(fdcanIrqTrampoline));
  NVIC_SetVector(FDCAN1_IT1_IRQn,
                 reinterpret_cast<uint32_t>(fdcanIrqTrampoline));
  NVIC_SetPriority(FDCAN1_IT0_IRQn, BOARD_M4_FDCAN_IRQ_PRIORITY);
  NVIC_SetPriority(FDCAN1_IT1_IRQn, BOARD_M4_FDCAN_IRQ_PRIORITY);
  NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
  NVIC_EnableIRQ(FDCAN1_IT1_IRQn);
  return true;
}

void M4Fdcan1Owner::serviceInterrupt() {
  if (handle_ != nullptr) HAL_FDCAN_IRQHandler(handle_);
}

void M4Fdcan1Owner::serviceRxInterrupt(uint32_t interrupt_flags) {
  if (handle_ == nullptr) return;
  if ((interrupt_flags & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) != 0u &&
      rx_fifo_lost_count_ != UINT32_MAX) {
    ++rx_fifo_lost_count_;
  }
  uint32_t drained = 0;
  while (drained < BOARD_M4_FDCAN_RX_DRAIN_PER_IRQ &&
         HAL_FDCAN_GetRxFifoFillLevel(handle_, FDCAN_RX_FIFO0) != 0u) {
    FDCAN_RxHeaderTypeDef header = {};
    uint8_t data[8] = {};
    if (HAL_FDCAN_GetRxMessage(handle_, FDCAN_RX_FIFO0, &header, data) !=
        HAL_OK) {
      break;
    }
    RawCanEntry entry;
    entry.capture_sequence = ++rx_capture_sequence_;
    entry.mono_us = micros();
    entry.can_id_flags = header.Identifier &
        (header.IdType == FDCAN_EXTENDED_ID ? 0x1FFFFFFFu : 0x7FFu);
    if (header.IdType == FDCAN_EXTENDED_ID) entry.can_id_flags |= 1u << 29;
    if (header.RxFrameType == FDCAN_REMOTE_FRAME) entry.can_id_flags |= 1u << 30;
    entry.fdcan_timestamp = header.RxTimestamp;
    entry.dlc_flags = dlcBytes(header.DataLength);
    memcpy(entry.data, data, sizeof(entry.data));
    (void)pushRawCanFromM4(entry);
    ++drained;
  }
}

void M4Fdcan1Owner::noteTxComplete(uint32_t buffer_indexes) {
  terminal(buffer_indexes, false);
}

void M4Fdcan1Owner::noteTxAbort(uint32_t buffer_indexes) {
  terminal(buffer_indexes, true);
}

void M4Fdcan1Owner::noteError(uint32_t) {
  error_status_latch_ = 1u;
}

void M4Fdcan1Owner::consumeLatchedEvents() {
  const bool error_latched = error_status_latch_ != 0u;
  error_status_latch_ = 0u;
  updateProtocolState();
  if (error_latched && protocol_.BusOff != 0u && !protocol_fault_latched_) {
    // Bus-off is a terminal transport fault. Latch until reset and abort every
    // outstanding dedicated buffer so no stale request can transmit later.
    protocol_fault_latched_ = true;
    for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
      if (pending(lane)) {
        (void)HAL_FDCAN_AbortTxRequest(handle_, bufferMask(lane));
      }
    }
  }
}

bool M4Fdcan1Owner::ready() const {
  return fdcan_ready_ && timebase_ready_ && clock_contract_ok_ &&
      !protocol_fault_latched_ && !busOff();
}

bool M4Fdcan1Owner::errorWarning() const {
  return protocol_.Warning != 0u;
}

bool M4Fdcan1Owner::errorPassive() const {
  return protocol_.ErrorPassive != 0u;
}

bool M4Fdcan1Owner::busOff() const { return protocol_.BusOff != 0u; }

bool M4Fdcan1Owner::request(uint8_t lane, const uint8_t data[8]) {
  if (lane >= kLaneCount || data == nullptr || !ready() || pending(lane)) {
    return false;
  }
  FDCAN_TxHeaderTypeDef header = {};
  header.Identifier = kLaneIds[lane];
  header.IdType = FDCAN_STANDARD_ID;
  header.TxFrameType = FDCAN_DATA_FRAME;
  header.DataLength = FDCAN_DLC_BYTES_8;
  header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  header.BitRateSwitch = FDCAN_BRS_OFF;
  header.FDFormat = FDCAN_CLASSIC_CAN;
  header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  header.MessageMarker = 0;
  const uint32_t buffer = bufferMask(lane);
  uint8_t payload[8];
  memcpy(payload, data, sizeof(payload));
  if (HAL_FDCAN_AddMessageToTxBuffer(handle_, &header, payload, buffer) !=
      HAL_OK) {
    return false;
  }
  if (HAL_FDCAN_EnableTxBufferRequest(handle_, buffer) == HAL_OK) return true;
  if (!pending(lane)) return false;
  if (HAL_FDCAN_AbortTxRequest(handle_, buffer) != HAL_OK && executor_ != nullptr) {
    executor_->latchTrackingFault(lane);
  }
  return true;
}

bool M4Fdcan1Owner::cancel(uint8_t lane) {
  return lane < kLaneCount &&
      HAL_FDCAN_AbortTxRequest(handle_, bufferMask(lane)) == HAL_OK;
}

bool M4Fdcan1Owner::pending(uint8_t lane) const {
  return lane < kLaneCount && handle_ != nullptr &&
      HAL_FDCAN_IsTxBufferMessagePending(handle_, bufferMask(lane)) != 0u;
}

FdcanRawSnapshot M4Fdcan1Owner::rawSnapshot() const {
  FdcanRawSnapshot value;
  if (handle_ == nullptr || handle_->Instance != FDCAN1) return value;
  value.cccr = FDCAN1->CCCR;
  value.psr = FDCAN1->PSR;
  value.ecr = FDCAN1->ECR;
  value.txfqs = FDCAN1->TXFQS;
  value.txbrp = FDCAN1->TXBRP;
  value.txbto = FDCAN1->TXBTO;
  value.txbcf = FDCAN1->TXBCF;
  value.ir = FDCAN1->IR;
  value.hal_state = static_cast<uint32_t>(HAL_FDCAN_GetState(handle_));
  value.hal_error = HAL_FDCAN_GetError(handle_);
  return value;
}

void M4Fdcan1Owner::updateProtocolState() {
  if (handle_ == nullptr) return;
  (void)HAL_FDCAN_GetProtocolStatus(handle_, &protocol_);
  (void)HAL_FDCAN_GetErrorCounters(handle_, &errors_);
}

void M4Fdcan1Owner::terminal(uint32_t buffer_indexes, bool abort_callback) {
  const FdcanRawSnapshot raw = rawSnapshot();
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    const uint32_t mask = bufferMask(lane);
    if ((buffer_indexes & mask) == 0u) continue;
    const bool transmitted = (raw.txbto & mask) != 0u;
    const bool cancelled = abort_callback || (raw.txbcf & mask) != 0u;
    executor_->latchTerminalEvent(lane, transmitted, cancelled);
  }
}

uint32_t M4Fdcan1Owner::bufferMask(uint8_t lane) {
  static constexpr uint32_t kBuffers[kLaneCount] = {
      FDCAN_TX_BUFFER0, FDCAN_TX_BUFFER1, FDCAN_TX_BUFFER2};
  return lane < kLaneCount ? kBuffers[lane] : 0u;
}

bool M4ControlTimebase::begin(M4StaticCyclicExecutor* executor,
                              M4Fdcan1Owner* owner) {
  executor_ = executor;
  owner_ = owner;
  if (executor_ == nullptr || owner_ == nullptr) return false;
  const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
  const bool apb_divided = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1) != 0u;
  timer_clock_hz_ = apb_divided ? pclk1 * 2u : pclk1;
  if (timer_clock_hz_ == 0u || timer_clock_hz_ % 1000000u != 0u ||
      timer_clock_hz_ / 1000000u > 65536u) {
    owner_->markTimebaseReady(false);
    return false;
  }
  __HAL_RCC_TIM4_CLK_ENABLE();
  TIM4->CR1 = 0;
  TIM4->DIER = 0;
  TIM4->PSC = timer_clock_hz_ / 1000000u - 1u;
  TIM4->ARR = 4999u;
  TIM4->CNT = 0;
  TIM4->EGR = TIM_EGR_UG;
  TIM4->SR = 0;
  TIM4->DIER = TIM_DIER_UIE;
  g_timebase = this;
  NVIC_SetVector(TIM4_IRQn, reinterpret_cast<uint32_t>(tim4IrqTrampoline));
  NVIC_SetPriority(TIM4_IRQn, BOARD_M4_TIM4_IRQ_PRIORITY);
  NVIC_EnableIRQ(TIM4_IRQn);
  TIM4->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
  owner_->markTimebaseReady(true);
  return true;
}

void M4ControlTimebase::serviceInterrupt() {
  if ((TIM4->SR & TIM_SR_UIF) == 0u) return;
  TIM4->SR &= ~TIM_SR_UIF;
  executor_->onFiveMillisecondSlot(micros());
}

}  // namespace csm::board::control_island

extern "C" void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef*,
                                            uint32_t interrupt_flags) {
  if (csm::board::control_island::g_fdcan_owner != nullptr) {
    csm::board::control_island::g_fdcan_owner->serviceRxInterrupt(
        interrupt_flags);
  }
}

extern "C" void HAL_FDCAN_TxBufferCompleteCallback(FDCAN_HandleTypeDef*,
                                                    uint32_t indexes) {
  if (csm::board::control_island::g_fdcan_owner != nullptr) {
    csm::board::control_island::g_fdcan_owner->noteTxComplete(indexes);
  }
}

extern "C" void HAL_FDCAN_TxBufferAbortCallback(FDCAN_HandleTypeDef*,
                                                 uint32_t indexes) {
  if (csm::board::control_island::g_fdcan_owner != nullptr) {
    csm::board::control_island::g_fdcan_owner->noteTxAbort(indexes);
  }
}

extern "C" void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef*,
                                               uint32_t status) {
  if (csm::board::control_island::g_fdcan_owner != nullptr) {
    csm::board::control_island::g_fdcan_owner->noteError(status);
  }
}
