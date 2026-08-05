#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "board/feeder/FeederWireProtocol.h"

namespace csm::board::feeder {

struct FeederUartIngressConfig {
  uint32_t baud = 1000000;
  uint32_t stale_timeout_ms = 250;
  // Bounds decoder/callback CPU occupancy inside one main-loop service call.
  // Zero disables the time bound; the byte budget remains mandatory.
  uint32_t service_time_budget_us = 750;
};

struct FeederUartIngressStats {
  uint32_t dma_overrun_bytes = 0;
  uint32_t dma_transfer_errors = 0;
  uint32_t uart_error_events = 0;
  uint32_t dma_restarts = 0;
  uint32_t dma_buffer_high_water = 0;
  uint32_t max_service_us = 0;
  uint32_t service_calls = 0;
  uint32_t bytes_consumed = 0;
  uint32_t dma_wrap_reconciliations = 0;
  uint32_t dma_cursor_faults = 0;
  uint32_t service_time_budget_hits = 0;
};

struct FeederDmaCursorResult {
  uint64_t produced_total = 0;
  bool valid = false;
  bool reconciled_pending_wrap = false;
};

constexpr uint32_t saturatingFeederCounterAdd(uint32_t value,
                                               uint32_t increment) {
  return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

class FeederDmaErrorEvent {
 public:
  void reset() { pending_.store(0, std::memory_order_relaxed); }

  void publishFromIsr() {
    uint32_t observed = pending_.load(std::memory_order_relaxed);
    while (observed != UINT32_MAX &&
           !pending_.compare_exchange_weak(observed, observed + 1U,
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
    }
  }

  uint32_t consume() {
    return pending_.exchange(0, std::memory_order_acquire);
  }

 private:
  std::atomic<uint32_t> pending_{0};
};

// DMA circular mode reloads NDTR before the transfer-complete callback updates
// the software wrap count. Reconcile exactly that one pending wrap; any other
// backwards cursor is ambiguous and must fail closed instead of replaying data.
constexpr FeederDmaCursorResult reconcileFeederDmaCursor(
    uint32_t completed_wraps, uint32_t position, uint32_t buffer_size,
    bool transfer_complete_pending, uint64_t consumed_total) {
  FeederDmaCursorResult result;
  if (buffer_size == 0U || position > buffer_size) {
    return result;
  }
  result.produced_total =
      static_cast<uint64_t>(completed_wraps) * buffer_size + position;
  if (result.produced_total >= consumed_total) {
    result.valid = true;
    return result;
  }
  if (!transfer_complete_pending) {
    return result;
  }
  const uint64_t reconciled =
      result.produced_total + static_cast<uint64_t>(buffer_size);
  if (reconciled < consumed_total) {
    return result;
  }
  result.produced_total = reconciled;
  result.valid = true;
  result.reconciled_pending_wrap = true;
  return result;
}

class FeederUartIngress {
 public:
  bool begin(const FeederUartIngressConfig& config);
  void service(size_t byte_budget, uint64_t arrival_mono_us,
               FeederCanFrameFn frame_fn, void* context);

  bool initialized() const { return initialized_; }
  bool stale(uint64_t now_mono_us) const;
  bool statusFresh(uint64_t now_mono_us) const;
  const FeederUartIngressStats& ingressStats() const {
    return ingress_stats_;
  }
  const FeederWireStats& wireStats() const { return decoder_.stats(); }
  const FeederStatus& feederStatus() const { return decoder_.status(); }
  bool statusValid() const { return decoder_.statusValid(); }

  void handleDmaIrq();
  void noteDmaComplete();
  void noteDmaError();

 private:
  bool initialized_ = false;
  FeederDmaErrorEvent dma_error_events_;
  uint32_t stale_timeout_ms_ = 250;
  uint32_t service_time_budget_us_ = 750;
  uint64_t consumed_total_ = 0;
  FeederUartIngressStats ingress_stats_ = {};
  FeederWireDecoder decoder_;

  bool startDma();
  bool producedTotal(uint64_t* produced_total);
  void invalidateRange(size_t offset, size_t length);
  void pollUartErrors();
};

}  // namespace csm::board::feeder
