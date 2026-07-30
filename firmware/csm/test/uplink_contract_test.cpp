#include <cstdio>
#include <cstring>
#include <atomic>
#include <array>
#include <thread>
#include <vector>

#include "board/uplink/CanonicalPublisher.h"
#include "board/uplink/CanRxSegmentBuilder.h"
#include "board/uplink/FixedFrameQueue.h"
#include "board/uplink/FixedFrameByteQueue.h"
#include "board/uplink/WifiTransportDiagnostic.h"
#include "board/uplink/WifiWorkerMailbox.h"
#include "protocol/HostCommands.h"
#include "protocol/TypedRecords.h"

using csm::RecordType;
using csm::board::uplink::CanonicalPublisher;
using csm::board::uplink::IFrameSink;
using csm::board::uplink::FixedFrameQueue;
using csm::board::uplink::PublishedFrameView;
using csm::board::uplink::SinkOfferResult;
using csm::board::uplink::UplinkPriority;

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
    PublishedFrameView anchor{
        &anchor_byte, kTestSessionAnchorLength, 0, RecordType::StreamSession,
        UplinkPriority::Critical};
    (void)tryOffer(anchor, 1);
  }
};

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      failures++;                                                              \
    }                                                                          \
  } while (0)

class FakeSink final : public IFrameSink {
 public:
  bool connected_value = true;
  bool overflow = false;
  uint8_t bytes[csm::encoded_typed_frame_len(csm::kMaxPayloadLen)] = {};
  uint16_t length = 0;
  uint64_t publish_seq = 0;
  uint32_t accept_total = 0;

  bool enabled() const override { return true; }
  bool connected() const override { return connected_value; }
  SinkOfferResult offer(const PublishedFrameView& frame) override {
    if (!connected_value) return SinkOfferResult::Disconnected;
    if (overflow) return SinkOfferResult::Overflow;
    std::memcpy(bytes, frame.bytes, frame.length);
    length = frame.length;
    publish_seq = frame.publish_seq;
    accept_total++;
    return SinkOfferResult::Accepted;
  }
};

void session_is_identical_before_fanout() {
  FakeSink usb;
  FakeSink wifi;
  CanonicalPublisher publisher;
  publisher.begin(0x1122334455667788ULL, &usb, &wifi);
  const auto result = publisher.service(123456789ULL);

  CHECK(result.record_published);
  CHECK(result.session_record);
  CHECK(result.sink_accept_count == 2);
  CHECK(result.required_sink_mask == ((1u << 0) | (1u << 1)));
  CHECK(result.missed_required_sink_mask == 0);
  CHECK(result.publish_seq == 0);
  CHECK(usb.length == wifi.length);
  CHECK(std::memcmp(usb.bytes, wifi.bytes, usb.length) == 0);
  CHECK(usb.bytes[3] == static_cast<uint8_t>(RecordType::StreamSession));
  CHECK(csm::rd_u16_le(&usb.bytes[5]) == 0);
  CHECK(csm::rd_u16_le(&usb.bytes[7]) == csm::kStreamSessionPayloadLen);
  CHECK(publisher.nextPublishSeq() == 1);
}

void missed_session_anchor_is_one_shot_until_a_new_epoch() {
  FakeSink usb;
  FakeSink wifi;
  wifi.connected_value = false;
  CanonicalPublisher publisher;
  publisher.begin(0xAABBCCDDEEFF0011ULL, &usb, &wifi);

  CHECK(publisher.service(1).session_record);
  CHECK(usb.accept_total == 1);
  CHECK(wifi.accept_total == 0);

  wifi.connected_value = true;
  wifi.overflow = true;
  publisher.requestSessionAnnouncement(
      csm::board::uplink::SessionAnnouncementReason::SinkEpochChanged,
      1u << 1);
  const auto missed = publisher.service(2);
  CHECK(missed.session_record);
  CHECK(missed.sink_accept_mask == (1u << 0));
  CHECK(missed.required_sink_mask == (1u << 1));
  CHECK(missed.missed_required_sink_mask == (1u << 1));

  wifi.overflow = false;
  const auto no_flood = publisher.service(3);
  CHECK(!no_flood.session_record);
  CHECK(usb.accept_total == 2);

  publisher.requestSessionAnnouncement(
      csm::board::uplink::SessionAnnouncementReason::SinkEpochChanged,
      1u << 1);
  const auto next_epoch = publisher.service(4);
  CHECK(next_epoch.session_record);
  CHECK((next_epoch.sink_accept_mask & (1u << 1)) != 0);
  CHECK(next_epoch.required_sink_mask == (1u << 1));
  CHECK(next_epoch.missed_required_sink_mask == 0);

  const uint8_t payload[] = {0x55};
  CHECK(publisher.enqueueRecord(RecordType::BoardEvent, payload, sizeof(payload),
                                UplinkPriority::Normal));
  CHECK(!publisher.service(5).session_record);
}

void explicit_session_refresh_precedes_queued_handshake_records() {
  FakeSink wifi;
  CanonicalPublisher publisher;
  publisher.begin(0x1020304050607080ULL, nullptr, &wifi);
  CHECK(publisher.service(1).session_record);

  const uint8_t capability[] = {0x01};
  const uint8_t ack[] = {0x02};
  CHECK(publisher.enqueueRecord(RecordType::Capability, capability,
                                sizeof(capability), UplinkPriority::Critical));
  CHECK(publisher.enqueueRecord(RecordType::ControlAck, ack, sizeof(ack),
                                UplinkPriority::Critical));
  publisher.requestSessionAnnouncement(
      csm::board::uplink::SessionAnnouncementReason::SinkEpochChanged,
      1u << 1);

  const auto session = publisher.service(2);
  CHECK(session.session_record);
  CHECK(wifi.bytes[3] == static_cast<uint8_t>(RecordType::StreamSession));
  CHECK(wifi.bytes[9 + csm::kStreamSessionReasonOffset] ==
        static_cast<uint8_t>(
            csm::board::uplink::SessionAnnouncementReason::SinkEpochChanged));

  const auto first_response = publisher.service(3);
  CHECK(!first_response.session_record);
  CHECK(wifi.bytes[3] == static_cast<uint8_t>(RecordType::Capability));

  const auto second_response = publisher.service(4);
  CHECK(!second_response.session_record);
  CHECK(wifi.bytes[3] == static_cast<uint8_t>(RecordType::ControlAck));
}

void one_sink_overflow_does_not_block_other_sink() {
  FakeSink usb;
  FakeSink wifi;
  CanonicalPublisher publisher;
  publisher.begin(0xA5A55A5AULL, &usb, &wifi);
  CHECK(publisher.service(1).record_published);
  wifi.overflow = true;
  const uint8_t payload[] = {0x10, 0x20, 0x30};
  CHECK(publisher.enqueueRecord(RecordType::BoardEvent, payload, sizeof(payload),
                                UplinkPriority::Normal));
  const auto result = publisher.service(2);

  CHECK(result.record_published);
  CHECK(result.sink_accept_count == 1);
  CHECK(result.publish_seq == 1);
  CHECK(usb.accept_total == 2);
  CHECK(wifi.accept_total == 1);
  CHECK(publisher.counters().sink_miss_total[1] == 1);
  CHECK(publisher.nextPublishSeq() == 2);
}

void disconnected_sink_preserves_admitted_record() {
  FakeSink usb;
  usb.connected_value = false;
  CanonicalPublisher publisher;
  publisher.begin(7, &usb);
  const uint8_t payload[] = {1};
  CHECK(publisher.enqueueRecord(RecordType::BoardEvent, payload, sizeof(payload),
                                UplinkPriority::Normal));
  const auto result = publisher.service(3);

  CHECK(!result.record_consumed);
  CHECK(publisher.hasQueuedRecords());
  CHECK(publisher.nextPublishSeq() == 0);
}

void fixed_queue_batches_without_losing_frame_boundaries() {
  FixedFrameQueue<4> queue;
  const uint8_t first[] = {1, 2, 3};
  const uint8_t second[] = {4, 5};
  PublishedFrameView first_view{first, sizeof(first), 10, RecordType::BoardEvent,
                                UplinkPriority::Normal};
  PublishedFrameView second_view{second, sizeof(second), 11, RecordType::BoardHealth,
                                 UplinkPriority::Critical};
  CHECK(queue.push(first_view));
  CHECK(queue.push(second_view));

  uint8_t batch[8] = {};
  CHECK(queue.copyFrontBytes(batch, sizeof(batch)) == 5);
  CHECK(std::memcmp(batch, "\x01\x02\x03\x04\x05", 5) == 0);

  const auto partial = queue.consumeMany(4);
  CHECK(partial.bytes == 4);
  CHECK(partial.frames == 1);
  CHECK(partial.last_publish_seq == 10);
  CHECK(queue.queuedBytes() == 1);

  const auto final = queue.consumeMany(1);
  CHECK(final.frames == 1);
  CHECK(final.last_publish_seq == 11);
  CHECK(queue.empty());
}

void byte_queue_wraps_without_losing_frame_boundaries() {
  using namespace csm::board::uplink;
  FixedFrameByteQueue<4, 523>::Storage storage;
  std::memset(&storage, 0xA5, sizeof(storage));
  FixedFrameByteQueue<4, 523> queue(storage);
  uint8_t first[400] = {};
  uint8_t second[100] = {};
  uint8_t third[200] = {};
  for (uint16_t index = 0; index < sizeof(first); ++index) first[index] = 1;
  for (uint16_t index = 0; index < sizeof(second); ++index) second[index] = 2;
  for (uint16_t index = 0; index < sizeof(third); ++index) third[index] = 3;
  PublishedFrameView first_view{first, sizeof(first), 1, RecordType::BoardEvent,
                                UplinkPriority::Normal};
  PublishedFrameView second_view{second, sizeof(second), 2, RecordType::BoardEvent,
                                 UplinkPriority::Normal};
  PublishedFrameView third_view{third, sizeof(third), 3, RecordType::BoardEvent,
                                UplinkPriority::Normal};
  CHECK(queue.push(first_view));
  CHECK(queue.push(second_view));
  auto consumed = queue.consumeMany(450);
  CHECK(consumed.frames == 1);
  CHECK(consumed.last_publish_seq == 1);
  CHECK(queue.push(third_view));
  uint8_t copied[250] = {};
  CHECK(queue.copyFrontBytes(copied, sizeof(copied)) == sizeof(copied));
  for (uint16_t index = 0; index < 50; ++index) CHECK(copied[index] == 2);
  for (uint16_t index = 50; index < sizeof(copied); ++index) CHECK(copied[index] == 3);
  consumed = queue.consumeMany(sizeof(copied));
  CHECK(consumed.frames == 2);
  CHECK(consumed.last_publish_seq == 3);
  CHECK(queue.empty());
  CHECK(queue.highWaterBytes() == 500);
}

void byte_queue_distinguishes_exact_byte_full_from_empty() {
  using namespace csm::board::uplink;
  FixedFrameByteQueue<4, 523>::Storage storage;
  FixedFrameByteQueue<4, 523> queue(storage);
  uint8_t payload[523] = {};
  for (uint16_t index = 0; index < sizeof(payload); ++index) {
    payload[index] = static_cast<uint8_t>(index);
  }
  PublishedFrameView frame{payload, sizeof(payload), 9, RecordType::BoardEvent,
                           UplinkPriority::Critical};
  CHECK(queue.push(frame));
  CHECK(queue.full());
  CHECK(queue.queuedBytes() == sizeof(payload));
  uint8_t staged[523] = {};
  CHECK(queue.copyFrontBytes(staged, sizeof(staged)) == sizeof(staged));
  CHECK(std::memcmp(staged, payload, sizeof(payload)) == 0);
  CHECK(queue.consumeMany(522).frames == 0);
  const auto final = queue.consumeMany(1);
  CHECK(final.frames == 1);
  CHECK(final.critical_frames == 1);
  CHECK(final.last_publish_seq == 9);
  CHECK(queue.empty());

  CHECK(queue.push(frame));
  CHECK(queue.clear() == sizeof(payload));
  CHECK(queue.empty());
  CHECK(queue.queuedBytes() == 0);
}

void byte_queue_uses_compact_descriptors_at_product_capacity() {
  using namespace csm::board::uplink;
  using ProductDescriptorQueue = FixedFrameByteQueue<512, 1024>;
  static_assert(ProductDescriptorQueue::kDescriptorSizeBytes == 16);
  static_assert(ProductDescriptorQueue::kDescriptorStorageBytes == 8192);

  ProductDescriptorQueue::Storage storage;
  ProductDescriptorQueue queue(storage);
  uint8_t byte = 0xA5;
  for (uint16_t index = 0; index < 512; ++index) {
    PublishedFrameView frame{&byte, 1, index, RecordType::CanTxRaw,
                             index == 511 ? UplinkPriority::Critical
                                          : UplinkPriority::Normal};
    CHECK(queue.push(frame));
  }
  PublishedFrameView overflow{&byte, 1, 512, RecordType::CanTxRaw,
                              UplinkPriority::Normal};
  CHECK(!queue.push(overflow));
  CHECK(queue.count() == 512);
  CHECK(queue.queuedBytes() == 512);
  CHECK(queue.highWaterRecords() == 512);

  uint8_t staged[512] = {};
  CHECK(queue.copyFrontBytes(staged, sizeof(staged)) == sizeof(staged));
  for (uint16_t index = 0; index < sizeof(staged); ++index) {
    CHECK(staged[index] == byte);
  }
  const auto partial = queue.consumeMany(511);
  CHECK(partial.bytes == 511);
  CHECK(partial.frames == 511);
  CHECK(partial.last_publish_seq == 510);
  CHECK(partial.critical_frames == 0);
  const auto final = queue.consumeMany(1);
  CHECK(final.frames == 1);
  CHECK(final.critical_frames == 1);
  CHECK(final.last_publish_seq == 511);
  CHECK(queue.empty());
}

void byte_queue_clear_releases_partial_frame_and_reuses_wrapped_ring() {
  using namespace csm::board::uplink;
  FixedFrameByteQueue<8, 523>::Storage storage;
  FixedFrameByteQueue<8, 523> queue(storage);
  uint8_t first[300] = {};
  uint8_t second[200] = {};
  std::memset(first, 0x11, sizeof(first));
  std::memset(second, 0x22, sizeof(second));
  PublishedFrameView first_view{first, sizeof(first), 1, RecordType::BoardEvent,
                                UplinkPriority::Normal};
  PublishedFrameView second_view{second, sizeof(second), 2, RecordType::BoardHealth,
                                 UplinkPriority::Critical};
  CHECK(queue.push(first_view));
  CHECK(queue.push(second_view));
  const auto partial = queue.consumeMany(450);
  CHECK(partial.frames == 1);
  CHECK(queue.queuedBytes() == 50);
  CHECK(queue.clear() == 50);
  CHECK(queue.empty());
  CHECK(queue.queuedBytes() == 0);

  uint8_t payload[37] = {};
  for (uint16_t cycle = 0; cycle < 1000; ++cycle) {
    std::memset(payload, static_cast<uint8_t>(cycle), sizeof(payload));
    PublishedFrameView frame{payload, sizeof(payload), cycle,
                             RecordType::BoardEvent, UplinkPriority::Normal};
    CHECK(queue.push(frame));
    uint8_t staged[37] = {};
    CHECK(queue.copyFrontBytes(staged, 13) == 13);
    CHECK(queue.consumeMany(13).frames == 0);
    CHECK(queue.copyFrontBytes(staged, sizeof(staged)) == 24);
    const auto consumed = queue.consumeMany(24);
    CHECK(consumed.frames == 1);
    CHECK(consumed.last_publish_seq == cycle);
    CHECK(queue.empty());
  }
}

void byte_queue_spsc_preserves_order_without_shared_lock() {
  using namespace csm::board::uplink;
  constexpr uint32_t kFrames = 10000;
  constexpr uint16_t kFrameBytes = 16;
  FixedFrameByteQueue<32, 2048>::Storage storage;
  FixedFrameByteQueue<32, 2048> queue(storage);
  std::atomic<bool> producer_done{false};
  std::atomic<bool> consumer_error{false};
  std::vector<uint8_t> received;
  received.reserve(kFrames * kFrameBytes);

  std::thread producer([&] {
    for (uint32_t sequence = 0; sequence < kFrames; ++sequence) {
      std::array<uint8_t, 16> bytes{};
      for (uint16_t index = 0; index < 16; ++index) {
        bytes[index] = static_cast<uint8_t>((sequence + index) & 0xFFu);
      }
      PublishedFrameView frame{bytes.data(), static_cast<uint16_t>(bytes.size()), sequence,
                               RecordType::BoardEvent, UplinkPriority::Normal};
      while (!queue.push(frame)) std::this_thread::yield();
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread consumer([&] {
    uint8_t bytes[37] = {};
    while (!producer_done.load(std::memory_order_acquire) || !queue.empty()) {
      const uint16_t copied = queue.copyFrontBytes(bytes, sizeof(bytes));
      if (copied == 0) {
        std::this_thread::yield();
        continue;
      }
      received.insert(received.end(), bytes, bytes + copied);
      const auto consumed = queue.consumeMany(copied);
      if (consumed.bytes != copied) consumer_error.store(true);
    }
  });
  producer.join();
  consumer.join();

  CHECK(received.size() == kFrames * kFrameBytes);
  CHECK(!consumer_error.load());
  bool content_ok = received.size() == kFrames * kFrameBytes;
  for (uint32_t sequence = 0; content_ok && sequence < kFrames; ++sequence) {
    for (uint16_t index = 0; index < kFrameBytes; ++index) {
      if (received[sequence * kFrameBytes + index] !=
          static_cast<uint8_t>((sequence + index) & 0xFFu)) {
        content_ok = false;
        break;
      }
    }
  }
  CHECK(content_ok);
  CHECK(queue.empty());
}

void wifi_mailbox_latency_class_tracks_consumer_completion() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  uint8_t bytes[16] = {};
  PublishedFrameView frame{bytes, sizeof(bytes), 1, RecordType::BoardEvent,
                           UplinkPriority::Critical,
                           UplinkDeliveryClass::LatencyBounded};
  CHECK(mailbox.tryOffer(frame, 10) == WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.queueSnapshot().latency_bounded);
  uint8_t staged[32] = {};
  WifiMailboxTxLease lease;
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  WifiWorkerMailbox::TxConsumeResult consumed;
  bool stale = false;
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(!stale);
  CHECK(consumed.frames == 1);
  CHECK(consumed.critical_frames == 1);
  CHECK(consumed.latency_frames == 1);
  CHECK(!mailbox.queueSnapshot().latency_bounded);
}

void wifi_mailbox_abort_invalidates_staged_generation() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  uint8_t bytes[16] = {};
  PublishedFrameView frame{bytes, sizeof(bytes), 7, RecordType::BoardEvent,
                           UplinkPriority::Normal};
  CHECK(mailbox.tryOffer(frame, 10) == WifiMailboxOfferResult::Accepted);
  uint8_t staged[32] = {};
  WifiMailboxTxLease lease;
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));

  WifiMailboxAbortResult aborted;
  CHECK(mailbox.tryApplyAbort(kTestSessionAnchorLength, aborted));
  CHECK(aborted.records == 1);
  CHECK(aborted.bytes == sizeof(bytes));
  CHECK(aborted.sequence_valid);
  CHECK(aborted.first_publish_seq == 7);
  CHECK(aborted.last_publish_seq == 7);
  WifiWorkerMailbox::TxConsumeResult consumed;
  bool stale = false;
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(stale);
  CHECK(consumed.bytes == 0);
  CHECK(mailbox.queueSnapshot().queued_bytes == 0);
  CHECK(mailbox.queueSnapshot().queued_records == 0);

  mailbox.activateLiveSession();
  uint8_t resumed_anchor_byte = 0;
  PublishedFrameView resumed_anchor{
      &resumed_anchor_byte, kTestSessionAnchorLength, 8,
      RecordType::StreamSession, UplinkPriority::Critical};
  CHECK(mailbox.tryOffer(resumed_anchor, 20) ==
        WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.tryOffer(frame, 20) == WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(!stale);
  CHECK(consumed.frames == 1);
}

void wifi_mailbox_refreshes_anchor_for_each_live_epoch() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  mailbox.activateLiveSession();
  uint8_t original[19] = {};
  original[0] = 0xA5;
  original[18] = 0x5A;
  PublishedFrameView first{original, sizeof(original), 7,
                           RecordType::StreamSession,
                           UplinkPriority::Critical,
                           UplinkDeliveryClass::LatencyBounded};
  CHECK(mailbox.tryOffer(first, 10) == WifiMailboxOfferResult::Accepted);

  WifiMailboxSessionAnchor anchor;
  CHECK(mailbox.tryReadSessionAnchor(anchor));
  CHECK(anchor.length == sizeof(original));
  CHECK(anchor.publish_seq == 7);
  CHECK(std::memcmp(anchor.bytes, original, sizeof(original)) == 0);

  WifiMailboxAbortResult aborted;
  CHECK(mailbox.tryApplyAbort(0, aborted));
  CHECK(aborted.bytes == sizeof(original));
  CHECK(aborted.records == 1);
  CHECK(aborted.first_publish_seq == 7);
  CHECK(aborted.last_publish_seq == 7);

  mailbox.deactivateLiveSession();
  mailbox.activateLiveSession();
  anchor = {};
  CHECK(!mailbox.tryReadSessionAnchor(anchor));
  uint8_t later[19] = {};
  later[0] = 0xBB;
  PublishedFrameView second{later, sizeof(later), 99,
                            RecordType::StreamSession,
                            UplinkPriority::Critical,
                            UplinkDeliveryClass::LatencyBounded};
  CHECK(mailbox.tryOffer(second, 20) == WifiMailboxOfferResult::Accepted);
  anchor = {};
  CHECK(mailbox.tryReadSessionAnchor(anchor));
  CHECK(anchor.publish_seq == 99);
  CHECK(anchor.bytes[0] == 0xBB);
}

void wifi_mailbox_abort_accounts_only_unsent_anchor_bytes() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  mailbox.activateLiveSession();
  uint8_t bytes[19] = {};
  PublishedFrameView anchor{bytes, sizeof(bytes), 7,
                            RecordType::StreamSession,
                            UplinkPriority::Critical,
                            UplinkDeliveryClass::LatencyBounded};
  CHECK(mailbox.tryOffer(anchor, 10) == WifiMailboxOfferResult::Accepted);
  WifiMailboxAbortResult aborted;
  CHECK(mailbox.tryApplyAbort(7, aborted));
  CHECK(aborted.bytes == sizeof(bytes) - 7);
  CHECK(aborted.records == 1);
  CHECK(aborted.first_publish_seq == 7);
  CHECK(aborted.last_publish_seq == 7);
  WifiMailboxSessionAnchor cleared;
  CHECK(!mailbox.tryReadSessionAnchor(cleared));
}

void wifi_mailbox_conservation_uses_actual_admission_send_abort_transitions() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox::TxStorage storage;
  WifiWorkerMailbox mailbox(storage);
  mailbox.configureSession(1);
  mailbox.activateLiveSession();

  uint8_t anchor_bytes[10] = {};
  uint8_t live_bytes[20] = {};
  constexpr uint64_t kAnchorSequence = 0x0000000200000001ULL;
  constexpr uint64_t kLiveSequence = 0x0000000300000002ULL;
  PublishedFrameView anchor{
      anchor_bytes, sizeof(anchor_bytes), kAnchorSequence,
      RecordType::StreamSession, UplinkPriority::Critical};
  PublishedFrameView live{
      live_bytes, sizeof(live_bytes), kLiveSequence,
      RecordType::BoardEvent, UplinkPriority::Normal};
  CHECK(mailbox.tryOffer(anchor, 10) == WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.tryOffer(live, 10) == WifiMailboxOfferResult::Accepted);

  WifiMailboxAdmissionSnapshot admission;
  CHECK(mailbox.tryReadAdmissionSnapshot(admission));
  CHECK(admission.accepted_bytes_total == 30);
  CHECK(admission.accepted_records_total == 2);
  CHECK(admission.sequence_valid);
  CHECK(admission.last_accepted_publish_seq == kLiveSequence);

  // The anchor is a completed socket frame; five live bytes then progress
  // through the actual FIFO lease/consume boundary.
  uint8_t staged[20] = {};
  WifiMailboxTxLease lease;
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  WifiWorkerMailbox::TxConsumeResult consumed;
  bool stale = false;
  CHECK(mailbox.tryConsumeTx(lease, 5, consumed, stale));
  CHECK(!stale);
  CHECK(consumed.bytes == 5);
  CHECK(consumed.frames == 0);
  constexpr uint32_t kSocketSentBytes = 15;
  constexpr uint32_t kSocketSentRecords = 1;
  CHECK(admission.accepted_bytes_total - kSocketSentBytes == 15);
  CHECK(admission.accepted_records_total - kSocketSentRecords == 1);

  WifiMailboxAbortResult aborted;
  CHECK(mailbox.tryApplyAbort(sizeof(anchor_bytes), aborted));
  CHECK(aborted.bytes == 15);
  CHECK(aborted.records == 1);
  CHECK(aborted.sequence_valid);
  CHECK(aborted.first_publish_seq == kLiveSequence);
  CHECK(aborted.last_publish_seq == kLiveSequence);
  CHECK(admission.accepted_bytes_total - kSocketSentBytes - aborted.bytes == 0);
  CHECK(admission.accepted_records_total - kSocketSentRecords -
            aborted.records ==
        0);
}

void wifi_mailbox_never_exposes_physical_tx_before_admission_commit() {
  using namespace csm::board::uplink;
  constexpr uint32_t kCycles = 500;
  WifiWorkerMailbox::TxStorage storage;
  WifiWorkerMailbox mailbox(storage);
  mailbox.configureSession(1);
  uint8_t anchor_bytes[10] = {};
  std::array<uint8_t, kCycles> anchor_results{};
  std::atomic<uint32_t> anchor_requested{0};
  std::atomic<uint32_t> anchor_completed{0};

  std::thread anchor_producer([&] {
    for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
      while (anchor_requested.load(std::memory_order_acquire) <= cycle) {
        std::this_thread::yield();
      }
      PublishedFrameView anchor{
          anchor_bytes, sizeof(anchor_bytes), 0x100000000ULL + cycle,
          RecordType::StreamSession, UplinkPriority::Critical};
      anchor_results[cycle] =
          static_cast<uint8_t>(mailbox.tryOffer(anchor, cycle + 1));
      anchor_completed.store(cycle + 1, std::memory_order_release);
    }
  });

  for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
    mailbox.activateLiveSession();
    WifiMailboxAdmissionSnapshot before;
    while (!mailbox.tryReadAdmissionSnapshot(before)) {
      std::this_thread::yield();
    }
    anchor_requested.store(cycle + 1, std::memory_order_release);
    WifiMailboxSessionAnchor exposed;
    while (!mailbox.tryReadSessionAnchor(exposed) &&
           anchor_completed.load(std::memory_order_acquire) <= cycle) {
      std::this_thread::yield();
    }
    while (anchor_completed.load(std::memory_order_acquire) <= cycle) {
      std::this_thread::yield();
    }
    CHECK(static_cast<WifiMailboxOfferResult>(anchor_results[cycle]) ==
          WifiMailboxOfferResult::Accepted);
    CHECK(mailbox.tryReadSessionAnchor(exposed));
    WifiMailboxAdmissionSnapshot after;
    while (!mailbox.tryReadAdmissionSnapshot(after)) {
      std::this_thread::yield();
    }
    CHECK(after.accepted_records_total == before.accepted_records_total + 1u);
    CHECK(after.accepted_bytes_total ==
          before.accepted_bytes_total + sizeof(anchor_bytes));
    CHECK(after.last_accepted_publish_seq == 0x100000000ULL + cycle);
    CHECK(exposed.publish_seq == after.last_accepted_publish_seq);
    mailbox.deactivateLiveSession();
    WifiMailboxAbortResult aborted;
    while (!mailbox.tryApplyAbort(0, aborted)) {
      std::this_thread::yield();
    }
    CHECK(aborted.bytes == sizeof(anchor_bytes));
    CHECK(aborted.records == 1);
  }
  anchor_producer.join();

  mailbox.activateLiveSession();
  PublishedFrameView anchor{
      anchor_bytes, sizeof(anchor_bytes), 0x200000000ULL,
      RecordType::StreamSession, UplinkPriority::Critical};
  CHECK(mailbox.tryOffer(anchor, 1) == WifiMailboxOfferResult::Accepted);
  uint8_t live_bytes[20] = {};
  std::array<uint8_t, kCycles> live_results{};
  std::atomic<uint32_t> live_requested{0};
  std::atomic<uint32_t> live_completed{0};
  std::thread live_producer([&] {
    for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
      while (live_requested.load(std::memory_order_acquire) <= cycle) {
        std::this_thread::yield();
      }
      PublishedFrameView live{
          live_bytes, sizeof(live_bytes), 0x300000000ULL + cycle,
          RecordType::BoardEvent, UplinkPriority::Normal};
      live_results[cycle] =
          static_cast<uint8_t>(mailbox.tryOffer(live, cycle + 1));
      live_completed.store(cycle + 1, std::memory_order_release);
    }
  });

  for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
    WifiMailboxAdmissionSnapshot before;
    while (!mailbox.tryReadAdmissionSnapshot(before)) {
      std::this_thread::yield();
    }
    live_requested.store(cycle + 1, std::memory_order_release);
    uint8_t staged[sizeof(live_bytes)] = {};
    WifiMailboxTxLease lease;
    while (!mailbox.tryStageTx(staged, sizeof(staged), lease) &&
           live_completed.load(std::memory_order_acquire) <= cycle) {
      std::this_thread::yield();
    }
    while (live_completed.load(std::memory_order_acquire) <= cycle) {
      std::this_thread::yield();
    }
    CHECK(static_cast<WifiMailboxOfferResult>(live_results[cycle]) ==
          WifiMailboxOfferResult::Accepted);
    if (lease.length == 0) {
      CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
    }
    WifiMailboxAdmissionSnapshot after;
    while (!mailbox.tryReadAdmissionSnapshot(after)) {
      std::this_thread::yield();
    }
    CHECK(after.accepted_records_total == before.accepted_records_total + 1u);
    CHECK(after.accepted_bytes_total ==
          before.accepted_bytes_total + sizeof(live_bytes));
    CHECK(after.last_accepted_publish_seq == 0x300000000ULL + cycle);
    CHECK(lease.length == sizeof(live_bytes));
    WifiWorkerMailbox::TxConsumeResult consumed;
    bool stale = false;
    CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
    CHECK(!stale);
    CHECK(consumed.frames == 1);
  }
  live_producer.join();
}

void wifi_mailbox_close_race_never_leaks_postcommit_frame() {
  using namespace csm::board::uplink;
  constexpr uint32_t kCycles = 2000;
  TestWifiWorkerMailbox mailbox;
  std::array<uint8_t, csm::encoded_typed_frame_len(csm::kMaxPayloadLen)>
      bytes{};
  std::array<uint8_t, kCycles> results{};
  std::atomic<uint32_t> requested{0};
  std::atomic<uint32_t> completed{0};

  std::thread producer([&] {
    for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
      while (requested.load(std::memory_order_acquire) <= cycle) {
        std::this_thread::yield();
      }
      PublishedFrameView frame{
          bytes.data(), static_cast<uint16_t>(bytes.size()), cycle,
          RecordType::BoardEvent, UplinkPriority::Normal};
      results[cycle] = static_cast<uint8_t>(mailbox.tryOffer(frame, cycle + 1));
      completed.store(cycle + 1, std::memory_order_release);
    }
  });

  for (uint32_t cycle = 0; cycle < kCycles; ++cycle) {
    mailbox.activateLiveSession();
    uint8_t anchor_byte = 0;
    PublishedFrameView anchor{
        &anchor_byte, kTestSessionAnchorLength, cycle,
        RecordType::StreamSession, UplinkPriority::Critical};
    CHECK(mailbox.tryOffer(anchor, cycle + 1) ==
          WifiMailboxOfferResult::Accepted);
    requested.store(cycle + 1, std::memory_order_release);
    if ((cycle & 1u) != 0) std::this_thread::yield();
    mailbox.deactivateLiveSession();
    WifiMailboxAbortResult aborted;
    while (!mailbox.tryApplyAbort(kTestSessionAnchorLength, aborted)) {
      std::this_thread::yield();
    }
    while (completed.load(std::memory_order_acquire) <= cycle) {
      std::this_thread::yield();
    }
    const auto result =
        static_cast<WifiMailboxOfferResult>(results[cycle]);
    CHECK(result == WifiMailboxOfferResult::Accepted ||
          result == WifiMailboxOfferResult::Busy);
    CHECK(aborted.bytes ==
          (result == WifiMailboxOfferResult::Accepted ? bytes.size() : 0u));
    CHECK(aborted.records ==
          (result == WifiMailboxOfferResult::Accepted ? 1u : 0u));
    CHECK(aborted.sequence_valid ==
          (result == WifiMailboxOfferResult::Accepted));
    CHECK(mailbox.queueSnapshot().queued_bytes == 0);
    CHECK(mailbox.queueSnapshot().queued_records == 0);
  }
  producer.join();
}

void wifi_mailbox_full_rolls_epoch_without_ack_or_replay() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  uint8_t bytes[64] = {};
  uint64_t last_accepted = 0;
  for (uint64_t sequence = 0; sequence < 2000; ++sequence) {
    PublishedFrameView frame{bytes, sizeof(bytes), sequence,
                             RecordType::BoardEvent,
                             UplinkPriority::Normal};
    const WifiMailboxOfferResult result = mailbox.tryOffer(frame, 10);
    if (result != WifiMailboxOfferResult::Accepted) break;
    last_accepted = sequence;
  }
  CHECK(!mailbox.liveSessionActive());
  const uint32_t first_close_request =
      mailbox.queuePressureDisconnectRequestSequence();
  PublishedFrameView rejected{bytes, sizeof(bytes), last_accepted + 1,
                              RecordType::BoardEvent,
                              UplinkPriority::Normal};
  CHECK(mailbox.tryOffer(rejected, 11) == WifiMailboxOfferResult::Busy);
  CHECK(mailbox.queuePressureDisconnectRequestSequence() ==
        first_close_request);

  WifiMailboxAbortResult discarded;
  CHECK(mailbox.tryApplyAbort(kTestSessionAnchorLength, discarded));
  CHECK(discarded.bytes != 0);
  CHECK(discarded.records != 0);
  CHECK(mailbox.queueSnapshot().queued_records == 0);

  mailbox.activateLiveSession();
  uint8_t anchor_byte = 0;
  PublishedFrameView anchor{
      &anchor_byte, kTestSessionAnchorLength, last_accepted + 1,
      RecordType::StreamSession, UplinkPriority::Critical};
  CHECK(mailbox.tryOffer(anchor, 20) == WifiMailboxOfferResult::Accepted);
  PublishedFrameView resumed{bytes, sizeof(bytes), last_accepted + 1,
                             RecordType::BoardEvent,
                             UplinkPriority::Normal};
  CHECK(mailbox.tryOffer(resumed, 20) == WifiMailboxOfferResult::Accepted);
}

void wifi_mailbox_requires_fresh_anchor_before_live_fifo() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  mailbox.activateLiveSession();
  uint8_t byte = 0;
  PublishedFrameView live{&byte, 1, 10, RecordType::BoardEvent,
                          UplinkPriority::Normal};
  CHECK(mailbox.tryOffer(live, 10) == WifiMailboxOfferResult::Busy);
  PublishedFrameView anchor{&byte, 1, 11, RecordType::StreamSession,
                            UplinkPriority::Critical};
  CHECK(mailbox.tryOffer(anchor, 11) == WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.tryOffer(live, 12) == WifiMailboxOfferResult::Accepted);
}

struct WifiWakeCapture {
  uint32_t calls = 0;
  uint32_t bits = 0;
};

void captureWifiWake(void* context, uint32_t bits) {
  auto* capture = static_cast<WifiWakeCapture*>(context);
  capture->calls++;
  capture->bits |= bits;
}

void wifi_mailbox_wakes_only_on_actionable_transitions() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
  WifiWakeCapture capture;
  mailbox.setNotifier({&capture, captureWifiWake});

  uint8_t bytes[16] = {};
  PublishedFrameView normal{bytes, sizeof(bytes), 1, RecordType::BoardEvent,
                            UplinkPriority::Normal};
  PublishedFrameView critical{bytes, sizeof(bytes), 2,
                              RecordType::ControlAck,
                              UplinkPriority::Critical,
                              UplinkDeliveryClass::LatencyBounded};
  CHECK(mailbox.tryOffer(normal, 10) == WifiMailboxOfferResult::Accepted);
  CHECK(capture.calls == 1);
  CHECK((capture.bits & WifiWakeTxData) != 0);
  CHECK(mailbox.tryOffer(normal, 11) == WifiMailboxOfferResult::Accepted);
  CHECK(capture.calls == 1);
  CHECK(mailbox.tryOffer(critical, 12) == WifiMailboxOfferResult::Accepted);
  CHECK(capture.calls == 2);
  const auto queued = mailbox.queueSnapshot();
  CHECK(queued.empty_to_nonempty_wake_total == 1);
  CHECK(queued.latency_wake_total == 1);

  mailbox.requestAbort();
  CHECK(capture.calls == 3);
  CHECK((capture.bits & WifiWakeControl) != 0);
}

void runtime_diagnostic_layout_is_fixed_and_bounded() {
  CHECK(static_cast<uint8_t>(RecordType::RuntimeDiagnostic) == 19);
  CHECK(csm::kRuntimeDiagnosticSchema == 2);
  CHECK(csm::kRuntimeDiagnosticPayloadLen == 128);
  CHECK(csm::kRuntimeDiagnosticBeforeRegistersOffset == 40);
  CHECK(csm::kRuntimeDiagnosticWifiCallPhaseOffset == 72);
  CHECK(csm::kRuntimeDiagnosticBootSessionOffset == 104);
  CHECK(csm::kRuntimeDiagnosticWifiConnectionEpochOffset + sizeof(uint32_t) ==
        csm::kRuntimeDiagnosticBootSessionOffset);
  CHECK(csm::kRuntimeDiagnosticWifiWorkerStackFreeOffset == 116);
  CHECK(csm::kRuntimeDiagnosticWifiWorkerStackMaxUsedOffset == 120);
  CHECK(csm::kRuntimeDiagnosticFirmwareBuildIdOffset + sizeof(uint32_t) ==
        csm::kRuntimeDiagnosticPayloadLen);
}

void transport_diagnostic_is_single_bounded_wire_record() {
  using csm::board::uplink::WifiTransportDiagnosticSnapshot;
  WifiTransportDiagnosticSnapshot snapshot;
  snapshot.mono_us = 0x0102030405060708ULL;
  snapshot.flags = csm::kTransportDiagnosticFlagEnabled |
                   csm::kTransportDiagnosticFlagConnected |
                   csm::kTransportDiagnosticFlagLossRangeValid;
  snapshot.connection_epoch = 7;
  snapshot.accepted_bytes_total = 12845;
  snapshot.accepted_records_total = 100;
  snapshot.rejected_records_total = 3;
  snapshot.queue_bytes = 512;
  snapshot.queue_records = 2;
  snapshot.socket_bytes_total = 12000;
  snapshot.socket_frames_total = 97;
  snapshot.queue_high_water_records = 77;
  snapshot.aborted_bytes_total = 333;
  snapshot.aborted_records_total = 1;
  snapshot.first_lost_publish_seq = 700;
  snapshot.last_lost_publish_seq = 701;
  snapshot.last_accepted_publish_seq = 899;
  snapshot.last_sent_publish_seq = 897;
  uint8_t payload[csm::kTransportDiagnosticPayloadLen] = {};
  CHECK(csm::board::uplink::build_wifi_transport_diagnostic_payload(
            snapshot, payload, sizeof(payload)) ==
        csm::kTransportDiagnosticPayloadLen);
  CHECK(static_cast<uint8_t>(RecordType::TransportDiagnostic) == 20);
  CHECK(payload[csm::kTransportDiagnosticSchemaOffset] ==
        csm::kTransportDiagnosticSchema);
  CHECK(payload[csm::kTransportDiagnosticFlagsOffset] == snapshot.flags);
  CHECK(csm::rd_u32_le(
            &payload[csm::kTransportDiagnosticAcceptedBytesOffset]) ==
        snapshot.accepted_bytes_total);
  CHECK(csm::rd_u32_le(&payload[csm::kTransportDiagnosticQueueBytesOffset]) ==
        snapshot.queue_bytes);
  CHECK(csm::rd_u32_le(
            &payload[csm::kTransportDiagnosticAcceptedRecordsOffset]) ==
        snapshot.accepted_records_total);
  CHECK(csm::rd_u32_le(
            &payload[csm::kTransportDiagnosticAbortedBytesOffset]) ==
        snapshot.aborted_bytes_total);
  CHECK(csm::rd_u32_le(
            &payload[csm::kTransportDiagnosticAbortedRecordsOffset]) ==
        snapshot.aborted_records_total);
  CHECK(csm::rd_u64_le(
            &payload[csm::kTransportDiagnosticFirstLostPublishSeqOffset]) ==
        snapshot.first_lost_publish_seq);
  CHECK(csm::rd_u64_le(
            &payload[csm::kTransportDiagnosticLastLostPublishSeqOffset]) ==
        snapshot.last_lost_publish_seq);
  CHECK(snapshot.accepted_bytes_total ==
        snapshot.socket_bytes_total + snapshot.queue_bytes +
            snapshot.aborted_bytes_total);
  CHECK(snapshot.accepted_records_total ==
        snapshot.socket_frames_total + snapshot.queue_records +
            snapshot.aborted_records_total);
  CHECK(csm::rd_u64_le(
            &payload[csm::kTransportDiagnosticLastAcceptedPublishSeqOffset]) ==
        snapshot.last_accepted_publish_seq);
  CHECK(csm::rd_u64_le(
            &payload[csm::kTransportDiagnosticLastSentPublishSeqOffset]) ==
        snapshot.last_sent_publish_seq);
  CHECK(csm::kTransportDiagnosticLastSentPublishSeqOffset + sizeof(uint64_t) ==
        csm::kTransportDiagnosticPayloadLen);
}

struct SegmentCapture {
  uint32_t emits = 0;
  uint8_t count = 0;
  uint64_t sequence = 0;
};

bool captureSegment(void* context,
                    const csm::board::uplink::CanRxSegmentItem*, uint8_t count,
                    uint64_t sequence) {
  auto* capture = static_cast<SegmentCapture*>(context);
  capture->emits++;
  capture->count = count;
  capture->sequence = sequence;
  return true;
}

void can_segment_batches_with_bounded_latency() {
  using namespace csm::board::uplink;
  constexpr uint32_t kProductFlushUs = 20000;
  SegmentCapture capture;
  CanRxSegmentBuilder builder;
  builder.begin(captureSegment, &capture, kProductFlushUs);
  CanRxSegmentItem item;

  CHECK(builder.push(item, 1000));
  CHECK(builder.push(item, 8700));
  CHECK(builder.pendingCount() == 2);
  CHECK(builder.flushIfDue(20999));
  CHECK(capture.emits == 0);
  CHECK(builder.flushIfDue(21000));
  CHECK(capture.emits == 1);
  CHECK(capture.count == 2);
  CHECK(capture.sequence == 0);

  CHECK(builder.push(item, UINT32_MAX - 10000u));
  CHECK(builder.flushIfDue(9999));
  CHECK(capture.emits == 2);
  CHECK(capture.count == 1);
  CHECK(capture.sequence == 1);
}

void wifi_queue_snapshot_supports_product_descriptor_capacity() {
  csm::board::uplink::WifiMailboxQueueSnapshot snapshot;
  snapshot.queued_records = 300;
  snapshot.high_water_records = 512;
  CHECK(snapshot.queued_records == 300);
  CHECK(snapshot.high_water_records == 512);
  static_assert(sizeof(snapshot.queued_records) >= sizeof(uint16_t));
}

}  // namespace

int main() {
  session_is_identical_before_fanout();
  missed_session_anchor_is_one_shot_until_a_new_epoch();
  explicit_session_refresh_precedes_queued_handshake_records();
  one_sink_overflow_does_not_block_other_sink();
  disconnected_sink_preserves_admitted_record();
  fixed_queue_batches_without_losing_frame_boundaries();
  byte_queue_wraps_without_losing_frame_boundaries();
  byte_queue_distinguishes_exact_byte_full_from_empty();
  byte_queue_uses_compact_descriptors_at_product_capacity();
  byte_queue_clear_releases_partial_frame_and_reuses_wrapped_ring();
  byte_queue_spsc_preserves_order_without_shared_lock();
  wifi_mailbox_latency_class_tracks_consumer_completion();
  wifi_mailbox_abort_invalidates_staged_generation();
  wifi_mailbox_refreshes_anchor_for_each_live_epoch();
  wifi_mailbox_abort_accounts_only_unsent_anchor_bytes();
  wifi_mailbox_conservation_uses_actual_admission_send_abort_transitions();
  wifi_mailbox_never_exposes_physical_tx_before_admission_commit();
  wifi_mailbox_close_race_never_leaks_postcommit_frame();
  wifi_mailbox_full_rolls_epoch_without_ack_or_replay();
  wifi_mailbox_requires_fresh_anchor_before_live_fifo();
  wifi_mailbox_wakes_only_on_actionable_transitions();
  runtime_diagnostic_layout_is_fixed_and_bounded();
  transport_diagnostic_is_single_bounded_wire_record();
  can_segment_batches_with_bounded_latency();
  wifi_queue_snapshot_supports_product_descriptor_capacity();
  if (failures != 0) return 1;
  std::puts("PASS: canonical publisher fanout contract");
  return 0;
}
