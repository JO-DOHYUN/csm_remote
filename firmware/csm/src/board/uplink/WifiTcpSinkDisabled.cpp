#include "board/uplink/WifiTcpSink.h"

namespace csm::board::uplink {

bool WifiTcpSink::begin(const WifiTcpSinkConfig& config) {
  config_ = config;
  counters_ = {};
  enabled_ = false;
  connected_ = false;
  return false;
}

bool WifiTcpSink::enabled() const { return false; }

bool WifiTcpSink::connected() const { return false; }

bool WifiTcpSink::socketConnected() const { return false; }

SinkOfferResult WifiTcpSink::offer(const PublishedFrameView&) {
  return SinkOfferResult::Disabled;
}

SinkServiceResult WifiTcpSink::service(uint32_t, uint32_t, uint32_t) { return {}; }

void WifiTcpSink::abortQueuedFrames() { mailbox_.requestAbort(); }

Stream* WifiTcpSink::downlinkStream() { return nullptr; }

int WifiTcpSink::available() { return 0; }

int WifiTcpSink::read() { return -1; }

int WifiTcpSink::peek() { return -1; }

uint32_t WifiTcpSink::workerHeartbeatAgeMs(uint32_t) const { return 0; }

LinkReliabilityDiagnosticSnapshot
WifiTcpSink::reliabilityDiagnosticSnapshot(uint64_t mono_us) const {
  LinkReliabilityDiagnosticSnapshot snapshot;
  snapshot.mono_us = mono_us;
  return snapshot;
}

}  // namespace csm::board::uplink
