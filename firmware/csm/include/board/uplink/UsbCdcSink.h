#pragma once

#include <stdint.h>

#include "board/uplink/FixedFrameByteQueue.h"
#include "board/uplink/ProductUplinkEnvelope.h"

#ifndef BOARD_USB_SINK_QUEUE_RECORDS
#define BOARD_USB_SINK_QUEUE_RECORDS 208
#endif

#ifndef BOARD_USB_SINK_QUEUE_BYTES
#define BOARD_USB_SINK_QUEUE_BYTES 40960
#endif

#ifndef BOARD_USB_TRANSIENT_COVERAGE_MS
#define BOARD_USB_TRANSIENT_COVERAGE_MS 0
#endif

#ifndef BOARD_SERIAL_TX_CHUNK_BYTES
#define BOARD_SERIAL_TX_CHUNK_BYTES 512
#endif

namespace csm::board::uplink {

static constexpr uint32_t kUsbTransientIngressBytes =
    (static_cast<uint64_t>(kProductEnabledWireBytesPerSecond) *
         BOARD_USB_TRANSIENT_COVERAGE_MS +
     999u) /
    1000u;
static constexpr uint32_t kUsbTransientIngressRecords =
    (static_cast<uint64_t>(kProductEnabledRecordsPerSecond) *
         BOARD_USB_TRANSIENT_COVERAGE_MS +
     999u) /
    1000u;
static_assert(
    BOARD_USB_SINK_QUEUE_BYTES >=
        kUsbTransientIngressBytes +
            csm::encoded_typed_frame_len(csm::kMaxPayloadLen),
    "USB byte queue does not cover the declared transient envelope");
static_assert(BOARD_USB_SINK_QUEUE_RECORDS >=
                  kUsbTransientIngressRecords + 1u,
              "USB descriptor queue does not cover the declared transient envelope");

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

// A short USB write may finish one or more staged records before stopping in
// the next record. Completion evidence must be committed independently of
// whether the whole staged byte batch was accepted.
inline void applyUsbCompletionEvidence(UsbCdcSinkCounters& counters,
                                       uint32_t completed_frames,
                                       uint64_t last_publish_seq) {
  if (completed_frames == 0) return;
  counters.frame_sent_total += completed_frames;
  counters.last_sent_publish_seq = last_publish_seq;
}

class UsbCdcSink final : public IFrameSink {
 public:
  UsbCdcSink();
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
  using TxQueue = FixedFrameByteQueue<BOARD_USB_SINK_QUEUE_RECORDS,
                                      BOARD_USB_SINK_QUEUE_BYTES>;
  using TxStorage = typename TxQueue::Storage;

  TxStorage queue_storage_ = {};
  TxQueue queue_;
  uint8_t tx_stage_[BOARD_SERIAL_TX_CHUNK_BYTES] = {};
  UsbCdcSinkConfig config_;
  UsbCdcSinkCounters counters_;
  uint32_t blocked_since_ms_ = 0;
  bool connected_last_ = false;

  bool updateConnectionState();
  void noteBackpressure(uint32_t now_ms, SinkServiceResult& result);
};

}  // namespace csm::board::uplink
