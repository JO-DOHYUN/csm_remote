#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "board/control_island/ControlIslandSharedMemory.h"
#include "board/control_island/ControlSourceManager.h"
#include "board/control_island/M4StaticCyclicExecutor.h"
#include "protocol/TypedRecords.h"

using namespace csm::board::control_island;

namespace {

static_assert(kControlIslandSchemaId == csm::kControlIslandHealthSchemaId);
static_assert(kHno1WireContractId ==
              csm::kControlIslandHealthWireContractId);
static_assert(kControlMemoryLayoutId ==
              csm::kControlIslandHealthMemoryLayoutId);

struct FakeDriver final : M4LaneDriver {
  bool ready_value = true;
  bool inhibit = false;
  bool passive = false;
  bool off = false;
  bool pending_value[kLaneCount] = {};
  uint32_t requests[kLaneCount] = {};
  uint32_t cancels[kLaneCount] = {};
  uint8_t last_data[kLaneCount][8] = {};

  bool ready() const override { return ready_value; }
  bool hardInhibitActive() const override { return inhibit; }
  uint8_t hardInputBits() const override {
    return inhibit ? kHardInputEstop : 0u;
  }
  bool errorPassive() const override { return passive; }
  bool busOff() const override { return off; }
  bool request(uint8_t lane, const uint8_t data[8]) override {
    if (lane >= kLaneCount || pending_value[lane]) return false;
    pending_value[lane] = true;
    ++requests[lane];
    memcpy(last_data[lane], data, 8u);
    return true;
  }
  bool cancel(uint8_t lane) override {
    if (lane >= kLaneCount || !pending_value[lane]) return false;
    ++cancels[lane];
    return true;
  }
  bool pending(uint8_t lane) const override {
    return lane < kLaneCount && pending_value[lane];
  }
  FdcanRawSnapshot rawSnapshot() const override { return {}; }

  void terminal(M4StaticCyclicExecutor* executor, uint8_t lane,
                bool transmitted, bool cancelled = false) {
    assert(lane < kLaneCount && pending_value[lane]);
    pending_value[lane] = false;
    executor->onTerminal(lane, transmitted, cancelled);
  }
};

FinalControlSnapshotPayload makeSnapshot(uint32_t publish_sequence,
                                         uint32_t authority_epoch,
                                         ControlSource source,
                                         uint32_t generation) {
  FinalControlSnapshotPayload snapshot;
  snapshot.m7_boot_id = 11u;
  snapshot.publish_sequence = publish_sequence;
  snapshot.authority_epoch = authority_epoch;
  snapshot.active_source = static_cast<uint32_t>(source);
  snapshot.source_image_generation = generation;
  snapshot.source_lease_sequence = 7u;
  snapshot.permit_mask = kAllLanePermitMask;
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    snapshot.lanes[lane].valid = 1u;
    snapshot.lanes[lane].value_generation = generation;
    for (uint8_t byte = 0; byte < 8u; ++byte) {
      snapshot.lanes[lane].data[byte] =
          static_cast<uint8_t>(lane * 16u + byte);
    }
  }
  return snapshot;
}

void closeAllPending(FakeDriver* driver, M4StaticCyclicExecutor* executor) {
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    if (driver->pending_value[lane]) driver->terminal(executor, lane, true);
  }
}

void testSourceManagerCoherentLatestState() {
  ControlSourceManager manager;
  manager.begin(9u);
  uint8_t a[8] = {1};
  uint8_t b[8] = {2};
  uint8_t c[8] = {3};
  assert(!manager.acceptHostState(1u, 0x03u, a, b, c, 1u));
  assert(manager.acceptHostState(1u, kAllLanePermitMask, a, b, c, 1u));
  assert(!manager.acceptHostState(1u, kAllLanePermitMask, a, b, c, 2u));
  manager.select(ControlSource::Host);
  const uint32_t epoch = manager.authorityEpoch();
  const FinalControlSnapshotPayload snapshot = manager.snapshot(0xFFu, 5u);
  assert(snapshot.active_source == static_cast<uint32_t>(ControlSource::Host));
  assert(snapshot.permit_mask == kAllLanePermitMask);
  assert(snapshot.lanes[kLane005].data[0] == 1u);
  assert(snapshot.lanes[kLane007].data[0] == 2u);
  assert(snapshot.lanes[kLane364].data[0] == 3u);
  manager.select(ControlSource::Remote);
  assert(manager.activeSource() == ControlSource::None);
  assert(manager.authorityEpoch() == epoch + 1u);
}

void testSharedMemoryIntegrityAndBoundedRing() {
  initializeControlIpcForM7(17u);
  const uint32_t m4_boot_id = initializeControlIpcForM4();
  assert(m4_boot_id != 0u);
  FinalControlSnapshotPayload snapshot =
      makeSnapshot(0u, 1u, ControlSource::Host, 1u);
  snapshot.m7_boot_id = 17u;
  assert(publishFinalControlSnapshot(snapshot));
  ControlReadResult read = readFinalControlSnapshot(0u);
  assert(read.accepted && read.new_snapshot && read.sequence != 0u);
  assert(read.payload.lanes[kLane364].data[0] == 32u);
  assert(readFinalControlSnapshot(read.sequence).accepted);

  ControlHealthPayload health;
  health.m4_boot_id = m4_boot_id;
  health.flags = kHealthFlagReady | kHealthFlagClockContractOk;
  assert(publishControlHealth(health));
  const HealthReadResult health_read = readControlHealth(0u);
  assert(health_read.accepted && health_read.new_snapshot);
  assert(health_read.payload.m4_boot_id == m4_boot_id);

  RawCanEntry raw;
  raw.can_id_flags = 0x123u;
  raw.dlc_flags = 8u;
  for (size_t index = 0; index < kRawCanRingCapacity; ++index) {
    assert(pushRawCanFromM4(raw));
  }
  assert(!pushRawCanFromM4(raw));
  assert(rawCanRingFill() == kRawCanRingCapacity);
  for (size_t index = 0; index < kRawCanRingCapacity; ++index) {
    RawCanEntry popped;
    assert(popRawCanForM7(&popped));
    assert(popped.can_id_flags == 0x123u);
  }
  assert(rawCanRingFill() == 0u);
}

void testStaticSlotsAndNoReplay() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(3u, 100000u, &driver);
  FinalControlSnapshotPayload snapshot =
      makeSnapshot(2u, 1u, ControlSource::Host, 1u);
  assert(executor.acceptSnapshot(snapshot, 100u));

  for (uint8_t slot = 0; slot < 4u; ++slot) {
    executor.onFiveMillisecondSlot(5000u + slot * 5000u);
    closeAllPending(&driver, &executor);
  }
  assert(driver.requests[kLane005] == 4u);
  assert(driver.requests[kLane007] == 1u);
  assert(driver.requests[kLane364] == 1u);

  executor.onFiveMillisecondSlot(200000u);
  assert(driver.requests[kLane005] == 4u);
  assert(executor.healthForPublish(200000u).m7_stale_count == 1u);
  executor.onFiveMillisecondSlot(250000u);
  assert(driver.requests[kLane005] == 4u);
  assert(executor.healthForPublish(250000u).m7_stale_count == 1u);
}

void testQuiescentAuthoritySwitch() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(4u, 100000u, &driver);
  FinalControlSnapshotPayload host =
      makeSnapshot(2u, 1u, ControlSource::Host, 1u);
  assert(executor.acceptSnapshot(host, 100u));
  executor.onFiveMillisecondSlot(5000u);
  assert(driver.requests[0] == 1u && driver.requests[1] == 1u &&
         driver.requests[2] == 1u);

  FinalControlSnapshotPayload remote =
      makeSnapshot(4u, 2u, ControlSource::Remote, 2u);
  assert(executor.acceptSnapshot(remote, 5100u));
  assert(driver.cancels[0] == 1u && driver.cancels[1] == 1u &&
         driver.cancels[2] == 1u);
  executor.onFiveMillisecondSlot(10000u);
  assert(driver.requests[0] == 1u && driver.requests[1] == 1u &&
         driver.requests[2] == 1u);
  for (uint8_t lane = 0; lane < kLaneCount; ++lane) {
    driver.terminal(&executor, lane, false, true);
  }
  executor.onFiveMillisecondSlot(15000u);
  assert(executor.health().active_source_seen ==
         static_cast<uint32_t>(ControlSource::Remote));
  assert(driver.requests[0] == 2u);
}

void testExactNSuccessBudget() {
  FakeDriver driver;
  M4StaticCyclicExecutor executor;
  executor.begin(5u, 1000000u, &driver);
  FinalControlSnapshotPayload snapshot =
      makeSnapshot(2u, 1u, ControlSource::Host, 3u);
  snapshot.transaction.active = 1u;
  snapshot.transaction.transaction_id = 77u;
  snapshot.transaction.payload_generation = 4u;
  snapshot.transaction.lane_index = kLane364;
  snapshot.transaction.requested_success_count = 4u;
  memset(snapshot.transaction.data, 0xA5, 8u);
  assert(executor.acceptSnapshot(snapshot, 100u));

  uint32_t now = 5000u;
  for (uint8_t cycle = 0; cycle < 5u; ++cycle) {
    for (uint8_t slot = 0; slot < 4u; ++slot) {
      executor.onFiveMillisecondSlot(now);
      closeAllPending(&driver, &executor);
      now += 5000u;
    }
  }
  assert(driver.requests[kLane364] == 4u);
  assert(executor.health().transaction_id == 77u);
  assert(executor.health().transaction_completed == 4u);
  assert(executor.health().transaction_state ==
         static_cast<uint8_t>(TransactionState::Complete));
}

}  // namespace

int main() {
  testSourceManagerCoherentLatestState();
  testSharedMemoryIntegrityAndBoundedRing();
  testStaticSlotsAndNoReplay();
  testQuiescentAuthoritySwitch();
  testExactNSuccessBudget();
  return 0;
}
