#pragma once

#if !defined(CORE_CM4)
#error "M4Fdcan1Owner may only be compiled into the Cortex-M4 image"
#endif

#include <Arduino.h>
#include <stdint.h>

#include "board/control_island/ControlIslandSharedMemory.h"
#include "board/control_island/M4StaticCyclicExecutor.h"
#include "hal/can_api.h"
#include "objects.h"

namespace csm::board::control_island {

class M4Fdcan1Owner final : public M4LaneDriver {
 public:
  bool begin(uint32_t m4_boot_id, M4StaticCyclicExecutor* executor,
             BringupTracePayload* trace);
  void serviceInterrupt();
  void serviceRxInterrupt(uint32_t interrupt_flags);
  void noteTxComplete(uint32_t buffer_indexes);
  void noteTxAbort(uint32_t buffer_indexes);
  void noteError(uint32_t error_status);

  void consumeLatchedEvents() override;
  bool ready() const override;
  bool errorWarning() const override;
  bool errorPassive() const override;
  bool busOff() const override;
  TxRequestResult request(uint8_t lane, const uint8_t data[8]) override;
  bool cancel(uint8_t lane) override;
  bool pending(uint8_t lane) const override;
  FdcanRawSnapshot rawSnapshot() const override;

  uint32_t m4BootId() const { return m4_boot_id_; }
  uint32_t rawCaptureDropCount() const { return rx_fifo_lost_count_; }
  void augmentHealth(ControlHealthPayload* health) const;

 private:
  bool configureDirectHal();
  bool configureInterrupts();
  void updateProtocolState();
  static uint32_t bufferMask(uint8_t lane);
  void stage(BringupStage stage, BringupFailure failure = BringupFailure::None,
             uint32_t detail = 0u);

  can_t can_ = {};
  FDCAN_HandleTypeDef* handle_ = nullptr;
  M4StaticCyclicExecutor* executor_ = nullptr;
  FDCAN_ProtocolStatusTypeDef protocol_ = {};
  FDCAN_ErrorCountersTypeDef errors_ = {};
  uint32_t m4_boot_id_ = 0;
  uint32_t rx_capture_sequence_ = 0;
  uint32_t rx_fifo_lost_count_ = 0;
  bool fdcan_ready_ = false;
  bool clock_contract_ok_ = false;
  bool protocol_fault_latched_ = false;
  volatile uint32_t error_status_latch_ = 0;
  volatile uint32_t fdcan_irq_total_ = 0;
  volatile uint32_t tx_complete_callback_total_ = 0;
  volatile uint32_t tx_abort_callback_total_ = 0;
  volatile uint32_t error_callback_total_ = 0;
  volatile uint32_t last_error_callback_status_ = 0;
  uint32_t accepted_buffer_mask_ = 0;
  uint32_t add_failure_total_ = 0;
  uint32_t enable_failure_total_ = 0;
  uint32_t abort_failure_total_ = 0;
  uint32_t fdcan_kernel_clock_hz_ = 0;
  uint32_t nominal_bitrate_ = 0;
  BringupTracePayload* trace_ = nullptr;
};

class M4ControlTimebase {
 public:
  bool begin(M4StaticCyclicExecutor* executor);
  void serviceInterrupt();
  uint32_t timerClockHz() const { return timer_clock_hz_; }
  bool hasTicked() const { return first_tick_seen_; }

 private:
  M4StaticCyclicExecutor* executor_ = nullptr;
  uint32_t timer_clock_hz_ = 0;
  volatile bool first_tick_seen_ = false;
};

}  // namespace csm::board::control_island
