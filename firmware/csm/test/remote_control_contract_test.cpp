#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "board/control_island/ControlIslandSharedMemory.h"
#include "board/control_island/ControlSourceManager.h"
#include "board/control_island/M4StaticCyclicExecutor.h"
#include "board/control/HostCommandFreshness.h"
#include "board/control/HostControlSession.h"
#include "board/remote/CrsfParser.h"
#include "board/remote/R16smReceiverProfile.h"
#include "board/remote/RcNormalizer.h"
#include "board/remote/ReceiverAdmission.h"
#include "board/remote/RemoteControlSource.h"
#include "board/remote/RemoteTypes.h"
#include "protocol/ControlProtocol.h"
#include "protocol/TypedRecords.h"

using namespace csm::board::control_island;

namespace {
static_assert(kControlIslandSchemaId == csm::kControlIslandHealthSchemaId);
static_assert(kHno1WireContractId == csm::kControlIslandHealthWireContractId);
static_assert(kControlMemoryLayoutId == csm::kControlIslandHealthMemoryLayoutId);

struct FakeDriver final : M4LaneDriver {
  bool ready_value = true;
  bool passive = false;
  bool off = false;
  bool pending_value[kLaneCount] = {};
  bool cancel_accept = true;
  uint32_t requests[kLaneCount] = {};
  uint32_t cancels[kLaneCount] = {};
  uint8_t last_data[kLaneCount][8] = {};
  TxRequestResult next_result = TxRequestResult::Accepted;

  bool ready() const override { return ready_value && !off; }
  bool errorPassive() const override { return passive; }
  bool busOff() const override { return off; }
  TxRequestResult request(uint8_t lane, const uint8_t data[8]) override {
    if (lane >= kLaneCount || off || !ready_value) {
      return TxRequestResult::TransportUnavailable;
    }
    if (pending_value[lane]) return TxRequestResult::AlreadyPending;
    const TxRequestResult result = next_result;
    next_result = TxRequestResult::Accepted;
    if (result != TxRequestResult::Accepted) {
      if (result == TxRequestResult::EnableFailedAbortPending ||
          result == TxRequestResult::EnableFailedAbortFailed) {
        pending_value[lane] = true;
      }
      return result;
    }
    pending_value[lane] = true;
    ++requests[lane];
    memcpy(last_data[lane], data, 8u);
    return TxRequestResult::Accepted;
  }
  bool cancel(uint8_t lane) override {
    if (lane >= kLaneCount || !pending_value[lane]) return false;
    ++cancels[lane];
    return cancel_accept;
  }
  bool pending(uint8_t lane) const override {
    return lane < kLaneCount && pending_value[lane];
  }
  FdcanRawSnapshot rawSnapshot() const override { return {}; }
  void terminal(M4StaticCyclicExecutor* executor, uint8_t lane,
                bool transmitted, bool cancelled = false) {
    assert(lane < kLaneCount && pending_value[lane]);
    pending_value[lane] = false;
    executor->latchTerminalEvent(lane, transmitted, cancelled);
  }
  void resetHardware() { memset(pending_value, 0, sizeof(pending_value)); }
};

FinalControlSnapshotPayload makeSnapshot(uint32_t publish_sequence,
                                         uint32_t source_epoch,
                                         uint32_t activation_epoch,
                                         ControlSource source,
                                         uint32_t generation) {
  FinalControlSnapshotPayload snapshot;
  snapshot.m7_boot_id = 11u;
  snapshot.publish_sequence = publish_sequence;
  snapshot.source_epoch = source_epoch;
  snapshot.activation_epoch = activation_epoch;
  snapshot.active_source = static_cast<uint32_t>(source);
  snapshot.source_image_generation = generation;
  snapshot.source_lease_sequence = 7u;
  snapshot.permit_mask = source == ControlSource::Remote ? 0x03u : kAllLanePermitMask;
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    const bool owned = source == ControlSource::Host || lane != kLane364;
    snapshot.lanes[lane].valid = owned ? 1u : 0u;
    snapshot.lanes[lane].value_generation = generation;
    for (uint8_t byte = 0; byte < 8u; ++byte) {
      snapshot.lanes[lane].data[byte] = static_cast<uint8_t>(0x30u + lane * 16u + byte);
    }
  }
  return snapshot;
}

void testStaticDueAndExplicitRequestAccounting() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 300000u, &driver);
  driver.ready_value = false;
  for (uint32_t slot = 0; slot < 4u; ++slot) {
    executor.onFiveMillisecondSlot((slot + 1u) * 5000u);
  }
  assert(executor.health().lanes[kLane005].schedule_due == 4u);
  assert(executor.health().lanes[kLane007].schedule_due == 1u);
  assert(executor.health().lanes[kLane364].schedule_due == 1u);
  assert(executor.health().lanes[kLane005].transport_blocked == 4u);
  assert(executor.health().lanes[kLane007].transport_blocked == 1u);
  assert(executor.health().lanes[kLane364].policy_suppressed == 1u);

  driver.ready_value = true;
  driver.next_result = TxRequestResult::AddFailed;
  executor.onFiveMillisecondSlot(25000u);
  const LaneHealth& lane = executor.health().lanes[kLane005];
  assert(lane.request_attempt == 1u);
  assert(lane.request_accepted == 0u);
  assert(lane.request_failed == 1u);
  assert(lane.schedule_due == lane.pending_blocked + lane.policy_suppressed +
      lane.transport_blocked + lane.request_attempt);
}

void testPhysicalPendingAlwaysReconciles() {
  const TxRequestResult failures[] = {
      TxRequestResult::TransportUnavailable,
      TxRequestResult::AlreadyPending,
      TxRequestResult::AddFailed,
      TxRequestResult::EnableFailedNoPending,
  };
  for (TxRequestResult failure : failures) {
    FakeDriver driver;
    M4StaticCyclicExecutor executor;
    executor.begin(3u, 300000u, &driver);
    driver.next_result = failure;
    executor.onFiveMillisecondSlot(5000u);
    assert(executor.health().lanes[kLane005].state ==
           static_cast<uint8_t>(LaneState::Free));
    assert(executor.health().lanes[kLane005].request_failed == 1u);
  }
  FakeDriver aborting_driver;
  M4StaticCyclicExecutor aborting_executor;
  aborting_executor.begin(3u, 300000u, &aborting_driver);
  aborting_driver.next_result = TxRequestResult::EnableFailedAbortPending;
  aborting_executor.onFiveMillisecondSlot(5000u);
  assert(aborting_executor.health().lanes[kLane005].state ==
         static_cast<uint8_t>(LaneState::PendingSafe));
  assert(aborting_executor.health().lanes[kLane005].request_failed == 1u);
  assert(aborting_executor.health().lanes[kLane005].request_accepted == 0u);
  aborting_driver.terminal(&aborting_executor, kLane005, false, true);
  aborting_executor.onFiveMillisecondSlot(10000u);
  assert(aborting_executor.health().lanes[kLane005].cancel_count == 1u);
  FakeDriver fatal_driver;
  M4StaticCyclicExecutor fatal_executor;
  fatal_executor.begin(3u, 300000u, &fatal_driver);
  fatal_driver.next_result = TxRequestResult::EnableFailedAbortFailed;
  fatal_executor.onFiveMillisecondSlot(5000u);
  assert(fatal_executor.health().lanes[kLane005].tracking_fault == 1u);
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 300000u, &driver);
  executor.onFiveMillisecondSlot(5000u);
  assert(executor.health().lanes[kLane005].state ==
         static_cast<uint8_t>(LaneState::PendingSafe));
  assert(executor.health().lanes[kLane005].request_accepted == 1u);
}

void stage(M4StaticCyclicExecutor* executor,
           const FinalControlSnapshotPayload& snapshot, uint32_t at_us) {
  assert(executor->stageSnapshot(snapshot, at_us));
}

void closeAll(FakeDriver* driver, M4StaticCyclicExecutor* executor,
              bool transmitted = true) {
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if (driver->pending_value[lane]) {
      driver->terminal(executor, lane, transmitted, !transmitted);
    }
  }
}

void testSourceManagerOwnershipAndEpochs() {
  ControlSourceManager manager;
  manager.begin(9u);
  uint8_t a[8] = {1}, b[8] = {2}, c[8] = {3};
  assert(manager.acceptHostState(1u, kAllLanePermitMask, a, b, c, 1u));
  manager.select(ControlSource::Host);
  const uint32_t host_epoch = manager.sourceEpoch();
  auto host = manager.snapshot(kAllLanePermitMask, 41u);
  assert(host.source_epoch == host_epoch && host.activation_epoch == 41u);
  assert(host.permit_mask == 0x07u && host.lanes[kLane364].valid == 1u);
  LaneExecutionImage remote[kLaneCount] = {};
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    remote[lane].valid = 1u;
    remote[lane].data[0] = static_cast<uint8_t>(9u + lane);
  }
  manager.updateRemote(2u, 3u, remote, true);
  manager.select(ControlSource::Remote);
  auto rc = manager.snapshot(kAllLanePermitMask, 42u);
  assert(rc.source_epoch != host_epoch && rc.permit_mask == 0x03u);
  assert(rc.lanes[kLane005].valid && rc.lanes[kLane007].valid);
  assert(!rc.lanes[kLane364].valid);
}

void testReceiverQualifiedAdmissionAndOptionalStatistics() {
  using namespace csm::board::remote;
  ReceiverAdmission admission;
  ReceiverAdmissionConfig config;
  config.configured = true;
  config.receiver_address = kR16smCrsfAddress;
  config.consecutive_frames_required = kR16smAdmissionConsecutiveFrames;
  config.required_channel_mask = kR16smRequiredControlChannelMask;
  config.rc_freshness_ms = kR16smRcFreshnessMs;
  config.link_statistics_freshness_ms = kR16smLinkStatisticsFreshnessMs;
  assert(admission.configure(config));
  assert(!admission.observeRcFrame(10u, 0xEEu,
                                   kR16smRequiredControlChannelMask));
  assert(!admission.observeRcFrame(20u, kR16smCrsfAddress,
                                   kR16smRequiredControlChannelMask));
  assert(!admission.observeRcFrame(30u, kR16smCrsfAddress,
                                   kR16smRequiredControlChannelMask));
  assert(admission.observeRcFrame(40u, kR16smCrsfAddress,
                                  kR16smRequiredControlChannelMask));
  // R16SM captures without 0x14 remain admissible. If a receiver publishes
  // statistics, zero LQ or stale statistics become an additional veto.
  assert(admission.usable(40u, false, kRemoteMetricUnknown, UINT32_MAX));
  assert(!admission.usable(41u, true, 0u, 0u));
  assert(admission.usable(42u, true, 80u, 0u));
  assert(!admission.usable(43u, true, 80u,
                           kR16smLinkStatisticsFreshnessMs + 1u));
  assert(!admission.usable(200u, false, kRemoteMetricUnknown, UINT32_MAX));
  assert(!admission.observeRcFrame(201u, kR16smCrsfAddress,
                                   kR16smRequiredControlChannelMask));
}

void testCrsfStreamResynchronizationAndR16smFixture() {
  using namespace csm::board::remote;
  // Byte-exact frame reconstructed from the typed 2026-07-21 R16SM capture
  // (address/type/raw16); an independently captured raw UART fixture remains
  // physical qualification evidence rather than being claimed here.
  constexpr uint8_t fixture[] = {
      0xC8, 0x18, 0x16, 0xE0, 0x03, 0x1F, 0xF8, 0xC0, 0x07,
      0x3E, 0xF0, 0x19, 0xC5, 0x28, 0xE0, 0x33, 0x8A, 0x51,
      0xC0, 0x07, 0x3E, 0xF0, 0x81, 0x0F, 0x7C, 0x86};
  constexpr uint16_t expected[kRcChannelCount] = {
      992, 992, 992, 992, 992, 992, 326, 326,
      992, 326, 326, 992, 992, 992, 992, 992};
  for (size_t offset = 0; offset < sizeof(fixture); ++offset) {
    CrsfParser parser;
    CrsfRcChannels decoded;
    bool found = false;
    for (uint8_t repeat = 0; repeat < 3u && !found; ++repeat) {
      const size_t begin = repeat == 0u ? offset : 0u;
      for (size_t index = begin; index < sizeof(fixture); ++index) {
        const CrsfParseResult parsed = parser.ingest(fixture[index]);
        if (parsed.status == CrsfParseStatus::FrameReady &&
            decodeCrsfRcChannels(parsed.frame, &decoded) ==
                CrsfDecodeStatus::Ok) {
          found = true;
          break;
        }
      }
    }
    assert(found);
    assert(decoded.valid_mask == kR16smSixteenChannelMask);
    for (uint8_t channel = 0; channel < kRcChannelCount; ++channel) {
      assert(decoded.raw[channel] == expected[channel]);
    }
  }

  uint8_t corrupt[sizeof(fixture)] = {};
  memcpy(corrupt, fixture, sizeof(fixture));
  corrupt[7] ^= 0x01u;
  CrsfParser recovery;
  for (uint8_t byte : corrupt) (void)recovery.ingest(byte);
  bool recovered = false;
  for (uint8_t byte : fixture) {
    recovered |= recovery.ingest(byte).status == CrsfParseStatus::FrameReady;
  }
  assert(recovered && recovery.rejectedCrcTotal() != 0u);
}

void packLittleEndian(uint8_t* output, uint16_t bit_offset,
                      uint8_t bit_count, uint16_t value) {
  for (uint8_t bit = 0; bit < bit_count; ++bit) {
    if ((value & (1u << bit)) != 0u) {
      const uint16_t target = bit_offset + bit;
      output[target / 8u] |= static_cast<uint8_t>(1u << (target % 8u));
    }
  }
}

void testCrsfModernFramesAndChannelValidity() {
  using namespace csm::board::remote;
  CrsfFrame subset;
  subset.address = kR16smCrsfAddress;
  subset.type = kCrsfFrameTypeSubsetRcChannelsPacked;
  subset.payload_len = 6u;
  subset.payload[0] = 0x21u;  // CH2 first, 11-bit, analog encoding.
  packLittleEndian(&subset.payload[1], 0u, 11u, 992u);
  packLittleEndian(&subset.payload[1], 11u, 11u, 992u);
  packLittleEndian(&subset.payload[1], 22u, 11u, 992u);
  CrsfRcChannels subset_channels;
  assert(decodeCrsfRcChannels(subset, &subset_channels) ==
         CrsfDecodeStatus::Ok);
  assert(subset_channels.valid_mask == 0x000Eu);

  RcNormalizer normalizer;
  RcNormalizerConfig normalizer_config;
  normalizer_config.configured = true;
  normalizer_config.required_channel_mask = kR16smRequiredControlChannelMask;
  assert(normalizer.configure(normalizer_config));
  const RcNormalizeResult normalized = normalizer.normalizeCrsfChannels(
      10u, 1u, subset_channels, kRemoteMetricUnknown,
      kRemoteMetricUnknown, 0u);
  assert(normalized.accepted);
  assert(normalized.sample.channel_valid_mask == 0x000Eu);

  CrsfRcChannels eight_channels;
  eight_channels.count = 8u;
  eight_channels.valid_mask = 0x00FFu;
  for (uint8_t channel = 0; channel < 8u; ++channel) {
    eight_channels.raw[channel] = kCrsfRawDefaultMid;
  }
  const RcNormalizeResult eight = normalizer.normalizeCrsfChannels(
      20u, 2u, eight_channels, kRemoteMetricUnknown,
      kRemoteMetricUnknown, 0u);
  assert(eight.accepted && eight.sample.channel_valid_mask == 0x00FFu);
  M4RemoteMailboxSnapshot snapshot;
  snapshot.sample = eight.sample;
  snapshot.sample_present = true;
  snapshot.integrity_ok = true;
  snapshot.link_state = RemoteLinkState::Valid;
  RemoteControlSource source;
  source.begin(20u);
  assert(source.configure(RemoteControlSourceConfig{}));
  source.update(20u, snapshot, true, false);
  assert(source.readyForTakeover());
  assert(source.command().auxiliary_permille == 0);
  assert(source.command().steering_overlay_permille == 0);
  assert(source.command().momentary_overlay_permille == 0);

  const uint8_t rx_payload[5] = {60u, 90u, 75u, 4u, 10u};
  const uint8_t tx_payload[6] = {62u, 88u, 70u, 3u, 10u, 50u};
  const uint8_t link_types[] = {kCrsfFrameTypeLinkStatisticsRx,
                                kCrsfFrameTypeLinkStatisticsTx};
  for (uint8_t type : link_types) {
    uint8_t wire[kCrsfMaxFrameBytes] = {};
    const uint8_t* payload = type == kCrsfFrameTypeLinkStatisticsRx
        ? rx_payload : tx_payload;
    const uint8_t payload_len = type == kCrsfFrameTypeLinkStatisticsRx
        ? sizeof(rx_payload) : sizeof(tx_payload);
    const uint8_t length = buildCrsfBroadcastFrame(
        type, payload, payload_len, wire, sizeof(wire));
    CrsfParser parser;
    CrsfParseResult parsed;
    for (uint8_t index = 0; index < length; ++index) {
      parsed = parser.ingest(wire[index]);
    }
    CrsfLinkStatistics statistics;
    assert(parsed.status == CrsfParseStatus::FrameReady);
    assert(decodeCrsfLinkStatistics(parsed.frame, &statistics) ==
           CrsfDecodeStatus::Ok);
    assert(statistics.uplink_link_quality ==
           (type == kCrsfFrameTypeLinkStatisticsRx ? 75u : 70u));
  }
}

void testTransmitterOffOnAndHostToRcTakeover() {
  using namespace csm::board::remote;
  ReceiverAdmission admission;
  ReceiverAdmissionConfig admission_config;
  admission_config.configured = true;
  admission_config.receiver_address = kR16smCrsfAddress;
  admission_config.consecutive_frames_required =
      kR16smAdmissionConsecutiveFrames;
  admission_config.required_channel_mask = kR16smRequiredControlChannelMask;
  admission_config.rc_freshness_ms = kR16smRcFreshnessMs;
  admission_config.link_statistics_freshness_ms =
      kR16smLinkStatisticsFreshnessMs;
  assert(admission.configure(admission_config));

  ControlSourceManager manager;
  manager.begin(19u);
  uint8_t lane005[8] = {1u};
  uint8_t lane007[8] = {2u};
  uint8_t lane364[8] = {3u};
  assert(manager.acceptHostState(1u, kAllLanePermitMask,
                                 lane005, lane007, lane364, 1u));
  manager.select(ControlSource::Host);
  assert(manager.activeSource() == ControlSource::Host);

  assert(!admission.observeRcFrame(10u, kR16smCrsfAddress,
                                   kR16smRequiredControlChannelMask));
  assert(!admission.observeRcFrame(20u, kR16smCrsfAddress,
                                   kR16smRequiredControlChannelMask));
  assert(admission.observeRcFrame(30u, kR16smCrsfAddress,
                                  kR16smRequiredControlChannelMask));
  LaneExecutionImage remote[kLaneCount] = {};
  remote[kLane005].valid = 1u;
  remote[kLane007].valid = 1u;
  manager.updateRemote(1u, 1u, remote,
                       admission.usable(30u, false, kRemoteMetricUnknown,
                                        UINT32_MAX));
  manager.select(ControlSource::Remote);
  assert(manager.activeSource() == ControlSource::Remote);
  const FinalControlSnapshotPayload rc = manager.snapshot(0x03u, 2u);
  assert(rc.permit_mask == 0x03u && !rc.lanes[kLane364].valid);

  // Transmitter OFF revokes RC. ON requires a fresh three-frame sequence; an
  // old qualified state cannot preempt Host after the stale boundary.
  assert(!admission.usable(200u, false, kRemoteMetricUnknown, UINT32_MAX));
  manager.updateRemote(2u, 2u, remote, false);
  manager.select(ControlSource::Host);
  assert(manager.activeSource() == ControlSource::Host);
  assert(!admission.observeRcFrame(201u, kR16smCrsfAddress,
                                   kR16smRequiredControlChannelMask));
  assert(!admission.observeRcFrame(211u, kR16smCrsfAddress,
                                   kR16smRequiredControlChannelMask));
  assert(admission.observeRcFrame(221u, kR16smCrsfAddress,
                                  kR16smRequiredControlChannelMask));
}

void testSharedMemoryIntegrityAndBoundedRing() {
  initializeControlIpcForM7(17u);
  const uint32_t m4_boot_id = initializeControlIpcForM4();
  auto snapshot = makeSnapshot(1u, 1u, 1u, ControlSource::Host, 1u);
  snapshot.m7_boot_id = 17u;
  assert(publishFinalControlSnapshot(snapshot));
  assert(readFinalControlSnapshot(0u).accepted);
  ControlHealthPayload health;
  health.m4_boot_id = m4_boot_id;
  assert(publishControlHealth(health));
  assert(readControlHealth(0u).accepted);
  RawCanEntry raw;
  raw.can_id_flags = 0x123u;
  for (size_t i = 0; i < kRawCanRingCapacity; ++i) assert(pushRawCanFromM4(raw));
  assert(!pushRawCanFromM4(raw));
  for (size_t i = 0; i < kRawCanRingCapacity; ++i) assert(popRawCanForM7(&raw));
}

void testLongSafeCyclicAndRepeatedSafeStaging() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 300000u, &driver);
  auto safe = makeSnapshot(1u, 1u, 0u, ControlSource::None, 1u);
  safe.permit_mask = 0u;
  stage(&executor, safe, 100u);
  safe.publish_sequence = 2u;
  stage(&executor, safe, 200u);
  assert(driver.cancels[kLane005] == 0u);
  uint32_t now = 5000u;
  for (uint8_t slot = 0; slot < 40u; ++slot) {
    executor.onFiveMillisecondSlot(now);
    closeAll(&driver, &executor);
    now += 5000u;
  }
  assert(driver.requests[kLane005] == 40u);
  assert(driver.requests[kLane007] == 10u);
  assert(driver.requests[kLane364] == 0u);
  assert(memcmp(driver.last_data[kLane005], kLaneSafeWirePolicies[kLane005].idle_safe.data, 8u) == 0);
  assert(memcmp(driver.last_data[kLane007], kLaneSafeWirePolicies[kLane007].idle_safe.data, 8u) == 0);
}

void testTerminalCancelRaceAndNoTransitionSkip() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 300000u, &driver);
  auto active = makeSnapshot(1u, 1u, 1u, ControlSource::Host, 1u);
  stage(&executor, active, 100u);
  executor.onFiveMillisecondSlot(5000u);
  assert(driver.requests[kLane005] == 1u && executor.hasActiveControl());
  auto disarm = active;
  disarm.publish_sequence = 2u;
  disarm.permit_mask = 0u;
  stage(&executor, disarm, 5100u);
  driver.terminal(&executor, kLane005, true, true);
  driver.terminal(&executor, kLane007, false, true);
  driver.terminal(&executor, kLane364, false, true);
  executor.onFiveMillisecondSlot(10000u);
  assert(!executor.hasActiveControl());
  assert(driver.requests[kLane005] == 2u);
  assert(memcmp(driver.last_data[kLane005], kLaneSafeWirePolicies[kLane005].idle_safe.data, 8u) == 0);
  assert(executor.health().lanes[kLane005].cancel_race_count == 1u);
}

void testHostAndRcLaneOwnership() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 300000u, &driver);
  auto rc = makeSnapshot(1u, 2u, 1u, ControlSource::Remote, 1u);
  stage(&executor, rc, 100u);
  executor.onFiveMillisecondSlot(5000u);
  assert(driver.requests[kLane005] == 1u && driver.requests[kLane007] == 1u);
  assert(driver.requests[kLane364] == 0u);
  closeAll(&driver, &executor);
  executor.onFiveMillisecondSlot(10000u);
  auto host = makeSnapshot(2u, 3u, 2u, ControlSource::Host, 2u);
  stage(&executor, host, 10100u);
  executor.onFiveMillisecondSlot(15000u);
  closeAll(&driver, &executor, false);
  executor.onFiveMillisecondSlot(20000u);
  assert(executor.health().active_source_seen == static_cast<uint32_t>(ControlSource::Host));
  closeAll(&driver, &executor);
  executor.onFiveMillisecondSlot(25000u);
  assert(driver.requests[kLane364] == 1u);
}

void testSameSourceRearmAndStale() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 10000u, &driver);
  auto active = makeSnapshot(1u, 5u, 10u, ControlSource::Host, 1u);
  stage(&executor, active, 100u);
  executor.onFiveMillisecondSlot(5000u);
  closeAll(&driver, &executor);
  executor.onFiveMillisecondSlot(20000u);
  assert(!executor.hasActiveControl());
  ControlHealthPayload health;
  assert(executor.healthSnapshot(20000u, &health));
  assert(health.m7_stale_count == 1u);
  active.publish_sequence = 2u;
  stage(&executor, active, 20100u);
  executor.onFiveMillisecondSlot(25000u);
  assert(!executor.hasActiveControl());
  closeAll(&driver, &executor);
  active.publish_sequence = 3u;
  active.activation_epoch = 11u;
  stage(&executor, active, 25100u);
  executor.onFiveMillisecondSlot(30000u);
  assert(executor.hasActiveControl());
  assert(executor.health().source_epoch_seen == 5u);
  assert(executor.health().activation_epoch_seen == 11u);
}

void testErrorPassiveBusOffResetAndHealthPurity() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 300000u, &driver);
  auto active = makeSnapshot(1u, 1u, 1u, ControlSource::Host, 1u);
  stage(&executor, active, 100u);
  executor.onFiveMillisecondSlot(5000u);
  closeAll(&driver, &executor);
  const uint32_t cancels = driver.cancels[kLane005];
  ControlHealthPayload health;
  assert(executor.healthSnapshot(6000u, &health));
  assert(executor.healthSnapshot(7000u, &health));
  assert(driver.cancels[kLane005] == cancels && executor.hasActiveControl());
  driver.passive = true;
  executor.onFiveMillisecondSlot(10000u);
  assert(!executor.hasActiveControl());
  assert(driver.requests[kLane005] == 2u);
  assert(memcmp(driver.last_data[kLane005], kLaneSafeWirePolicies[kLane005].idle_safe.data, 8u) == 0);
  closeAll(&driver, &executor);
  driver.passive = false;
  driver.off = true;
  executor.onFiveMillisecondSlot(15000u);
  assert(driver.requests[kLane005] == 2u);
  driver.off = false;
  driver.resetHardware();
  executor.begin(4u, 300000u, &driver);
  executor.onFiveMillisecondSlot(20000u);
  assert(driver.requests[kLane005] == 3u);
  assert(!executor.hasActiveControl());
}

void testTrackingFaultGloballyClosesActive() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 300000u, &driver);
  auto active = makeSnapshot(1u, 1u, 1u, ControlSource::Host, 1u);
  stage(&executor, active, 100u);
  executor.onFiveMillisecondSlot(5000u);
  executor.latchTrackingFault(kLane005);
  executor.onFiveMillisecondSlot(10000u);
  assert(!executor.hasActiveControl());
  assert(driver.cancels[kLane005] == 1u);
  assert(driver.cancels[kLane007] == 1u);
  assert(driver.cancels[kLane364] == 1u);
  ControlHealthPayload health;
  assert(executor.healthSnapshot(10000u, &health));
  assert((health.flags & kHealthFlagTrackingFault) != 0u);
}

void testDedicatedBufferTerminalReconciliation() {
  const TxBufferReconciliation pending =
      reconcileAcceptedTxBuffers(0x07u, 0x01u, 0x02u, 0x04u);
  assert(pending.pending == 0x01u);
  assert(pending.transmitted == 0x02u);
  assert(pending.cancelled == 0x04u);
  assert(pending.failed == 0u);

  const TxBufferReconciliation callback_missed =
      reconcileAcceptedTxBuffers(0x01u, 0u, 0x01u, 0u);
  assert(callback_missed.transmitted == 0x01u);
  assert(callback_missed.pending == 0u);

  const TxBufferReconciliation non_retransmitted_error =
      reconcileAcceptedTxBuffers(0x02u, 0u, 0u, 0u);
  assert(non_retransmitted_error.failed == 0x02u);
  assert(non_retransmitted_error.transmitted == 0u);
  assert(non_retransmitted_error.cancelled == 0u);
  assert(non_retransmitted_error.pending == 0u);
}

void testBringupTraceMonotonicFailureRetention() {
  BringupTracePayload trace;
  trace.source_id = 0x1122334455667788ull;
  trace.runtime_contract_id = 0x8877665544332211ull;
  trace.build_id = 0xAABBCCDDu;
  assert(advanceBringupTrace(&trace, BringupStage::ForegroundLoopEntered));
  assert(!advanceBringupTrace(&trace, BringupStage::FirstTim4Tick));
  assert(trace.stage ==
         static_cast<uint16_t>(BringupStage::ForegroundLoopEntered));

  assert(advanceBringupTrace(&trace, BringupStage::HalStarted,
                             BringupFailure::ClockContract, 480000u));
  assert(trace.failure ==
         static_cast<uint16_t>(BringupFailure::ClockContract));
  assert(trace.failure_detail == 480000u);
  assert(!advanceBringupTrace(&trace, BringupStage::FdcanOperational));
  assert(trace.failure ==
         static_cast<uint16_t>(BringupFailure::ClockContract));
  assert(trace.source_id == 0x1122334455667788ull);
  assert(trace.runtime_contract_id == 0x8877665544332211ull);
  assert(trace.build_id == 0xAABBCCDDu);

  BringupTracePayload failed_then_advanced;
  assert(advanceBringupTrace(&failed_then_advanced, BringupStage::HalStarted,
                             BringupFailure::ClockContract, 499999u));
  assert(advanceBringupTrace(&failed_then_advanced,
                             BringupStage::FdcanOperational));
  assert(failed_then_advanced.stage ==
         static_cast<uint16_t>(BringupStage::FdcanOperational));
  assert(failed_then_advanced.failure ==
         static_cast<uint16_t>(BringupFailure::ClockContract));
  assert(failed_then_advanced.failure_detail == 499999u);
}

void testHostSessionCausalAckProofAndConsumedWatermark() {
  csm::board::control::HostCommandFreshness freshness;
  assert(!freshness.begin({}));
  csm::board::control::HostCommandFreshnessConfig limits;
  limits.proof_timeout_ms = 300u;
  assert(freshness.begin(limits));
  using csm::board::control::HostFreshnessResult;

  // T01: a bootstrap heartbeat is ACKable but cannot qualify liveness.
  assert(freshness.acceptHeartbeat(1u, 0u, 1000u, 2000u) ==
         HostFreshnessResult::BootstrapAccepted);
  assert(!freshness.qualified() && !freshness.proofAlive(2000u));

  // T02/T05: only a causal echo qualifies; forward commands then pass.
  assert(freshness.acceptHeartbeat(2u, 1u, 500000u, 2100u) ==
         HostFreshnessResult::Accepted);
  assert(freshness.qualified());
  assert(freshness.acceptCommand(3u, 2150u) ==
         HostFreshnessResult::Accepted);

  // T03/T07: a wrong echo consumes its ID but refreshes neither pending nor
  // proof time, so repeated wrong proof cannot keep authority alive.
  assert(freshness.acceptHeartbeat(4u, 1u, 600000u, 2200u) ==
         HostFreshnessResult::ProofMismatch);
  assert(freshness.proofAlive(2399u));
  assert(freshness.acceptHeartbeat(5u, 1u, 700000u, 2399u) ==
         HostFreshnessResult::ProofMismatch);
  freshness.update(2401u);
  assert(!freshness.proofAlive(2401u));
  assert(freshness.proofTimeoutTotal() == 1u);

  // T04/T06/T08: consumed IDs remain replay-protected across proof timeout and
  // policy rejection; a late heartbeat is bootstrap only, never auto-resume.
  assert(freshness.acceptCommand(5u, 2402u) == HostFreshnessResult::Replay);
  assert(freshness.acceptCommand(6u, 2402u) ==
         HostFreshnessResult::ProofRequired);
  assert(freshness.acceptCommand(6u, 2403u) == HostFreshnessResult::Replay);
  assert(freshness.acceptHeartbeat(7u, 2u, 800000u, 2404u) ==
         HostFreshnessResult::BootstrapAccepted);
  assert(!freshness.qualified());
  assert(freshness.acceptHeartbeat(8u, 7u, 900000u, 2410u) ==
         HostFreshnessResult::Accepted);

  // T09: uint32 command IDs advance correctly through wrap.
  freshness.resetTransportEpoch();
  assert(freshness.acceptHeartbeat(0xFFFFFFFEu, 0u, 0u, 10u) ==
         HostFreshnessResult::BootstrapAccepted);
  assert(freshness.acceptHeartbeat(0xFFFFFFFFu, 0xFFFFFFFEu, 0u, 20u) ==
         HostFreshnessResult::Accepted);
  assert(freshness.acceptCommand(1u, 21u) == HostFreshnessResult::Accepted);
  assert(freshness.acceptCommand(0xFFFFFFFFu, 22u) ==
         HostFreshnessResult::Replay);

  csm::board::control::HostControlSession session;
  session.begin(0u);
  assert(session.arm(0u, 500u, true) == csm::ControlReasonHostTimeout);
  session.heartbeat(10u);
  assert(session.arm(10u, 500u, true) == csm::ControlReasonOk);
  const uint32_t first = session.activationEpoch();
  session.update(511u);
  // T11: proof can remain alive while the independent lease closes.
  assert(!session.leaseAlive(511u) && session.timeoutCount() == 1u);
  session.heartbeat(512u);
  assert(session.arm(512u, 500u, true) == csm::ControlReasonOk);
  assert(session.activationEpoch() > first);

  // T10: an otherwise live lease cannot compensate for expired proof.
  freshness.resetTransportEpoch();
  assert(freshness.acceptHeartbeat(10u, 0u, 0u, 1000u) ==
         HostFreshnessResult::BootstrapAccepted);
  assert(freshness.acceptHeartbeat(11u, 10u, 0u, 1010u) ==
         HostFreshnessResult::Accepted);
  session.heartbeat(1010u);
  assert(session.arm(1010u, 1000u, true) == csm::ControlReasonOk);
  freshness.update(1311u);
  assert(!freshness.proofAlive(1311u));
  assert(session.leaseAlive(1311u));
}
}  // namespace

int main() {
  testHostSessionCausalAckProofAndConsumedWatermark();
  testReceiverQualifiedAdmissionAndOptionalStatistics();
  testCrsfStreamResynchronizationAndR16smFixture();
  testCrsfModernFramesAndChannelValidity();
  testTransmitterOffOnAndHostToRcTakeover();
  testSourceManagerOwnershipAndEpochs();
  testSharedMemoryIntegrityAndBoundedRing();
  testLongSafeCyclicAndRepeatedSafeStaging();
  testTerminalCancelRaceAndNoTransitionSkip();
  testHostAndRcLaneOwnership();
  testSameSourceRearmAndStale();
  testErrorPassiveBusOffResetAndHealthPurity();
  testTrackingFaultGloballyClosesActive();
  testDedicatedBufferTerminalReconciliation();
  testBringupTraceMonotonicFailureRetention();
  testStaticDueAndExplicitRequestAccounting();
  testPhysicalPendingAlwaysReconciles();
  return 0;
}
