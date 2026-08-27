#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "board/control_island/ControlIslandSharedMemory.h"
#include "board/control_island/ControlSourceManager.h"
#include "board/control_island/M4StaticCyclicExecutor.h"
#include "board/control/HostCommandFreshness.h"
#include "board/control/HostControlSession.h"
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

void testOnlyAcceptedCreatesPending() {
  const TxRequestResult failures[] = {
      TxRequestResult::TransportUnavailable,
      TxRequestResult::AlreadyPending,
      TxRequestResult::AddFailed,
      TxRequestResult::EnableFailedNoPending,
      TxRequestResult::EnableFailedAbortPending,
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

void testHostSessionAndSenderTimeBounds() {
  csm::board::control::HostCommandFreshness freshness;
  assert(!freshness.begin({}));
  csm::board::control::HostCommandFreshnessConfig limits;
  limits.heartbeat_max_extra_lag_ms = 100u;
  limits.command_max_age_ms = 40u;
  limits.clock_future_tolerance_ms = 20u;
  assert(freshness.begin(limits));
  using csm::board::control::HostFreshnessResult;
  assert(freshness.acceptHeartbeat(1u, 1000u, 2000u) ==
         HostFreshnessResult::AnchorEstablished);
  assert(freshness.acceptHeartbeat(2u, 1100u, 2100u) ==
         HostFreshnessResult::Accepted);
  assert(freshness.acceptCommand(3u, 1100u, 2150u) ==
         HostFreshnessResult::Stale);

  csm::board::control::HostControlSession session;
  session.begin(0u);
  assert(session.arm(0u, 500u, true) == csm::ControlReasonHostTimeout);
  session.heartbeat(10u);
  assert(session.arm(10u, 500u, true) == csm::ControlReasonOk);
  const uint32_t first = session.activationEpoch();
  session.update(511u);
  assert(!session.leaseAlive(511u) && session.timeoutCount() == 1u);
  session.heartbeat(512u);
  assert(session.arm(512u, 500u, true) == csm::ControlReasonOk);
  assert(session.activationEpoch() > first);
}
}  // namespace

int main() {
  testHostSessionAndSenderTimeBounds();
  testSourceManagerOwnershipAndEpochs();
  testSharedMemoryIntegrityAndBoundedRing();
  testLongSafeCyclicAndRepeatedSafeStaging();
  testTerminalCancelRaceAndNoTransitionSkip();
  testHostAndRcLaneOwnership();
  testSameSourceRearmAndStale();
  testErrorPassiveBusOffResetAndHealthPurity();
  testTrackingFaultGloballyClosesActive();
  testStaticDueAndExplicitRequestAccounting();
  testOnlyAcceptedCreatesPending();
  return 0;
}
