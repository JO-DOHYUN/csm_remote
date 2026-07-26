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

enum class SimulatedSend {
  Progress,
  WouldBlock,
  ZeroWrite,
};

csm::board::uplink::WifiTransmitPumpResult simulateTransmitPump(
    csm::board::uplink::WifiWorkerMailbox& mailbox,
    uint32_t max_writes_per_pump, uint32_t max_bytes_per_pump,
    uint16_t chunk_bytes, SimulatedSend send) {
  using namespace csm::board::uplink;
  WifiTransmitPumpResult result;
  WifiTxPumpBudget budget(max_writes_per_pump, max_bytes_per_pump, chunk_bytes);
  uint8_t staged[64] = {};
  CHECK(chunk_bytes <= sizeof(staged));

  while (budget.canAttempt()) {
    WifiMailboxTxLease lease;
    CHECK(budget.nextWriteCapacity() <= sizeof(staged));
    if (!mailbox.tryStageTx(staged, budget.nextWriteCapacity(), lease)) break;

    uint16_t progressed = 0;
    if (send == SimulatedSend::Progress) {
      progressed = lease.length;
    } else if (send == SimulatedSend::WouldBlock) {
      result.would_block = true;
    } else {
      result.zero_write = true;
    }
    budget.noteAttempt(progressed);
    result.writes_attempted = budget.writesAttempted();
    result.bytes_progressed = budget.bytesProgressed();
    if (progressed == 0) break;

    WifiWorkerMailbox::TxConsumeResult consumed;
    bool stale = false;
    CHECK(mailbox.tryConsumeTx(lease, progressed, consumed, stale));
    CHECK(!stale);
  }
  return result;
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
  CHECK(mailbox.queuePressureDisconnectLatched());
  CHECK(mailbox.queuePressureDisconnectRequestSequence() == 1);
  CHECK(mailbox.tryOffer(
            makeFrame(bytes, sizeof(bytes), 100, UplinkPriority::Critical), 10) ==
        WifiMailboxOfferResult::Busy);
  CHECK(mailbox.queuePressureDisconnectRequestSequence() == 1);

  uint8_t staged[16] = {};
  WifiMailboxTxLease lease;
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  CHECK(lease.length != 0);
  uint32_t aborted = 0;
  CHECK(mailbox.tryApplyAbort(aborted));
  CHECK(aborted == static_cast<uint32_t>(normal_capacity) * sizeof(bytes));

  WifiWorkerMailbox::TxConsumeResult consumed;
  bool stale = false;
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(stale);
  CHECK(consumed.bytes == 0);
  CHECK(mailbox.queueSnapshot().queued_bytes == 0);

  CHECK(!mailbox.acknowledgeQueuePressureDisconnect(0));
  CHECK(mailbox.queuePressureDisconnectLatched());
  mailbox.markQueuePressureDisconnectHandled(1);
  CHECK(mailbox.acknowledgeQueuePressureDisconnect(
      mailbox.queuePressureDisconnectHandledSequence()));
  CHECK(!mailbox.queuePressureDisconnectLatched());
  CHECK(mailbox.tryOffer(
            makeFrame(bytes, sizeof(bytes), 101, UplinkPriority::Critical), 20) ==
        WifiMailboxOfferResult::Accepted);
}

void testPumpBudgetBoundsWritesAndBytes() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  const uint8_t bytes[16] = {};
  for (uint64_t sequence = 0; sequence < 3; ++sequence) {
    CHECK(mailbox.tryOffer(
              makeFrame(bytes, sizeof(bytes), sequence, UplinkPriority::Critical),
              10) == WifiMailboxOfferResult::Accepted);
  }

  const WifiTransmitPumpResult pump =
      simulateTransmitPump(mailbox, 2, 24, 16, SimulatedSend::Progress);
  CHECK(pump.writes_attempted == 2);
  CHECK(pump.bytes_progressed == 24);
  CHECK(!pump.would_block);
  CHECK(mailbox.queueSnapshot().queued_bytes == 24);
}

void testSustainedProducerStaysWithinPumpEnvelope() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  const uint8_t bytes[] = {1, 2, 3, 4};
  uint64_t sequence = 0;
  for (uint32_t round = 0; round < 64; ++round) {
    for (uint8_t produced = 0; produced < 2; ++produced) {
      CHECK(mailbox.tryOffer(
                makeFrame(bytes, sizeof(bytes), sequence++,
                          UplinkPriority::Normal),
                round) == WifiMailboxOfferResult::Accepted);
    }
    const WifiTransmitPumpResult pump =
        simulateTransmitPump(mailbox, 2, 8, 4, SimulatedSend::Progress);
    CHECK(pump.writes_attempted == 2);
    CHECK(pump.bytes_progressed == 8);
    CHECK(mailbox.queueSnapshot().queued_records == 0);
    CHECK(!mailbox.queuePressureDisconnectLatched());
  }
}

void testHighWaterAloneDoesNotIsolate() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  const uint8_t bytes[] = {1, 2, 3, 4};
  for (uint64_t sequence = 0; sequence < 7; ++sequence) {
    CHECK(mailbox.tryOffer(
              makeFrame(bytes, sizeof(bytes), sequence, UplinkPriority::Critical),
              10) == WifiMailboxOfferResult::Accepted);
  }
  CHECK(wifiQueuePressureReached(
      mailbox.queueSnapshot().queued_bytes,
      mailbox.queueSnapshot().queued_records, BOARD_WIFI_SINK_QUEUE_BYTES,
      BOARD_WIFI_SINK_QUEUE_RECORDS, 75));

  const WifiTransmitPumpResult pump =
      simulateTransmitPump(mailbox, 1, 4, 4, SimulatedSend::Progress);
  const WifiMailboxQueueSnapshot after = mailbox.queueSnapshot();
  const bool pressure_after = wifiQueuePressureReached(
      after.queued_bytes, after.queued_records, BOARD_WIFI_SINK_QUEUE_BYTES,
      BOARD_WIFI_SINK_QUEUE_RECORDS, 75);
  CHECK(pump.writes_attempted == 1);
  CHECK(pump.bytes_progressed == sizeof(bytes));
  CHECK(after.queued_records == 6);
  CHECK(pressure_after);
  CHECK(!mailbox.queuePressureDisconnectLatched());
}

void testWouldBlockAtHighWaterUsesOnlyTransmitTimeout() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  const uint8_t bytes[] = {1, 2, 3, 4};
  for (uint64_t sequence = 0; sequence < 6; ++sequence) {
    CHECK(mailbox.tryOffer(
              makeFrame(bytes, sizeof(bytes), sequence, UplinkPriority::Critical),
              10) == WifiMailboxOfferResult::Accepted);
  }

  WifiTransmitPumpResult pump =
      simulateTransmitPump(mailbox, 2, 8, 4, SimulatedSend::WouldBlock);
  WifiTxProgressTracker progress;
  const WifiTxProgressObservation first_block =
      progress.observe(1000, false, BOARD_WIFI_STALL_TIMEOUT_MS);
  pump.no_progress_duration_ms = first_block.duration_ms;
  const WifiMailboxQueueSnapshot after = mailbox.queueSnapshot();
  const bool pressure_after = wifiQueuePressureReached(
      after.queued_bytes, after.queued_records, BOARD_WIFI_SINK_QUEUE_BYTES,
      BOARD_WIFI_SINK_QUEUE_RECORDS, 75);
  CHECK(pump.writes_attempted == 1);
  CHECK(pump.bytes_progressed == 0);
  CHECK(pump.would_block);
  CHECK(pressure_after);
  CHECK(!mailbox.queuePressureDisconnectLatched());

  WifiTransmitPumpResult recovered =
      simulateTransmitPump(mailbox, 1, 4, 4, SimulatedSend::Progress);
  const WifiTxProgressObservation recovery =
      progress.observe(1005, true, BOARD_WIFI_STALL_TIMEOUT_MS);
  CHECK(recovery.recovered);
  CHECK(recovered.progressed());
  CHECK(!mailbox.queuePressureDisconnectLatched());
}

void testSlowPositiveProgressRequestsOneAdmissionBoundaryClose() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  const uint8_t bytes[] = {1, 2, 3, 4};
  uint64_t sequence = 0;
  while (!mailbox.queuePressureDisconnectLatched()) {
    const WifiMailboxOfferResult offered = mailbox.tryOffer(
        makeFrame(bytes, sizeof(bytes), sequence++, UplinkPriority::Normal), 10);
    if (offered == WifiMailboxOfferResult::Reserved) break;
    CHECK(offered == WifiMailboxOfferResult::Accepted);
    // The socket continues making positive progress, but slower than ingress.
    if ((sequence & 1u) == 0) {
      const WifiTransmitPumpResult pump =
          simulateTransmitPump(mailbox, 1, sizeof(bytes), sizeof(bytes),
                               SimulatedSend::Progress);
      CHECK(pump.progressed());
    }
  }
  CHECK(mailbox.queuePressureDisconnectLatched());
  CHECK(mailbox.queuePressureDisconnectRequestSequence() == 1);
  for (uint8_t repeat = 0; repeat < 4; ++repeat) {
    CHECK(mailbox.tryOffer(
              makeFrame(bytes, sizeof(bytes), sequence++,
                        UplinkPriority::Normal),
              10) == WifiMailboxOfferResult::Busy);
  }
  CHECK(mailbox.queuePressureDisconnectRequestSequence() == 1);
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

void testTransmitNoProgressPolicy() {
  using namespace csm::board::uplink;
  constexpr uint32_t kProductTimeoutMs = 2500;
  WifiTxProgressTracker tracker;
  auto observed = tracker.observe(100, false, kProductTimeoutMs);
  CHECK(observed.started);
  CHECK(!observed.close_no_progress);
  CHECK(observed.duration_ms == 0);

  observed = tracker.observe(2599, false, kProductTimeoutMs);
  CHECK(!observed.close_no_progress);
  CHECK(observed.duration_ms == 2499);
  observed = tracker.observe(2600, false, kProductTimeoutMs);
  CHECK(observed.close_no_progress);
  CHECK(observed.duration_ms == kProductTimeoutMs);

  observed = tracker.observe(2601, true, kProductTimeoutMs);
  CHECK(observed.recovered);
  CHECK(observed.duration_ms == 2501);
  CHECK(!tracker.active());

  // Millis wrap must retain unsigned elapsed-time semantics.
  tracker.observe(UINT32_MAX - 10u, false, 25);
  observed = tracker.observe(20, false, 25);
  CHECK(observed.close_no_progress);
  CHECK(observed.duration_ms == 31);
}

}  // namespace

int main() {
  testQueueReserveAndAbortGeneration();
  testPumpBudgetBoundsWritesAndBytes();
  testSustainedProducerStaysWithinPumpEnvelope();
  testHighWaterAloneDoesNotIsolate();
  testWouldBlockAtHighWaterUsesOnlyTransmitTimeout();
  testSlowPositiveProgressRequestsOneAdmissionBoundaryClose();
  testRxEpochDiscardAndOverflow();
  testCallBoundarySnapshot();
  testRuntimeModeContract();
  testTransmitNoProgressPolicy();
  if (failures != 0) {
    std::cerr << failures << " Wi-Fi isolation contract checks failed\n";
    return 1;
  }
  std::cout << "Wi-Fi isolation contract PASS\n";
  return 0;
}
