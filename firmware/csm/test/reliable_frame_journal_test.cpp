#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "board/uplink/ReliableFrameJournal.h"
#include "board/uplink/LinkReliabilityDiagnostic.h"
#include "protocol/TypedRecords.h"

namespace {

using csm::RecordType;
using csm::board::uplink::PublishedFrameView;
using csm::board::uplink::ReliableFrameJournal;
using csm::board::uplink::UplinkDeliveryClass;
using csm::board::uplink::UplinkPriority;

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

using Journal = ReliableFrameJournal<8, 523>;

PublishedFrameView view(const uint8_t* bytes, uint16_t length, uint64_t seq,
                        UplinkPriority priority = UplinkPriority::Normal,
                        UplinkDeliveryClass delivery =
                            UplinkDeliveryClass::Batchable) {
  PublishedFrameView frame;
  frame.bytes = bytes;
  frame.length = length;
  frame.publish_seq = seq;
  frame.type = RecordType::BoardEvent;
  frame.priority = priority;
  frame.delivery = delivery;
  return frame;
}

void positive_send_does_not_reclaim() {
  Journal::Storage storage{};
  Journal journal(storage);
  const uint8_t bytes[] = {1, 2, 3, 4};
  CHECK(journal.push(view(bytes, sizeof(bytes), 10), 100));

  uint8_t staged[8] = {};
  Journal::TxLease lease;
  CHECK(journal.stage(staged, sizeof(staged), lease));
  CHECK(lease.length == sizeof(bytes));
  Journal::SendResult sent;
  bool stale = false;
  CHECK(journal.advanceSent(lease, lease.length, sent, stale));
  CHECK(!stale);
  CHECK(sent.frames == 1);
  CHECK(sent.last_publish_seq == 10);

  const auto before_ack = journal.snapshot();
  CHECK(before_ack.retained_records == 1);
  CHECK(before_ack.retained_bytes == sizeof(bytes));
  CHECK(before_ack.unsent_records == 0);
  CHECK(before_ack.unsent_bytes == 0);

  const auto ack = journal.acknowledge(10);
  CHECK(ack.status == Journal::AckStatus::Accepted);
  CHECK(ack.reclaimed_frames == 1);
  CHECK(ack.reclaimed_bytes == sizeof(bytes));
  CHECK(journal.snapshot().retained_records == 0);
}

void reconnect_rewinds_only_unacked_data() {
  Journal::Storage storage{};
  Journal journal(storage);
  const uint8_t first[] = {0x11, 0x12};
  const uint8_t second[] = {0x21, 0x22, 0x23};
  CHECK(journal.push(view(first, sizeof(first), 20), 1));
  CHECK(journal.push(view(second, sizeof(second), 21), 2));

  uint8_t staged[8] = {};
  Journal::TxLease lease;
  CHECK(journal.stage(staged, sizeof(staged), lease));
  Journal::SendResult sent;
  bool stale = false;
  CHECK(journal.advanceSent(lease, lease.length, sent, stale));
  CHECK(sent.frames == 2);
  CHECK(journal.acknowledge(20).accepted());

  journal.rewindToLastAck();
  std::memset(staged, 0, sizeof(staged));
  CHECK(journal.stage(staged, sizeof(staged), lease));
  CHECK(lease.length == sizeof(second));
  CHECK(std::memcmp(staged, second, sizeof(second)) == 0);
}

void recovery_ack_advances_a_rewound_send_cursor() {
  Journal::Storage storage{};
  Journal journal(storage);
  const uint8_t first[] = {0x31, 0x32};
  const uint8_t second[] = {0x41, 0x42, 0x43};
  CHECK(journal.push(view(first, sizeof(first), 30), 1));
  CHECK(journal.push(view(second, sizeof(second), 31), 2));

  uint8_t staged[8] = {};
  Journal::TxLease lease;
  CHECK(journal.stage(staged, sizeof(staged), lease));
  Journal::SendResult sent;
  bool stale = false;
  CHECK(journal.advanceSent(lease, lease.length, sent, stale));
  CHECK(sent.frames == 2);

  journal.rewindToLastAck();
  CHECK(journal.acknowledge(30).accepted());
  const auto after_recovery_ack = journal.snapshot();
  CHECK(after_recovery_ack.retained_records == 1);
  CHECK(after_recovery_ack.unsent_records == 1);
  CHECK(after_recovery_ack.retained_bytes == sizeof(second));
  CHECK(after_recovery_ack.unsent_bytes == sizeof(second));

  std::memset(staged, 0, sizeof(staged));
  CHECK(journal.stage(staged, sizeof(staged), lease));
  CHECK(lease.length == sizeof(second));
  CHECK(std::memcmp(staged, second, sizeof(second)) == 0);
}

void ack_must_be_sent_and_exactly_contiguous() {
  Journal::Storage storage{};
  Journal journal(storage);
  const uint8_t byte = 0x5A;
  CHECK(journal.push(view(&byte, 1, 100), 1));
  CHECK(journal.push(view(&byte, 1, 101), 2));
  CHECK(journal.acknowledge(100).status == Journal::AckStatus::NoSentFrame);

  uint8_t staged[1] = {};
  Journal::TxLease lease;
  CHECK(journal.stage(staged, sizeof(staged), lease));
  Journal::SendResult sent;
  bool stale = false;
  CHECK(journal.advanceSent(lease, 1, sent, stale));
  CHECK(journal.acknowledge(101).status == Journal::AckStatus::AheadOfSent);
  CHECK(journal.acknowledge(99).status == Journal::AckStatus::NotContiguous);
  CHECK(journal.acknowledge(100).status == Journal::AckStatus::Accepted);
  CHECK(journal.acknowledge(100).status == Journal::AckStatus::Duplicate);
}

void batching_and_ring_wrap_preserve_bytes() {
  Journal::Storage storage{};
  Journal journal(storage);
  std::array<uint8_t, 300> first{};
  std::array<uint8_t, 200> second{};
  std::array<uint8_t, 250> third{};
  first.fill(0xA1);
  second.fill(0xB2);
  third.fill(0xC3);
  CHECK(journal.push(view(first.data(), first.size(), 1), 10));
  CHECK(journal.push(view(second.data(), second.size(), 2), 11));

  uint8_t staged[523] = {};
  Journal::TxLease lease;
  CHECK(journal.stage(staged, 450, lease));
  Journal::SendResult sent;
  bool stale = false;
  CHECK(journal.advanceSent(lease, 450, sent, stale));
  CHECK(sent.frames == 1);
  CHECK(journal.acknowledge(1).accepted());
  CHECK(journal.push(view(third.data(), third.size(), 3), 12));

  std::memset(staged, 0, sizeof(staged));
  CHECK(journal.stage(staged, sizeof(staged), lease));
  CHECK(lease.length == 300);
  for (uint16_t i = 0; i < 50; ++i) CHECK(staged[i] == 0xB2);
  for (uint16_t i = 50; i < 300; ++i) CHECK(staged[i] == 0xC3);
}

void front_deadline_follows_actual_send_cursor() {
  Journal::Storage storage{};
  Journal journal(storage);
  const uint8_t first[] = {1};
  const uint8_t second[] = {2};
  CHECK(journal.push(view(first, 1, 7, UplinkPriority::Normal,
                          UplinkDeliveryClass::Batchable),
                     100));
  CHECK(journal.push(view(second, 1, 8, UplinkPriority::Critical,
                          UplinkDeliveryClass::LatencyBounded),
                     250));
  CHECK(journal.snapshot().front_admitted_ms == 100);
  CHECK(!journal.snapshot().front_latency_bounded);

  uint8_t staged[1] = {};
  Journal::TxLease lease;
  CHECK(journal.stage(staged, 1, lease));
  Journal::SendResult sent;
  bool stale = false;
  CHECK(journal.advanceSent(lease, 1, sent, stale));
  CHECK(journal.snapshot().front_admitted_ms == 250);
  CHECK(journal.snapshot().front_latency_bounded);
}

void producer_worker_stress_keeps_order_until_ack() {
  constexpr uint32_t kFrames = 20000;
  using StressJournal = ReliableFrameJournal<64, 4096>;
  StressJournal::Storage storage{};
  StressJournal journal(storage);
  std::atomic<bool> producer_done{false};
  std::atomic<bool> failed{false};
  std::vector<uint32_t> observed;
  observed.reserve(kFrames);

  std::thread producer([&] {
    for (uint32_t seq = 0; seq < kFrames; ++seq) {
      uint8_t bytes[4] = {
          static_cast<uint8_t>(seq),
          static_cast<uint8_t>(seq >> 8),
          static_cast<uint8_t>(seq >> 16),
          static_cast<uint8_t>(seq >> 24),
      };
      while (!journal.push(view(bytes, sizeof(bytes), seq), seq)) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread worker([&] {
    // Keep the verification read size frame-aligned. The journal itself
    // supports arbitrary partial sends; that contract is covered separately.
    uint8_t bytes[36] = {};
    uint64_t last_complete = 0;
    bool have_complete = false;
    while (!producer_done.load(std::memory_order_acquire) ||
           journal.snapshot().retained_records != 0) {
      StressJournal::TxLease lease;
      if (!journal.stage(bytes, sizeof(bytes), lease)) {
        std::this_thread::yield();
        continue;
      }
      for (uint16_t offset = 0; offset + 4 <= lease.length; offset += 4) {
        const uint32_t value =
            static_cast<uint32_t>(bytes[offset]) |
            (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
            (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
            (static_cast<uint32_t>(bytes[offset + 3]) << 24);
        observed.push_back(value);
      }
      StressJournal::SendResult sent;
      bool stale = false;
      if (!journal.advanceSent(lease, lease.length, sent, stale) || stale) {
        failed.store(true);
        break;
      }
      if (sent.frames != 0) {
        last_complete = sent.last_publish_seq;
        have_complete = true;
      }
      if (have_complete && !journal.acknowledge(last_complete).accepted()) {
        failed.store(true);
        break;
      }
    }
  });

  producer.join();
  worker.join();
  CHECK(!failed.load());
  CHECK(observed.size() == kFrames);
  bool ordered = observed.size() == kFrames;
  for (uint32_t seq = 0; ordered && seq < kFrames; ++seq) {
    ordered = observed[seq] == seq;
  }
  CHECK(ordered);
}

void release_diagnostic_preserves_64_bit_conservation() {
  csm::board::uplink::LinkReliabilityDiagnosticSnapshot snapshot;
  snapshot.mono_us = 0x0102030405060708ULL;
  snapshot.boot_session_id = 0x1122334455667788ULL;
  snapshot.last_accepted_publish_seq = 0x100000002ULL;
  snapshot.highest_sent_publish_seq = 0x100000001ULL;
  snapshot.last_acked_publish_seq = 0xFFFFFFFFULL;
  snapshot.first_not_admitted_publish_seq = 0x200000003ULL;
  snapshot.offered_bytes_total = 0x300000004ULL;
  snapshot.admitted_bytes_total = 0x300000000ULL;
  snapshot.socket_sent_bytes_total = 0x2FFFFFFF0ULL;
  snapshot.reclaimed_bytes_total = 0x2FFFF0000ULL;
  snapshot.retained_bytes = 58890;
  snapshot.unsent_bytes = 1024;
  snapshot.high_water_bytes = 60000;
  snapshot.ack_accepted_total = 77;
  snapshot.journal_full_total = 1;
  uint8_t payload[csm::kLinkReliabilityDiagnosticPayloadLen] = {};
  CHECK(csm::board::uplink::build_link_reliability_diagnostic_payload(
            snapshot, payload, sizeof(payload)) == sizeof(payload));
  CHECK(payload[csm::kLinkReliabilityDiagnosticSchemaOffset] ==
        csm::kLinkReliabilityDiagnosticSchema);
  CHECK(csm::rd_u64_le(
            &payload[csm::kLinkReliabilityDiagnosticBootSessionOffset]) ==
        snapshot.boot_session_id);
  CHECK(csm::rd_u64_le(
            &payload[csm::kLinkReliabilityDiagnosticAdmittedBytesOffset]) ==
        snapshot.admitted_bytes_total);
  CHECK(csm::rd_u32_le(
            &payload[csm::kLinkReliabilityDiagnosticRetainedBytesOffset]) ==
        snapshot.retained_bytes);
  CHECK(csm::rd_u32_le(
            &payload[csm::kLinkReliabilityDiagnosticJournalFullOffset]) == 1);
}

}  // namespace

int main() {
  static_assert(ReliableFrameJournal<1024, 65520>::kDescriptorSizeBytes == 24);
  static_assert(ReliableFrameJournal<1024, 65520>::kDescriptorStorageBytes ==
                24576);
  static_assert(ReliableFrameJournal<1024, 65520>::kStorageBytes == 90096);
  positive_send_does_not_reclaim();
  reconnect_rewinds_only_unacked_data();
  recovery_ack_advances_a_rewound_send_cursor();
  ack_must_be_sent_and_exactly_contiguous();
  batching_and_ring_wrap_preserve_bytes();
  front_deadline_follows_actual_send_cursor();
  producer_worker_stress_keeps_order_until_ack();
  release_diagnostic_preserves_64_bit_conservation();
  if (failures != 0) return 1;
  std::puts("PASS: reliable frame journal contract");
  return 0;
}
