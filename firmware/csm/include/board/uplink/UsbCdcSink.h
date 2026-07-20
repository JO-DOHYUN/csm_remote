#pragma once

#include <stdint.h>

#include "board/uplink/FixedFrameQueue.h"

#ifndef BOARD_USB_SINK_QUEUE_RECORDS
#define BOARD_USB_SINK_QUEUE_RECORDS 8
#endif

namespace csm::board::uplink {

struct UsbCdcSinkConfig {
  uint32_t drain_time_budget_us = 0;
  uint32_t max_writes_per_pump = 0;
  uint32_t max_bytes_per_pump = 0;
  uint32_t stall_timeout_ms = 250;
};

struct UsbCdcSinkCounters {
  uint32_t offer_accept_total = 0;
  uint32_t offer_disconnected_total = 0;
  uint32_t offer_overflow_total = 0;
  uint32_t bytes_sent_total = 0;
  uint32_t frame_sent_total = 0;
  uint32_t write_attempt_total = 0;
  uint32_t partial_write_total = 0;
  uint32_t zero_write_total = 0;
  uint32_t backpressure_total = 0;
  uint32_t backpressure_max_duration_ms = 0;
  uint32_t queue_abort_total = 0;
  uint32_t queue_aborted_bytes_total = 0;
  uint32_t connection_epoch = 0;
  uint32_t connect_total = 0;
  uint32_t disconnect_total = 0;
  uint32_t queue_high_water_bytes = 0;
  uint32_t queue_high_water_records = 0;
  uint64_t first_accepted_publish_seq = 0;
  uint64_t last_accepted_publish_seq = 0;
  uint64_t last_sent_publish_seq = 0;
  bool first_accepted_valid = false;
};

class UsbCdcSink final : public IFrameSink {
 public:
  void begin(const UsbCdcSinkConfig& config);
  bool enabled() const override;
  bool connected() const override;
  SinkOfferResult offer(const PublishedFrameView& frame) override;
  SinkServiceResult service(uint32_t byte_budget, uint32_t now_ms, uint32_t now_us);
  void abortQueuedFrames();

  bool hasPendingFrames() const { return !queue_.empty(); }
  bool backpressureActive() const { return blocked_since_ms_ != 0; }
  uint32_t queuedBytes() const { return queue_.queuedBytes(); }
  const UsbCdcSinkCounters& counters() const { return counters_; }

 private:
  FixedFrameQueue<BOARD_USB_SINK_QUEUE_RECORDS> queue_;
  UsbCdcSinkConfig config_;
  UsbCdcSinkCounters counters_;
  uint32_t blocked_since_ms_ = 0;
  bool connected_last_ = false;

  bool updateConnectionState();
  void noteBackpressure(uint32_t now_ms, SinkServiceResult& result);
};

}  // namespace csm::board::uplink
