#include "board/uplink/UsbCdcSink.h"
#include "board/observability/DebugObservation.h"

#include <Arduino.h>

#if defined(SERIAL_CDC)
#include "USB/PluggableUSBSerial.h"
#endif

#ifndef BOARD_SERIAL_TX_CHUNK_BYTES
#define BOARD_SERIAL_TX_CHUNK_BYTES 512
#endif

namespace csm::board::uplink {

UsbCdcSink::UsbCdcSink() : queue_(queue_storage_) {}

void UsbCdcSink::begin(const UsbCdcSinkConfig& config) {
  config_ = config;
  counters_ = {};
  blocked_since_ms_ = 0;
  connected_last_ = connected();
  if (connected_last_) {
    counters_.connection_epoch = 1;
    counters_.connect_total = 1;
  }
}

bool UsbCdcSink::enabled() const {
#if defined(SERIAL_CDC)
  return true;
#else
  return false;
#endif
}

bool UsbCdcSink::connected() const {
#if defined(SERIAL_CDC)
  return _SerialUSB.connected();
#else
  return false;
#endif
}

SinkOfferResult UsbCdcSink::offer(const PublishedFrameView& frame) {
  if (!enabled()) return SinkOfferResult::Disabled;
  if (!connected()) {
    counters_.offer_disconnected_total++;
    return SinkOfferResult::Disconnected;
  }
  if (!queue_.push(frame)) {
    counters_.offer_overflow_total++;
    return SinkOfferResult::Overflow;
  }
  counters_.offer_accept_total++;
  if (!counters_.first_accepted_valid) {
    counters_.first_accepted_valid = true;
    counters_.first_accepted_publish_seq = frame.publish_seq;
  }
  counters_.last_accepted_publish_seq = frame.publish_seq;
  counters_.queue_high_water_bytes = queue_.highWaterBytes();
  counters_.queue_high_water_records = queue_.highWaterRecords();
  return SinkOfferResult::Accepted;
}

SinkServiceResult UsbCdcSink::service(uint32_t byte_budget, uint32_t now_ms,
                                      uint32_t now_us) {
  SinkServiceResult result;
  result.epoch_changed = updateConnectionState();
#if defined(SERIAL_CDC)
  if (!connected() || byte_budget == 0) return result;
  const uint32_t start_us = now_us;
  const uint32_t configured_bytes =
      config_.max_bytes_per_pump == 0 ? byte_budget : config_.max_bytes_per_pump;
  const uint32_t pump_budget = byte_budget < configured_bytes ? byte_budget : configured_bytes;
  const uint32_t max_writes =
      config_.max_writes_per_pump == 0 ? 0xFFFFFFFFu : config_.max_writes_per_pump;
  uint32_t writes = 0;

  while (result.actual_bytes < pump_budget && writes < max_writes) {
    if (config_.drain_time_budget_us > 0 &&
        static_cast<uint32_t>(micros() - start_us) >= config_.drain_time_budget_us) break;
    const uint32_t budget_left = pump_budget - result.actual_bytes;
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
    // OBS_BOUNDARY:usb_debug_drain DEBUG_TRACE. Never interrupt a canonical frame:
    // begin debug only when canonical queue is empty; finish its partial frame
    // before returning to canonical bytes. Same write/time/byte budget, no retry.
    if(observation_length_ || queue_.empty()) {
      if(!observation_length_) {
        observation_length_=static_cast<uint16_t>(observation::nextFrame(
          observation_frame_,sizeof(observation_frame_)));
        observation_offset_=0;
      }
      if(observation_length_) {
        const uint16_t remaining=observation_length_-observation_offset_;
        const uint16_t requested=static_cast<uint16_t>(budget_left<remaining?budget_left:remaining);
        uint32_t actual=0;
        _SerialUSB.send_nb(observation_frame_+observation_offset_,requested,&actual,true);
        if(actual>requested) actual=requested;
        ++writes; result.actual_bytes+=actual;
        ++counters_.write_attempt_total;
        observation_offset_=static_cast<uint16_t>(observation_offset_+actual);
        if(observation_offset_==observation_length_) observation_length_=0;
        if(actual<requested) {
          if(actual==0) ++counters_.zero_write_total;
          else ++counters_.partial_write_total;
          // Same physical USB stall contract. Do not generate a canonical
          // BOARD_EVENT for each debug-only would-block/recovery transition.
          SinkServiceResult debug_backpressure;
          noteBackpressure(now_ms,debug_backpressure);
          break;
        }
        if(blocked_since_ms_!=0) {
          const uint32_t duration=now_ms-blocked_since_ms_;
          if(duration>counters_.backpressure_max_duration_ms)
            counters_.backpressure_max_duration_ms=duration;
          blocked_since_ms_=0;
        }
        continue;
      }
    }
#endif
    if(queue_.empty()) break;
    const uint16_t stage_capacity = static_cast<uint16_t>(
        budget_left < BOARD_SERIAL_TX_CHUNK_BYTES
            ? budget_left
            : BOARD_SERIAL_TX_CHUNK_BYTES);
    const uint16_t requested =
        queue_.copyFrontBytes(tx_stage_, stage_capacity);
    if (requested == 0) break;

    uint32_t actual = 0;
    _SerialUSB.send_nb(tx_stage_, requested, &actual, true);
    if (actual > requested) actual = requested;
    writes++;
    counters_.write_attempt_total++;
    if (actual == 0) {
      counters_.zero_write_total++;
      noteBackpressure(now_ms, result);
      break;
    }
    const TxQueue::ConsumeResult consumed = queue_.consumeMany(actual);
    result.actual_bytes += actual;
    result.frames_completed += consumed.frames;
    counters_.bytes_sent_total += actual;
    applyUsbCompletionEvidence(counters_, consumed.frames,
                               consumed.last_publish_seq);
    if (actual < requested) {
      counters_.partial_write_total++;
      noteBackpressure(now_ms, result);
      break;
    }
    if (blocked_since_ms_ != 0) {
      const uint32_t duration = now_ms - blocked_since_ms_;
      if (duration > counters_.backpressure_max_duration_ms)
        counters_.backpressure_max_duration_ms = duration;
      blocked_since_ms_ = 0;
      result.backpressure_event = true;
      result.backpressure_duration_ms = duration;
    }
  }
  if (blocked_since_ms_ != 0 && config_.stall_timeout_ms > 0 &&
      now_ms - blocked_since_ms_ >= config_.stall_timeout_ms) {
    abortQueuedFrames();
    counters_.connection_epoch++;
    blocked_since_ms_ = 0;
    result.epoch_changed = true;
  }
#else
  (void)byte_budget;
  (void)now_ms;
  (void)now_us;
#endif
  return result;
}

void UsbCdcSink::abortQueuedFrames() {
  CSM_OBS(if(observation_length_) ++observation::state().usb_aborted;
    observation_length_=0;observation_offset_=0);
  const uint32_t bytes = queue_.clear();
  if (bytes > 0) {
    counters_.queue_abort_total++;
    counters_.queue_aborted_bytes_total += bytes;
  }
}

bool UsbCdcSink::updateConnectionState() {
  const bool now_connected = connected();
  if (now_connected == connected_last_) return false;
  connected_last_ = now_connected;
  blocked_since_ms_ = 0;
  abortQueuedFrames();
  counters_.connection_epoch++;
  if (now_connected) counters_.connect_total++;
  else counters_.disconnect_total++;
  return true;
}

void UsbCdcSink::noteBackpressure(uint32_t now_ms, SinkServiceResult& result) {
  if (blocked_since_ms_ == 0) {
    blocked_since_ms_ = now_ms == 0 ? 1 : now_ms;
    counters_.backpressure_total++;
    result.backpressure_event = true;
  } else {
    const uint32_t duration = now_ms - blocked_since_ms_;
    if (duration > counters_.backpressure_max_duration_ms)
      counters_.backpressure_max_duration_ms = duration;
    result.backpressure_duration_ms = duration;
  }
}

}  // namespace csm::board::uplink
