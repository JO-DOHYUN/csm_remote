#include "board/feeder/FeederUartIngress.h"

#include <Arduino.h>
#include <stm32h7xx_hal.h>

#include <algorithm>
#include <string.h>

namespace csm::board::feeder {
namespace {

constexpr size_t kDmaBufferSize = 4096;
constexpr uint32_t kCacheLineSize = 32;
constexpr size_t kServiceChunkBytes = 64;
static_assert((kDmaBufferSize & (kDmaBufferSize - 1U)) == 0U,
              "DMA buffer must be a power of two");

alignas(kCacheLineSize) uint8_t g_dma_buffer[kDmaBufferSize] = {};
UART_HandleTypeDef g_uart = {};
DMA_HandleTypeDef g_dma = {};
volatile uint32_t g_dma_wraps = 0;
FeederUartIngress* g_active = nullptr;

void dmaComplete(DMA_HandleTypeDef*) {
  if (g_active != nullptr) {
    g_active->noteDmaComplete();
  }
}

void dmaError(DMA_HandleTypeDef*) {
  if (g_active != nullptr) {
    g_active->noteDmaError();
  }
}

uint32_t alignDown(uint32_t value) {
  return value & ~(kCacheLineSize - 1U);
}

uint32_t alignUp(uint32_t value) {
  return (value + kCacheLineSize - 1U) & ~(kCacheLineSize - 1U);
}

}  // namespace

bool FeederUartIngress::begin(const FeederUartIngressConfig& config) {
  if (g_active != nullptr && g_active != this) {
    return false;
  }
  g_active = this;
  stale_timeout_ms_ = config.stale_timeout_ms;
  service_time_budget_us_ = config.service_time_budget_us;
  consumed_total_ = 0;
  ingress_stats_ = {};
  dma_error_events_.reset();
  decoder_.begin(nullptr, nullptr);

  // Mid Carrier J14 RX2 is Portenta HD SERIAL2_RX on PG9/USART6_RX.
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_USART6_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Pin = GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF7_USART6;
  HAL_GPIO_Init(GPIOG, &gpio);

  g_uart = {};
  g_uart.Instance = USART6;
  g_uart.Init.BaudRate = config.baud;
  g_uart.Init.WordLength = UART_WORDLENGTH_8B;
  g_uart.Init.StopBits = UART_STOPBITS_1;
  g_uart.Init.Parity = UART_PARITY_NONE;
  g_uart.Init.Mode = UART_MODE_RX;
  g_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  g_uart.Init.OverSampling = UART_OVERSAMPLING_8;
  g_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  g_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  g_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&g_uart) != HAL_OK) {
    g_active = nullptr;
    return false;
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&g_uart, UART_RXFIFO_THRESHOLD_1_8) !=
          HAL_OK ||
      HAL_UARTEx_EnableFifoMode(&g_uart) != HAL_OK) {
    g_active = nullptr;
    return false;
  }

  g_dma = {};
  g_dma.Instance = DMA1_Stream0;
  g_dma.Init.Request = DMA_REQUEST_USART6_RX;
  g_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
  g_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  g_dma.Init.MemInc = DMA_MINC_ENABLE;
  g_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  g_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  g_dma.Init.Mode = DMA_CIRCULAR;
  g_dma.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  g_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&g_dma) != HAL_OK) {
    g_active = nullptr;
    return false;
  }
  __HAL_LINKDMA(&g_uart, hdmarx, g_dma);

  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  initialized_ = startDma();
  if (!initialized_) {
    g_active = nullptr;
  }
  return initialized_;
}

bool FeederUartIngress::startDma() {
  memset(g_dma_buffer, 0, sizeof(g_dma_buffer));
  SCB_CleanInvalidateDCache_by_Addr(
      reinterpret_cast<uint32_t*>(g_dma_buffer), sizeof(g_dma_buffer));
  g_dma_wraps = 0;
  consumed_total_ = 0;
  if (HAL_UART_Receive_DMA(&g_uart, g_dma_buffer, sizeof(g_dma_buffer)) !=
      HAL_OK) {
    return false;
  }
  g_dma.XferCpltCallback = dmaComplete;
  g_dma.XferErrorCallback = dmaError;
  g_dma.XferHalfCpltCallback = nullptr;
  __HAL_DMA_DISABLE_IT(&g_dma, DMA_IT_HT);
  return true;
}

bool FeederUartIngress::producedTotal(uint64_t* produced_total) {
  if (produced_total == nullptr) {
    return false;
  }
  uint32_t wraps_before = 0;
  uint32_t wraps_after = 0;
  uint32_t remaining = 0;
  bool transfer_complete_pending = false;
  do {
    wraps_before = g_dma_wraps;
    __DMB();
    remaining = __HAL_DMA_GET_COUNTER(&g_dma);
    transfer_complete_pending =
        __HAL_DMA_GET_FLAG(
            &g_dma, __HAL_DMA_GET_TC_FLAG_INDEX(&g_dma)) != RESET;
    __DMB();
    wraps_after = g_dma_wraps;
  } while (wraps_before != wraps_after);
  const uint32_t position =
      remaining <= kDmaBufferSize
          ? static_cast<uint32_t>(kDmaBufferSize - remaining)
          : static_cast<uint32_t>(kDmaBufferSize + 1U);
  const FeederDmaCursorResult reconciled = reconcileFeederDmaCursor(
      wraps_before, position, kDmaBufferSize, transfer_complete_pending,
      consumed_total_);
  if (!reconciled.valid) {
    ++ingress_stats_.dma_cursor_faults;
    return false;
  }
  if (reconciled.reconciled_pending_wrap) {
    ++ingress_stats_.dma_wrap_reconciliations;
  }
  *produced_total = reconciled.produced_total;
  return true;
}

void FeederUartIngress::invalidateRange(size_t offset, size_t length) {
  if (length == 0U) {
    return;
  }
  const uint32_t start = alignDown(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&g_dma_buffer[offset])));
  const uint32_t end = alignUp(static_cast<uint32_t>(
      reinterpret_cast<uintptr_t>(&g_dma_buffer[offset + length])));
  SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(start), end - start);
}

void FeederUartIngress::pollUartErrors() {
  const uint32_t errors =
      g_uart.Instance->ISR &
      (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE);
  if (errors == 0U) {
    return;
  }
  uint32_t clear = 0;
  if ((errors & USART_ISR_PE) != 0U) clear |= USART_ICR_PECF;
  if ((errors & USART_ISR_FE) != 0U) clear |= USART_ICR_FECF;
  if ((errors & USART_ISR_NE) != 0U) clear |= USART_ICR_NECF;
  if ((errors & USART_ISR_ORE) != 0U) clear |= USART_ICR_ORECF;
  g_uart.Instance->ICR = clear;
  ++ingress_stats_.uart_error_events;
}

void FeederUartIngress::service(size_t byte_budget,
                                uint64_t arrival_mono_us,
                                FeederCanFrameFn frame_fn, void* context) {
  if (!initialized_ || byte_budget == 0U) {
    return;
  }
  const uint32_t started_us = micros();
  ++ingress_stats_.service_calls;
  pollUartErrors();

  const uint32_t dma_error_events = dma_error_events_.consume();
  if (dma_error_events != 0U) {
    ingress_stats_.dma_transfer_errors = saturatingFeederCounterAdd(
        ingress_stats_.dma_transfer_errors, dma_error_events);
    HAL_UART_DMAStop(&g_uart);
    decoder_.resetFraming();
    if (startDma()) {
      ++ingress_stats_.dma_restarts;
    } else {
      initialized_ = false;
      return;
    }
  }

  uint64_t produced = 0;
  if (!producedTotal(&produced)) {
    // The producer position can no longer be reconstructed without risking
    // duplicate or silently skipped bytes. Stop this ingress epoch and require
    // an explicit board recovery instead of resetting the consumer cursor.
    HAL_UART_DMAStop(&g_uart);
    decoder_.resetFraming();
    initialized_ = false;
    ingress_stats_.max_service_us =
        std::max(ingress_stats_.max_service_us, micros() - started_us);
    return;
  }
  uint64_t available = produced - consumed_total_;
  if (available > kDmaBufferSize) {
    const uint64_t lost = available - kDmaBufferSize;
    ingress_stats_.dma_overrun_bytes +=
        lost > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(lost);
    consumed_total_ = produced - kDmaBufferSize;
    available = kDmaBufferSize;
    decoder_.resetFraming();
  }
  ingress_stats_.dma_buffer_high_water =
      std::max(ingress_stats_.dma_buffer_high_water,
               static_cast<uint32_t>(available));

  size_t remaining_budget =
      static_cast<size_t>(std::min<uint64_t>(available, byte_budget));
  decoder_.setCallback(frame_fn, context);
  while (remaining_budget > 0U) {
    if (service_time_budget_us_ > 0U &&
        static_cast<uint32_t>(micros() - started_us) >=
            service_time_budget_us_) {
      ++ingress_stats_.service_time_budget_hits;
      break;
    }
    const size_t offset =
        static_cast<size_t>(consumed_total_ & (kDmaBufferSize - 1U));
    const size_t chunk =
        std::min(
            std::min(remaining_budget, kDmaBufferSize - offset),
            kServiceChunkBytes);
    invalidateRange(offset, chunk);
    decoder_.push(&g_dma_buffer[offset], chunk, arrival_mono_us);
    consumed_total_ += chunk;
    remaining_budget -= chunk;
    ingress_stats_.bytes_consumed += static_cast<uint32_t>(chunk);
  }

  ingress_stats_.max_service_us =
      std::max(ingress_stats_.max_service_us, micros() - started_us);
}

bool FeederUartIngress::stale(uint64_t now_mono_us) const {
  const uint64_t last = decoder_.lastPacketMonoUs();
  if (!initialized_ || last == 0U || now_mono_us < last) {
    return true;
  }
  return now_mono_us - last >
         static_cast<uint64_t>(stale_timeout_ms_) * 1000ULL;
}

bool FeederUartIngress::statusFresh(uint64_t now_mono_us) const {
  const uint64_t last = decoder_.lastStatusMonoUs();
  if (!initialized_ || !decoder_.statusValid() || last == 0U ||
      now_mono_us < last) {
    return false;
  }
  return now_mono_us - last <=
         static_cast<uint64_t>(stale_timeout_ms_) * 1000ULL;
}

void FeederUartIngress::handleDmaIrq() { HAL_DMA_IRQHandler(&g_dma); }

void FeederUartIngress::noteDmaComplete() { ++g_dma_wraps; }

void FeederUartIngress::noteDmaError() {
  dma_error_events_.publishFromIsr();
}

}  // namespace csm::board::feeder

extern "C" void DMA1_Stream0_IRQHandler(void) {
  if (csm::board::feeder::g_active != nullptr) {
    csm::board::feeder::g_active->handleDmaIrq();
  }
}
