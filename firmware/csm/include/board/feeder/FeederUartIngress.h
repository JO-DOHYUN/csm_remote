#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board/feeder/FeederWireProtocol.h"

namespace csm::board::feeder {

struct FeederUartIngressConfig {
  uint32_t baud = 1000000;
  uint32_t stale_timeout_ms = 250;
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
};

class FeederUartIngress {
 public:
  bool begin(const FeederUartIngressConfig& config);
  void service(size_t byte_budget, uint64_t arrival_mono_us,
               FeederCanFrameFn frame_fn, void* context);

  bool initialized() const { return initialized_; }
  bool stale(uint64_t now_mono_us) const;
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
  bool restart_pending_ = false;
  uint32_t stale_timeout_ms_ = 250;
  uint64_t consumed_total_ = 0;
  FeederUartIngressStats ingress_stats_ = {};
  FeederWireDecoder decoder_;

  bool startDma();
  uint64_t producedTotal() const;
  void invalidateRange(size_t offset, size_t length);
  void pollUartErrors();
};

}  // namespace csm::board::feeder
