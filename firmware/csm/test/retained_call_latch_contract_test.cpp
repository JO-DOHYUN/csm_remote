#include <array>
#include <cstdint>
#include <iostream>

#include "board/diagnostics/RetainedCallLatch.h"

namespace {

using csm::board::diagnostics::RetainedCallLatch;
using csm::board::diagnostics::RetainedStorageAdapter;

int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition \
                << '\n';                                                    \
      ++failures;                                                            \
    }                                                                        \
  } while (false)

using Storage =
    std::array<uint8_t, RetainedCallLatch::kRequiredStorageBytes>;

struct Probe {
  uint32_t prepare = 0;
  uint32_t commit = 0;
  uint32_t barrier = 0;
};

bool prepare(void* context, void*, std::size_t bytes) {
  auto& probe = *static_cast<Probe*>(context);
  ++probe.prepare;
  CHECK(bytes == RetainedCallLatch::kRequiredStorageBytes);
  return true;
}

void commit(void* context, const void*, std::size_t bytes) {
  auto& probe = *static_cast<Probe*>(context);
  ++probe.commit;
  CHECK(bytes == sizeof(uint32_t) ||
        bytes == RetainedCallLatch::kChecksumOffset);
}

void barrier(void* context) {
  ++static_cast<Probe*>(context)->barrier;
}

RetainedStorageAdapter adapter(Probe& probe) {
  return {&probe, prepare, commit, barrier};
}

void testCompletedCallAndNewSession() {
  Storage storage{};
  Probe probe{};
  {
    RetainedCallLatch latch(storage.data(), storage.size(), adapter(probe));
    CHECK(latch.beginSession(0x1122334455667788ULL, 7, 10));
    CHECK(!latch.previousSnapshot().valid);
    CHECK(latch.enter(1, 2, 9, 20));
    CHECK(latch.leave(-3012, 4567, 25));
  }
  {
    RetainedCallLatch latch(storage.data(), storage.size());
    CHECK(latch.beginSession(0x1122334455667788ULL, 8, 1));
    const auto previous = latch.previousSnapshot();
    CHECK(previous.valid);
    CHECK(!previous.in_progress);
    CHECK(previous.completed);
    CHECK(!previous.contract_changed);
    CHECK(previous.owner == 1);
    CHECK(previous.operation == 2);
    CHECK(previous.boot_sequence == 7);
    CHECK(previous.call_sequence == 9);
    CHECK(previous.started_uptime_ms == 20);
    CHECK(previous.completed_uptime_ms == 25);
    CHECK(previous.duration_us == 4567);
    CHECK(previous.result == -3012);
  }
  CHECK(probe.prepare == 1);
  CHECK(probe.commit > 0);
  CHECK(probe.barrier > 0);
}

void testCrashInsideCallAndContractChange() {
  Storage storage{};
  {
    RetainedCallLatch latch(storage.data(), storage.size());
    CHECK(latch.beginSession(100, 1, 0));
    CHECK(latch.enter(4, 2, 1, 33));
  }
  {
    RetainedCallLatch latch(storage.data(), storage.size());
    CHECK(latch.beginSession(200, 2, 0));
    const auto previous = latch.previousSnapshot();
    CHECK(previous.valid);
    CHECK(previous.in_progress);
    CHECK(!previous.completed);
    CHECK(previous.contract_changed);
    CHECK(previous.owner == 4);
    CHECK(previous.operation == 2);
    CHECK(previous.boot_sequence == 1);
    CHECK(previous.call_sequence == 1);
    CHECK(previous.started_uptime_ms == 33);
  }
}

void testTornNewestFallsBack() {
  Storage storage{};
  {
    RetainedCallLatch latch(storage.data(), storage.size());
    CHECK(latch.beginSession(10, 1, 0));
    CHECK(latch.enter(1, 7, 2, 50));
    CHECK(latch.leave(0, 10, 51));
  }

  // The latest write alternates slots; corrupt both candidates one at a time
  // and confirm a valid older checksum-last boundary remains recoverable.
  storage[RetainedCallLatch::slotOffset(0) +
          RetainedCallLatch::kChecksumOffset] ^= 0xA5;
  RetainedCallLatch latch(storage.data(), storage.size());
  CHECK(latch.beginSession(10, 2, 0));
  const auto previous = latch.previousSnapshot();
  CHECK(previous.valid);
  CHECK(previous.in_progress);
  CHECK(previous.call_sequence == 2);
}

}  // namespace

int main() {
  static_assert(RetainedCallLatch::kRequiredStorageBytes == 128,
                "unexpected retained call-latch footprint");
  testCompletedCallAndNewSession();
  testCrashInsideCallAndContractChange();
  testTornNewestFallsBack();
  if (failures != 0) return 1;
  std::cout << "Retained call latch contract PASS\n";
  return 0;
}
