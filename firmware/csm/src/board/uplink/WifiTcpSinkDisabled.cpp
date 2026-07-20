#include "board/uplink/WifiTcpSink.h"

namespace csm::board::uplink {

bool WifiTcpSink::begin(const WifiTcpSinkConfig& config) {
  config_ = config;
  counters_ = {};
  blocked_since_ms_ = 0;
  enabled_ = false;
  client_active_ = false;
  return false;
}

bool WifiTcpSink::enabled() const { return false; }

bool WifiTcpSink::connected() const { return false; }

SinkOfferResult WifiTcpSink::offer(const PublishedFrameView&) {
  return SinkOfferResult::Disabled;
}

SinkServiceResult WifiTcpSink::service(uint32_t, uint32_t, uint32_t) { return {}; }

void WifiTcpSink::abortQueuedFrames() { queue_.clear(); }

Stream* WifiTcpSink::downlinkStream() { return nullptr; }

void WifiTcpSink::noteBackpressure(uint32_t, SinkServiceResult&) {}

}  // namespace csm::board::uplink
