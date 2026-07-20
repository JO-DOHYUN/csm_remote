#include "board/uplink/WifiTcpSink.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiServer.h>

namespace csm::board::uplink {

#if BOARD_ENABLE_WIFI_UPLINK
namespace {

class RawWifiServer final : public arduino::WiFiServer {
 public:
  TCPSocket* acceptRaw(nsapi_error_t* error) {
    if (sock == nullptr) {
      if (error != nullptr) *error = NSAPI_ERROR_NO_SOCKET;
      return nullptr;
    }
    return sock->accept(error);
  }
};

RawWifiServer wifi_server;

}  // namespace
#endif

bool WifiTcpSink::begin(const WifiTcpSinkConfig& config) {
  config_ = config;
  counters_ = {};
  blocked_since_ms_ = 0;
  batch_started_ms_ = 0;
  urgent_flush_ = false;
  enabled_ = false;
#if BOARD_ENABLE_WIFI_UPLINK
  client_socket_ = nullptr;
  client_active_ = false;
  socket_closed_ = false;
  rx_offset_ = 0;
  rx_length_ = 0;
  counters_.ap_start_total++;
  if (config_.ap_ssid == nullptr || config_.ap_passphrase == nullptr ||
      config_.port == 0) {
    counters_.ap_start_fail_total++;
    return false;
  }
  WiFi.config(IPAddress(config_.ip[0], config_.ip[1], config_.ip[2], config_.ip[3]));
  const int status = WiFi.beginAP(config_.ap_ssid, config_.ap_passphrase, config_.channel);
  if (status != WL_AP_LISTENING && status != WL_AP_CONNECTED) {
    counters_.ap_start_fail_total++;
    return false;
  }
  wifi_server.begin(config_.port);
  enabled_ = static_cast<bool>(wifi_server);
  if (!enabled_) counters_.ap_start_fail_total++;
#endif
  return enabled_;
}

bool WifiTcpSink::enabled() const { return enabled_; }

bool WifiTcpSink::connected() const {
#if BOARD_ENABLE_WIFI_UPLINK
  return enabled_ && client_active_ && client_socket_ != nullptr && !socket_closed_;
#else
  return false;
#endif
}

SinkOfferResult WifiTcpSink::offer(const PublishedFrameView& frame) {
  if (!enabled()) return SinkOfferResult::Disabled;
  if (!connected()) {
    counters_.offer_disconnected_total++;
    return SinkOfferResult::Disconnected;
  }
  const uint8_t normal_limit = static_cast<uint8_t>(
      BOARD_WIFI_SINK_QUEUE_RECORDS - BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS);
  if (frame.priority != UplinkPriority::Critical && queue_.count() >= normal_limit) {
    counters_.offer_overflow_total++;
    return SinkOfferResult::Overflow;
  }
  if (!queue_.push(frame)) {
    counters_.offer_overflow_total++;
    return SinkOfferResult::Overflow;
  }
  if (queue_.count() == 1) batch_started_ms_ = millis();
  if (frame.priority == UplinkPriority::Critical) urgent_flush_ = true;
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

SinkServiceResult WifiTcpSink::service(uint32_t byte_budget, uint32_t now_ms,
                                       uint32_t now_us) {
  SinkServiceResult result;
#if BOARD_ENABLE_WIFI_UPLINK
  if (!enabled_) return result;
  if (client_active_ && socket_closed_) {
    disconnectClient(false);
    result.epoch_changed = true;
  }
  if (!connected()) {
    if (client_active_) {
      disconnectClient(false);
      result.epoch_changed = true;
    }
    if (acceptClient()) result.epoch_changed = true;
  } else {
    rejectExtraClient();
  }
  if (!connected() || byte_budget == 0) return result;

  if (!queue_.empty() && !urgent_flush_ &&
      queue_.count() < config_.batch_min_records &&
      config_.batch_max_latency_ms > 0 &&
      now_ms - batch_started_ms_ < config_.batch_max_latency_ms) {
    return result;
  }

  const uint32_t configured_bytes =
      config_.max_bytes_per_pump == 0 ? byte_budget : config_.max_bytes_per_pump;
  const uint32_t pump_budget = byte_budget < configured_bytes ? byte_budget : configured_bytes;
  uint8_t tx_chunk[BOARD_WIFI_TX_CHUNK_BYTES];
  const uint16_t chunk_capacity = static_cast<uint16_t>(
      pump_budget < BOARD_WIFI_TX_CHUNK_BYTES ? pump_budget : BOARD_WIFI_TX_CHUNK_BYTES);
  const uint16_t requested = queue_.copyFrontBytes(tx_chunk, chunk_capacity);
  if (requested == 0) return result;

  counters_.write_attempt_total++;
  const uint32_t started_us = now_us == 0 ? micros() : now_us;
  const nsapi_size_or_error_t sent = client_socket_->send(tx_chunk, requested);
  const uint32_t duration_us = micros() - started_us;
  if (duration_us > counters_.send_call_max_us) counters_.send_call_max_us = duration_us;
  if (config_.drain_time_budget_us > 0 && duration_us > config_.drain_time_budget_us) {
    counters_.send_budget_overrun_total++;
  }

  if (sent < 0 && sent != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(sent));
    disconnectClient(false);
    result.epoch_changed = true;
    return result;
  }

  if (sent <= 0) {
    counters_.zero_write_total++;
    noteBackpressure(now_ms, result);
  } else {
    const uint16_t actual = sent > requested ? requested : static_cast<uint16_t>(sent);
    const auto consumed = queue_.consumeMany(actual);
    result.actual_bytes += actual;
    counters_.bytes_sent_total += actual;
    if (actual < requested) {
      counters_.partial_write_total++;
      noteBackpressure(now_ms, result);
    }
    if (consumed.frames > 0) {
      counters_.frame_sent_total += consumed.frames;
      counters_.last_sent_publish_seq = consumed.last_publish_seq;
      result.frames_completed += consumed.frames;
    }
    if (actual == requested && blocked_since_ms_ != 0) {
      const uint32_t duration = now_ms - blocked_since_ms_;
      if (duration > counters_.backpressure_max_duration_ms) {
        counters_.backpressure_max_duration_ms = duration;
      }
      blocked_since_ms_ = 0;
      result.backpressure_event = true;
      result.backpressure_duration_ms = duration;
    }
  }

  if (queue_.empty()) {
    batch_started_ms_ = 0;
    urgent_flush_ = false;
  } else if (batch_started_ms_ == 0) {
    batch_started_ms_ = now_ms;
  }

  if (blocked_since_ms_ != 0) {
    const uint32_t blocked_duration_ms = now_ms - blocked_since_ms_;
    if (blocked_duration_ms > counters_.backpressure_max_duration_ms) {
      counters_.backpressure_max_duration_ms = blocked_duration_ms;
    }
    result.backpressure_duration_ms = blocked_duration_ms;
    const uint8_t normal_limit = static_cast<uint8_t>(
        BOARD_WIFI_SINK_QUEUE_RECORDS - BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS);
    const bool timed_out = config_.stall_timeout_ms > 0 &&
                           blocked_duration_ms >= config_.stall_timeout_ms;
    const bool queue_pressure_close = queue_.count() >= normal_limit;
    if (timed_out || queue_pressure_close) {
      disconnectClient(true);
      result.backpressure_event = true;
      result.epoch_changed = true;
    }
  }
#else
  (void)byte_budget;
  (void)now_ms;
  (void)now_us;
#endif
  return result;
}

void WifiTcpSink::abortQueuedFrames() {
  const uint32_t bytes = queue_.clear();
  batch_started_ms_ = 0;
  urgent_flush_ = false;
  if (bytes > 0) {
    counters_.queue_abort_total++;
    counters_.queue_aborted_bytes_total += bytes;
  }
}

Stream* WifiTcpSink::downlinkStream() {
#if BOARD_ENABLE_WIFI_UPLINK
  return connected() ? this : nullptr;
#else
  return nullptr;
#endif
}

int WifiTcpSink::available() {
#if BOARD_ENABLE_WIFI_UPLINK
  if (rx_offset_ < rx_length_) return rx_length_ - rx_offset_;
  rx_offset_ = 0;
  rx_length_ = 0;
  if (!connected()) return 0;

  const uint32_t started_us = micros();
  const nsapi_size_or_error_t received = client_socket_->recv(rx_buffer_, sizeof(rx_buffer_));
  const uint32_t duration_us = micros() - started_us;
  if (duration_us > counters_.recv_call_max_us) counters_.recv_call_max_us = duration_us;
  if (received > 0) {
    rx_length_ = static_cast<uint16_t>(received);
    return rx_length_;
  }
  if (received == 0) {
    socket_closed_ = true;
  } else if (received != NSAPI_ERROR_WOULD_BLOCK) {
    noteSocketError(static_cast<nsapi_error_t>(received));
    socket_closed_ = true;
  }
#endif
  return 0;
}

int WifiTcpSink::read() {
  if (available() <= 0) return -1;
  return rx_buffer_[rx_offset_++];
}

int WifiTcpSink::peek() {
  if (available() <= 0) return -1;
  return rx_buffer_[rx_offset_];
}

#if BOARD_ENABLE_WIFI_UPLINK
bool WifiTcpSink::acceptClient() {
  nsapi_error_t error = NSAPI_ERROR_WOULD_BLOCK;
  TCPSocket* candidate = wifi_server.acceptRaw(&error);
  if (candidate == nullptr) {
    if (error != NSAPI_ERROR_WOULD_BLOCK) noteSocketError(error);
    return false;
  }
  candidate->set_blocking(false);
  client_socket_ = candidate;
  client_active_ = true;
  socket_closed_ = false;
  rx_offset_ = 0;
  rx_length_ = 0;
  blocked_since_ms_ = 0;
  batch_started_ms_ = 0;
  urgent_flush_ = false;
  abortQueuedFrames();
  counters_.connection_epoch++;
  counters_.connect_total++;
  return true;
}

void WifiTcpSink::disconnectClient(bool stalled) {
  const bool was_connected = client_active_;
  client_active_ = false;
  socket_closed_ = false;
  rx_offset_ = 0;
  rx_length_ = 0;
  blocked_since_ms_ = 0;
  batch_started_ms_ = 0;
  urgent_flush_ = false;
  abortQueuedFrames();

  TCPSocket* socket = client_socket_;
  client_socket_ = nullptr;
  if (socket != nullptr) {
    const uint32_t started_us = micros();
    const nsapi_error_t error = socket->close();
    const uint32_t duration_us = micros() - started_us;
    if (duration_us > counters_.close_call_max_us) counters_.close_call_max_us = duration_us;
    if (error != NSAPI_ERROR_OK) noteSocketError(error);
    delete socket;
  }
  if (!was_connected) return;
  counters_.connection_epoch++;
  counters_.disconnect_total++;
  if (stalled) counters_.stall_close_total++;
}

void WifiTcpSink::rejectExtraClient() {
  nsapi_error_t error = NSAPI_ERROR_WOULD_BLOCK;
  TCPSocket* extra = wifi_server.acceptRaw(&error);
  if (extra == nullptr) {
    if (error != NSAPI_ERROR_WOULD_BLOCK) noteSocketError(error);
    return;
  }
  extra->set_blocking(false);
  const uint32_t started_us = micros();
  const nsapi_error_t close_error = extra->close();
  const uint32_t duration_us = micros() - started_us;
  if (duration_us > counters_.close_call_max_us) counters_.close_call_max_us = duration_us;
  if (close_error != NSAPI_ERROR_OK) noteSocketError(close_error);
  delete extra;
  counters_.extra_client_reject_total++;
}

void WifiTcpSink::noteSocketError(nsapi_error_t error) {
  if (error != NSAPI_ERROR_OK && error != NSAPI_ERROR_WOULD_BLOCK) {
    counters_.socket_error_total++;
  }
}
#endif

void WifiTcpSink::noteBackpressure(uint32_t now_ms, SinkServiceResult& result) {
  if (blocked_since_ms_ == 0) {
    blocked_since_ms_ = now_ms == 0 ? 1 : now_ms;
    counters_.backpressure_total++;
    result.backpressure_event = true;
  } else {
    const uint32_t duration = now_ms - blocked_since_ms_;
    if (duration > counters_.backpressure_max_duration_ms) {
      counters_.backpressure_max_duration_ms = duration;
    }
    result.backpressure_duration_ms = duration;
  }
}

}  // namespace csm::board::uplink
