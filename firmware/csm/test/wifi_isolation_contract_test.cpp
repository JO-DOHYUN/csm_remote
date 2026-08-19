#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>

#include "board/uplink/WifiWorkerMailbox.h"
#include "protocol/HostCommands.h"

namespace {

int failures = 0;

struct TestWifiMailboxStorage {
  csm::board::uplink::WifiWorkerMailbox::TxStorage storage;
};

static constexpr uint16_t kTestSessionAnchorLength = 1;

class TestWifiWorkerMailbox final
    : private TestWifiMailboxStorage,
      public csm::board::uplink::WifiWorkerMailbox {
 public:
  TestWifiWorkerMailbox()
      : csm::board::uplink::WifiWorkerMailbox(storage) {
    configureSession(1);
    activateLiveSession();
    uint8_t anchor_byte = 0;
    csm::board::uplink::PublishedFrameView anchor{
        &anchor_byte, kTestSessionAnchorLength, 0,
        csm::RecordType::StreamSession,
        csm::board::uplink::UplinkPriority::Critical};
    (void)tryOffer(anchor, 1);
  }
};

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

void acknowledge(csm::board::uplink::WifiWorkerMailbox& mailbox,
                 uint64_t sequence) {
  uint8_t payload[csm::kAppRxCommitAckPayloadLen] = {};
  csm::wr_u64_le(&payload[csm::kAppRxCommitAckBootSessionOffset], 1);
  csm::wr_u64_le(&payload[csm::kAppRxCommitAckPublishSeqOffset], sequence);
  uint8_t encoded[csm::encoded_typed_frame_len(
      csm::kAppRxCommitAckPayloadLen)] = {};
  size_t written = 0;
  CHECK(csm::encode_typed_frame(
      encoded, sizeof(encoded), csm::RecordType::AppRxCommitAck, payload,
      sizeof(payload), 0, 0, &written));
  CHECK(mailbox.pushRx(encoded, static_cast<uint16_t>(written)));
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

void testQueueHighWaterAndAbortGeneration() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  const uint8_t bytes[] = {1, 2, 3, 4};
  const uint8_t normal_capacity = static_cast<uint8_t>(
      BOARD_WIFI_SINK_QUEUE_RECORDS - BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS);
  for (uint8_t index = 0; index < normal_capacity; ++index) {
    CHECK(mailbox.tryOffer(
              makeFrame(bytes, sizeof(bytes), index, UplinkPriority::Normal), 10) ==
          WifiMailboxOfferResult::Accepted);
  }
  CHECK(mailbox.queueSnapshot().queued_records == normal_capacity);
  CHECK(!mailbox.queuePressureDisconnectLatched());
  CHECK(mailbox.tryOffer(
            makeFrame(bytes, sizeof(bytes), 99, UplinkPriority::Normal), 10) ==
        WifiMailboxOfferResult::Reserved);
  CHECK(mailbox.queuePressureDisconnectLatched());
  CHECK(mailbox.queuePressureDisconnectRequestSequence() == 1);
  CHECK(mailbox.tryOffer(
            makeFrame(bytes, sizeof(bytes), 200, UplinkPriority::Critical), 10) ==
        WifiMailboxOfferResult::Busy);
  CHECK(mailbox.queuePressureDisconnectRequestSequence() == 1);

  uint8_t staged[16] = {};
  WifiMailboxTxLease lease;
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  CHECK(lease.length != 0);
  WifiMailboxAbortResult aborted;
  CHECK(mailbox.tryApplyAbort(kTestSessionAnchorLength, aborted));
  CHECK(aborted.records == normal_capacity);
  CHECK(aborted.bytes ==
        static_cast<uint32_t>(
            normal_capacity) *
            sizeof(bytes));

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
  mailbox.activateLiveSession();
  uint8_t anchor_byte = 0;
  PublishedFrameView anchor{
      &anchor_byte, kTestSessionAnchorLength, 100,
      csm::RecordType::StreamSession, UplinkPriority::Critical};
  CHECK(mailbox.tryOffer(anchor, 20) == WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.tryOffer(
            makeFrame(bytes, sizeof(bytes), 101, UplinkPriority::Critical), 20) ==
        WifiMailboxOfferResult::Accepted);
}

void testPumpBudgetBoundsWritesAndBytes() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
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
  CHECK(mailbox.queueSnapshot().unsent_bytes == 24);
}

void testFreshAnchorPartialCompletionBoundary() {
  using namespace csm::board::uplink;
  CHECK(!wifiSessionAnchorCompleted(0, 8, 20));
  CHECK(!wifiSessionAnchorCompleted(8, 11, 20));
  CHECK(wifiSessionAnchorCompleted(8, 12, 20));
  CHECK(!wifiSessionAnchorCompleted(20, 1, 20));
  CHECK(wifiSendResultHasPositiveProgress(1));
  CHECK(!wifiSendResultHasPositiveProgress(0));
  CHECK(!wifiSendResultHasPositiveProgress(-1));
  CHECK(wifiPostSendIsolationDecision(2, 1, 9, 9) ==
        WifiPostSendIsolation::QueuePressure);
  CHECK(wifiPostSendIsolationDecision(1, 1, 10, 9) ==
        WifiPostSendIsolation::IsolationRequest);
  CHECK(wifiPostSendIsolationDecision(1, 1, 9, 9) ==
        WifiPostSendIsolation::None);
  CHECK(!wifiSocketSendPermitted(true, 1, 1, 9, 9));
  CHECK(!wifiSocketSendPermitted(false, 2, 1, 9, 9));
  CHECK(!wifiSocketSendPermitted(false, 1, 1, 10, 9));
  CHECK(wifiSocketSendPermitted(false, 1, 1, 9, 9));
  constexpr uint64_t kAcceptedPastWireWrap =
      static_cast<uint64_t>(UINT32_MAX) + 1024u;
  constexpr uint64_t kSentPastWireWrap =
      static_cast<uint64_t>(UINT32_MAX) + 1000u;
  CHECK(wifiPendingAcceptedBytes(
            kAcceptedPastWireWrap, kSentPastWireWrap, 4u) == 20u);
  CHECK(static_cast<uint32_t>(kAcceptedPastWireWrap) == 1023u);
}

void testQueueEnvelopeUsesExactEnabledRateWithoutUnqualifiedThresholds() {
  using namespace csm::board::uplink;
  CHECK(kProductEnabledWireBytesPerSecond == 131222);
  CHECK(kProductEnabledRecordsPerSecond == 1086);
  CHECK(BOARD_WIFI_SINK_QUEUE_BYTES == 49152);
  CHECK(BOARD_WIFI_SINK_QUEUE_RECORDS == 256);
  CHECK(BOARD_WIFI_TRANSIENT_COVERAGE_MS == 0);
  CHECK(BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES == 32768);
  CHECK(BOARD_WIFI_PRESSURE_LOW_WATER_BYTES == 8192);
  CHECK(BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS == 192);
  CHECK(BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS == 64);
  CHECK(kWifiFallbackIngressBytes == 657);
  CHECK(kWifiTransientIngressBytes == 0);
  CHECK(kWifiFallbackIngressRecords == 6);
  CHECK(kWifiTransientIngressRecords == 0);
  CHECK(BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES +
            csm::encoded_typed_frame_len(csm::kMaxPayloadLen) +
            kWifiFallbackIngressBytes <=
        BOARD_WIFI_SINK_QUEUE_BYTES -
            BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES);
  CHECK(BOARD_WIFI_TX_MAX_BYTES_PER_PUMP >=
        kWifiEnabledIngressBytesPerFallback);
}

void testPressureTrackerUsesHysteresisWithoutEpochClose() {
  using namespace csm::board::uplink;
  WifiQueuePressureTracker tracker;
  auto pressure = tracker.observe(
      100, BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES - 1u,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS - 1u,
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS,
      BOARD_WIFI_PRESSURE_LOW_WATER_BYTES,
      BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS);
  CHECK(!pressure.active);
  pressure = tracker.observe(
      110, BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES, 1,
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS,
      BOARD_WIFI_PRESSURE_LOW_WATER_BYTES,
      BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS);
  CHECK(pressure.entered);
  CHECK(pressure.active);
  pressure = tracker.observe(
      150, BOARD_WIFI_PRESSURE_LOW_WATER_BYTES,
      BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS + 1u,
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS,
      BOARD_WIFI_PRESSURE_LOW_WATER_BYTES,
      BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS);
  CHECK(pressure.active);
  CHECK(!pressure.recovered);
  pressure = tracker.observe(
      175, BOARD_WIFI_PRESSURE_LOW_WATER_BYTES,
      BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS,
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS,
      BOARD_WIFI_PRESSURE_LOW_WATER_BYTES,
      BOARD_WIFI_PRESSURE_LOW_WATER_RECORDS);
  CHECK(pressure.recovered);
  CHECK(!pressure.active);
  CHECK(pressure.duration_ms == 65);
}

void testSustainedProducerStaysWithinPumpEnvelope() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
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

void testHighWaterIsObservableBoundaryBeforeReserve() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  const uint8_t bytes[] = {1, 2, 3, 4};
  uint64_t sequence = 0;
  while (!wifiQueuePressureReached(
      mailbox.queueSnapshot().queued_bytes,
      mailbox.queueSnapshot().queued_records,
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS)) {
    CHECK(mailbox.tryOffer(
              makeFrame(bytes, sizeof(bytes), sequence++,
                        UplinkPriority::Normal),
              10) == WifiMailboxOfferResult::Accepted);
  }
  const WifiMailboxQueueSnapshot queued = mailbox.queueSnapshot();
  CHECK(wifiQueuePressureReached(
      queued.queued_bytes, queued.queued_records,
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS));
  CHECK(queued.queued_records <
        BOARD_WIFI_SINK_QUEUE_RECORDS -
            BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS);
  CHECK(!mailbox.queuePressureDisconnectLatched());
}

void testWouldBlockBelowHighWaterUsesOnlyTransmitTimeout() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
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
      after.queued_bytes, after.queued_records,
      BOARD_WIFI_PRESSURE_HIGH_WATER_BYTES,
      BOARD_WIFI_PRESSURE_HIGH_WATER_RECORDS);
  CHECK(pump.writes_attempted == 1);
  CHECK(pump.bytes_progressed == 0);
  CHECK(pump.would_block);
  CHECK(!pressure_after);
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
  TestWifiWorkerMailbox mailbox;
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
  TestWifiWorkerMailbox mailbox;
  uint8_t payload[500] = {};
  uint8_t frame[csm::encoded_typed_frame_len(sizeof(payload))] = {};
  size_t written = 0;
  CHECK(csm::encode_typed_frame(
      frame, sizeof(frame), csm::RecordType::HostHeartbeat, payload,
      sizeof(payload), 0, 0, &written));
  CHECK(mailbox.pushRx(frame, static_cast<uint16_t>(written)));
  CHECK(mailbox.pushRx(frame, static_cast<uint16_t>(written)));
  CHECK(!mailbox.pushRx(frame, static_cast<uint16_t>(written)));
  CHECK(mailbox.rxAvailable() == written * 2);
  CHECK(mailbox.peekRx() == csm::kFrameSof0);
  CHECK(mailbox.readRx() == csm::kFrameSof0);
  CHECK(mailbox.peekRx() == csm::kFrameSof1);
  mailbox.discardRx();
  CHECK(mailbox.rxAvailable() == 0);

  const uint32_t initial_generation = mailbox.rxEpochGeneration();
  CHECK((initial_generation & 1u) == 0u);
  mailbox.invalidateRxEpoch();
  const uint32_t invalid_generation = mailbox.rxEpochGeneration();
  CHECK((invalid_generation & 1u) != 0u);
  CHECK(mailbox.rxAvailable() == 0);
  CHECK(mailbox.readRx() == -1);
  mailbox.resetRxForNewEpoch();
  CHECK((mailbox.rxEpochGeneration() & 1u) == 0u);
  CHECK(mailbox.rxEpochGeneration() > invalid_generation);

  // Race a facade consumer against worker epoch invalidation. A stale plain
  // tail store could move the tail behind the discard and corrupt the next
  // epoch; CAS tail ownership plus generation fencing must preserve it.
  uint8_t small_payload[4] = {0x11, 0x22, 0x33, 0x44};
  uint8_t small_frame[csm::encoded_typed_frame_len(sizeof(small_payload))] = {};
  size_t small_written = 0;
  CHECK(csm::encode_typed_frame(
      small_frame, sizeof(small_frame), csm::RecordType::HostHeartbeat,
      small_payload, sizeof(small_payload), 0, 0, &small_written));
  for (uint32_t cycle = 0; cycle < 200; ++cycle) {
    mailbox.resetRxForNewEpoch();
    CHECK(mailbox.pushRx(small_frame, static_cast<uint16_t>(small_written)));
    std::atomic<bool> start{false};
    std::thread consumer([&] {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      while (mailbox.readRx() >= 0) {
      }
    });
    start.store(true, std::memory_order_release);
    if ((cycle & 1u) != 0u) std::this_thread::yield();
    mailbox.invalidateRxEpoch();
    consumer.join();
    CHECK((mailbox.rxEpochGeneration() & 1u) != 0u);
    CHECK(mailbox.rxAvailable() == 0);

    mailbox.resetRxForNewEpoch();
    CHECK(mailbox.pushRx(small_frame, static_cast<uint16_t>(small_written)));
    for (size_t index = 0; index < small_written; ++index) {
      CHECK(mailbox.readRx() == small_frame[index]);
    }
    CHECK(mailbox.readRx() == -1);
  }
}

void testCallBoundarySnapshot() {
  using namespace csm::board::uplink;
  const WifiWorkerCallSnapshot invalid;
  CHECK(!invalid.coherent);
  CHECK(!invalid.in_progress);

  TestWifiWorkerMailbox mailbox;
  mailbox.beginCall(WifiWorkerCallPhase::Receive, 100);
  WifiWorkerCallSnapshot call = mailbox.callSnapshot();
  CHECK(call.coherent);
  CHECK(call.in_progress);
  CHECK(call.phase == WifiWorkerCallPhase::Receive);
  CHECK(call.sequence == 1);
  CHECK(call.started_ms == 100);
  mailbox.endCall(101, 1200, -3001);
  call = mailbox.callSnapshot();
  CHECK(call.coherent);
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

void testStatePublicationIsCoherentAndNeverDropsFinalWrite() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  constexpr uint32_t kLastRevision = 50000;
  std::atomic<bool> start{false};
  std::atomic<bool> done{false};
  std::thread writer([&] {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (uint32_t revision = 1; revision <= kLastRevision; ++revision) {
      WifiWorkerStateSnapshot state;
      state.connected = (revision & 1u) != 0u;
      state.heartbeat_ms = revision;
      state.counters.connection_epoch = revision;
      state.counters.connect_total = ~revision;
      state.counters.bytes_sent_total = revision * 3u;
      mailbox.publishState(state);
    }
    done.store(true, std::memory_order_release);
  });

  start.store(true, std::memory_order_release);
  while (!done.load(std::memory_order_acquire)) {
    WifiWorkerStateSnapshot state;
    if (!mailbox.tryReadState(state) || state.heartbeat_ms == 0) continue;
    CHECK(state.counters.connection_epoch == state.heartbeat_ms);
    CHECK(state.counters.connect_total == ~state.heartbeat_ms);
    CHECK(state.counters.bytes_sent_total == state.heartbeat_ms * 3u);
    CHECK(state.connected == ((state.heartbeat_ms & 1u) != 0u));
  }
  writer.join();

  WifiWorkerStateSnapshot final_state;
  CHECK(mailbox.tryReadState(final_state));
  CHECK(final_state.heartbeat_ms == kLastRevision);
  CHECK(final_state.counters.connection_epoch == kLastRevision);
  CHECK(final_state.counters.connect_total == ~kLastRevision);
  CHECK(final_state.counters.bytes_sent_total == kLastRevision * 3u);
}

void testRuntimeModeContract() {
  using namespace csm::board::uplink;
  WifiTcpSinkConfig config;
  CHECK(config.runtime_mode == WifiRuntimeMode::FullTcp);
  CHECK(config.startup_attempt_limit == BOARD_WIFI_STARTUP_ATTEMPT_LIMIT);
  CHECK(config.startup_retry_ms == BOARD_WIFI_STARTUP_RETRY_MS);
  CHECK(!wifiStartupAttemptsExhausted(1000, 0));
  CHECK(!wifiStartupAttemptsExhausted(2, 3));
  CHECK(wifiStartupAttemptsExhausted(3, 3));
  CHECK(wifiStartupRetryAllowed(
      WifiStartupFailureBoundary::BeforeApStart, false));
  CHECK(!wifiStartupRetryAllowed(
      WifiStartupFailureBoundary::OpaqueApStart, true));
  CHECK(!wifiStartupRetryAllowed(
      WifiStartupFailureBoundary::AfterApStarted, false));
  CHECK(wifiStartupRetryAllowed(
      WifiStartupFailureBoundary::AfterApStarted, true));
  CHECK(wifiObservedAgeMs(100, 101) == 0);
  CHECK(wifiObservedAgeMs(101, 100) == 1);
  CHECK(wifiObservedAgeMs(0x10u, 0xFFFFFFF0u) == 0x20u);
  CHECK(config.drain_time_budget_us ==
        BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US);
  CHECK(BOARD_WIFI_TX_DRAIN_TIME_BUDGET_US == 2000);
  CHECK(BOARD_WIFI_TX_MAX_WRITES_PER_PUMP == 4);
  CHECK(BOARD_WIFI_TX_MAX_BYTES_PER_PUMP == 11680);
  CHECK(BOARD_WIFI_TX_BATCH_TARGET_BYTES == 1460);
  CHECK(BOARD_WIFI_CONNECTED_FALLBACK_MS == 5);
  CHECK(BOARD_WIFI_TX_BATCH_MAX_LATENCY_MS == 20);
  CHECK(BOARD_WIFI_TX_LATENCY_BOUND_MAX_MS == 2);
  CHECK(BOARD_WIFI_STALL_TIMEOUT_MS == 5000);
  CHECK(BOARD_WIFI_CALL_STALL_TIMEOUT_MS == 5000);
  CHECK(!wifiRuntimeModeStartsWorker(WifiRuntimeMode::Disabled));
  CHECK(wifiRuntimeModeStartsWorker(WifiRuntimeMode::AccessPointOnly));
  CHECK(wifiRuntimeModeStartsWorker(WifiRuntimeMode::FullTcp));
  CHECK(!wifiRuntimeModeEnablesTcp(WifiRuntimeMode::Disabled));
  CHECK(!wifiRuntimeModeEnablesTcp(WifiRuntimeMode::AccessPointOnly));
  CHECK(wifiRuntimeModeEnablesTcp(WifiRuntimeMode::FullTcp));
}

void testTransmitNoProgressPolicy() {
  using namespace csm::board::uplink;
  constexpr uint32_t kProductTimeoutMs = BOARD_WIFI_STALL_TIMEOUT_MS;
  WifiTxProgressTracker tracker;
  auto observed = tracker.observe(100, false, kProductTimeoutMs);
  CHECK(observed.started);
  CHECK(!observed.close_no_progress);
  CHECK(observed.duration_ms == 0);

  observed = tracker.observe(100 + kProductTimeoutMs - 1u, false,
                             kProductTimeoutMs);
  CHECK(!observed.close_no_progress);
  CHECK(observed.duration_ms == kProductTimeoutMs - 1u);
  observed =
      tracker.observe(100 + kProductTimeoutMs, false, kProductTimeoutMs);
  CHECK(observed.close_no_progress);
  CHECK(observed.duration_ms == kProductTimeoutMs);

  observed =
      tracker.observe(101 + kProductTimeoutMs, true, kProductTimeoutMs);
  CHECK(observed.recovered);
  CHECK(observed.duration_ms == kProductTimeoutMs + 1u);
  CHECK(!tracker.active());

  // Millis wrap must retain unsigned elapsed-time semantics.
  tracker.observe(UINT32_MAX - 10u, false, 25);
  observed = tracker.observe(20, false, 25);
  CHECK(observed.close_no_progress);
  CHECK(observed.duration_ms == 31);
}

}  // namespace

int main() {
  testQueueHighWaterAndAbortGeneration();
  testPumpBudgetBoundsWritesAndBytes();
  testFreshAnchorPartialCompletionBoundary();
  testQueueEnvelopeUsesExactEnabledRateWithoutUnqualifiedThresholds();
  testPressureTrackerUsesHysteresisWithoutEpochClose();
  testSustainedProducerStaysWithinPumpEnvelope();
  testHighWaterIsObservableBoundaryBeforeReserve();
  testWouldBlockBelowHighWaterUsesOnlyTransmitTimeout();
  testSlowPositiveProgressRequestsOneAdmissionBoundaryClose();
  testRxEpochDiscardAndOverflow();
  testCallBoundarySnapshot();
  testStatePublicationIsCoherentAndNeverDropsFinalWrite();
  testRuntimeModeContract();
  testTransmitNoProgressPolicy();
  if (failures != 0) {
    std::cerr << failures << " Wi-Fi isolation contract checks failed\n";
    return 1;
  }
  std::cout << "Wi-Fi isolation contract PASS\n";
  return 0;
}
