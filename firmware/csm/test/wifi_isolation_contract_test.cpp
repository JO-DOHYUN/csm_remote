#include <cstdint>
#include <iostream>

#include "board/uplink/WifiWorkerMailbox.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition     \
                << '\n';                                                        \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

csm::board::uplink::PublishedFrameView makeFrame(
    const uint8_t* bytes, uint16_t length, uint64_t sequence,
    csm::board::uplink::UplinkPriority priority) {
  csm::board::uplink::PublishedFrameView frame;
  frame.bytes = bytes;
  frame.length = length;
  frame.publish_seq = sequence;
  frame.type = csm::RecordType::BoardEvent;
  frame.priority = priority;
  return frame;
}

void testQueueReserveAndAbortGeneration() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  const uint8_t bytes[] = {1, 2, 3, 4};
  const uint8_t normal_capacity = static_cast<uint8_t>(
      BOARD_WIFI_SINK_QUEUE_RECORDS - BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS);
  for (uint8_t index = 0; index < normal_capacity; ++index) {
    CHECK(mailbox.tryOffer(
              makeFrame(bytes, sizeof(bytes), index, UplinkPriority::Normal), 10) ==
          WifiMailboxOfferResult::Accepted);
  }
  CHECK(mailbox.tryOffer(
            makeFrame(bytes, sizeof(bytes), 99, UplinkPriority::Normal), 10) ==
        WifiMailboxOfferResult::Reserved);
  CHECK(mailbox.tryOffer(
            makeFrame(bytes, sizeof(bytes), 100, UplinkPriority::Critical), 10) ==
        WifiMailboxOfferResult::Accepted);

  uint8_t staged[16] = {};
  WifiMailboxTxLease lease;
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  CHECK(lease.length != 0);
  mailbox.requestAbort();
  uint32_t aborted = 0;
  CHECK(mailbox.tryApplyAbort(aborted));
  CHECK(aborted == static_cast<uint32_t>(normal_capacity + 1u) * sizeof(bytes));

  FixedFrameQueue<BOARD_WIFI_SINK_QUEUE_RECORDS>::ConsumeResult consumed;
  bool stale = false;
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(stale);
  CHECK(consumed.bytes == 0);
  CHECK(mailbox.queueSnapshot().queued_bytes == 0);
}

void testRxEpochDiscardAndOverflow() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  uint8_t bytes[BOARD_WIFI_RX_MAILBOX_BYTES] = {};
  for (uint32_t index = 0; index < sizeof(bytes); ++index) {
    bytes[index] = static_cast<uint8_t>(index);
  }
  CHECK(mailbox.pushRx(bytes, sizeof(bytes)));
  CHECK(!mailbox.pushRx(bytes, 1));
  CHECK(mailbox.rxAvailable() == sizeof(bytes));
  CHECK(mailbox.peekRx() == 0);
  CHECK(mailbox.readRx() == 0);
  CHECK(mailbox.peekRx() == 1);
  mailbox.discardRx();
  CHECK(mailbox.rxAvailable() == 0);
}

void testCallBoundarySnapshot() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  mailbox.beginCall(WifiWorkerCallPhase::Receive, 100);
  WifiWorkerCallSnapshot call = mailbox.callSnapshot();
  CHECK(call.in_progress);
  CHECK(call.phase == WifiWorkerCallPhase::Receive);
  CHECK(call.sequence == 1);
  CHECK(call.started_ms == 100);
  mailbox.endCall(101, 1200, -3001);
  call = mailbox.callSnapshot();
  CHECK(!call.in_progress);
  CHECK(call.duration_us == 1200);
  CHECK(call.result == -3001);
  CHECK(call.heartbeat_ms == 101);

  WifiWorkerStateSnapshot state;
  state.runtime_mode = WifiRuntimeMode::AccessPointOnly;
  state.worker_started = true;
  state.ap_ready = true;
  state.server_ready = false;
  state.tcp_enabled = false;
  state.running = true;
  state.network_ready = true;
  state.connected = true;
  state.stack_free_bytes = 12000;
  state.stack_max_used_bytes = 4384;
  state.counters.connection_epoch = 7;
  state.counters.worker_start_total = 1;
  state.counters.startup_attempt_total = 1;
  state.counters.ap_start_total = 1;
  mailbox.publishState(state);
  WifiWorkerStateSnapshot copied;
  CHECK(mailbox.tryReadState(copied));
  CHECK(copied.running);
  CHECK(copied.runtime_mode == WifiRuntimeMode::AccessPointOnly);
  CHECK(copied.worker_started);
  CHECK(copied.ap_ready);
  CHECK(!copied.server_ready);
  CHECK(!copied.tcp_enabled);
  CHECK(copied.network_ready);
  CHECK(copied.connected);
  CHECK(copied.stack_free_bytes == 12000);
  CHECK(copied.stack_max_used_bytes == 4384);
  CHECK(copied.counters.connection_epoch == 7);
  CHECK(copied.counters.worker_start_total == 1);
  CHECK(copied.counters.startup_attempt_total == 1);
  CHECK(copied.counters.ap_start_total == 1);
}

void testRuntimeModeContract() {
  using namespace csm::board::uplink;
  WifiTcpSinkConfig config;
  CHECK(config.runtime_mode == WifiRuntimeMode::FullTcp);
  CHECK(config.startup_attempt_limit == 1);
  CHECK(!wifiRuntimeModeStartsWorker(WifiRuntimeMode::Disabled));
  CHECK(wifiRuntimeModeStartsWorker(WifiRuntimeMode::AccessPointOnly));
  CHECK(wifiRuntimeModeStartsWorker(WifiRuntimeMode::FullTcp));
  CHECK(!wifiRuntimeModeEnablesTcp(WifiRuntimeMode::Disabled));
  CHECK(!wifiRuntimeModeEnablesTcp(WifiRuntimeMode::AccessPointOnly));
  CHECK(wifiRuntimeModeEnablesTcp(WifiRuntimeMode::FullTcp));
}

}  // namespace

int main() {
  testQueueReserveAndAbortGeneration();
  testRxEpochDiscardAndOverflow();
  testCallBoundarySnapshot();
  testRuntimeModeContract();
  if (failures != 0) {
    std::cerr << failures << " Wi-Fi isolation contract checks failed\n";
    return 1;
  }
  std::cout << "Wi-Fi isolation contract PASS\n";
  return 0;
}
