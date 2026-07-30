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

class TestWifiWorkerMailbox final
    : private TestWifiMailboxStorage,
      public csm::board::uplink::WifiWorkerMailbox {
 public:
  TestWifiWorkerMailbox()
      : csm::board::uplink::WifiWorkerMailbox(storage) {
    configureReliableSession(1);
    activateReliableSession();
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
  PublishedFrameView frame{bytes, sizeof(bytes), 1, RecordType::StreamSession,
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

  uint32_t aborted_bytes = 0;
  CHECK(mailbox.tryApplyAbort(aborted_bytes));
  CHECK(aborted_bytes == sizeof(bytes));
  WifiWorkerMailbox::TxConsumeResult consumed;
  bool stale = false;
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(stale);
  CHECK(consumed.bytes == 0);
  CHECK(mailbox.queueSnapshot().queued_bytes == 0);
  CHECK(mailbox.queueSnapshot().queued_records == 0);

  mailbox.activateReliableSession();
  CHECK(mailbox.tryOffer(frame, 20) == WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(!stale);
  CHECK(consumed.frames == 1);
}

void wifi_mailbox_retains_first_boot_anchor_outside_journal() {
  using namespace csm::board::uplink;
  TestWifiWorkerMailbox mailbox;
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

  uint32_t aborted_bytes = 0;
  CHECK(mailbox.tryApplyAbort(aborted_bytes));
  CHECK(aborted_bytes == sizeof(original));
  anchor = {};
  CHECK(mailbox.tryReadSessionAnchor(anchor));
  CHECK(anchor.publish_seq == 7);
  CHECK(std::memcmp(anchor.bytes, original, sizeof(original)) == 0);

  mailbox.activateReliableSession();
  uint8_t later[19] = {};
  later[0] = 0xBB;
  PublishedFrameView second{later, sizeof(later), 99,
                            RecordType::StreamSession,
                            UplinkPriority::Critical,
                            UplinkDeliveryClass::LatencyBounded};
  CHECK(mailbox.tryOffer(second, 20) == WifiMailboxOfferResult::Accepted);
  anchor = {};
  CHECK(mailbox.tryReadSessionAnchor(anchor));
  CHECK(anchor.publish_seq == 7);
  CHECK(anchor.bytes[0] == 0xA5);
  CHECK(anchor.bytes[18] == 0x5A);
}

void wifi_mailbox_recovers_live_admission_after_durable_drain() {
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
  CHECK(mailbox.reliableIntegrityFault());
  const uint32_t first_close_request =
      mailbox.queuePressureDisconnectRequestSequence();
  for (uint16_t attempt = 0; attempt < 16; ++attempt) {
    const uint64_t sequence = last_accepted + 1;
    PublishedFrameView critical{bytes, sizeof(bytes), sequence,
                                RecordType::StreamSession,
                                UplinkPriority::Critical,
                                UplinkDeliveryClass::LatencyBounded};
    if (mailbox.tryOffer(critical, 11 + attempt) ==
        WifiMailboxOfferResult::Accepted) {
      last_accepted = sequence;
    }
  }
  CHECK(mailbox.queuePressureDisconnectRequestSequence() ==
        first_close_request);

  uint8_t staged[256] = {};
  while (mailbox.queueSnapshot().unsent_records != 0) {
    WifiMailboxTxLease lease;
    CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
    WifiWorkerMailbox::TxConsumeResult consumed;
    bool stale = false;
    CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
    CHECK(!stale);
  }

  uint8_t ack_payload[csm::kAppRxCommitAckPayloadLen] = {};
  csm::wr_u64_le(
      &ack_payload[csm::kAppRxCommitAckBootSessionOffset], 1);
  csm::wr_u64_le(
      &ack_payload[csm::kAppRxCommitAckPublishSeqOffset], last_accepted);
  uint8_t ack_frame[
      csm::encoded_typed_frame_len(csm::kAppRxCommitAckPayloadLen)] = {};
  size_t ack_length = 0;
  CHECK(csm::encode_typed_frame(
      ack_frame, sizeof(ack_frame), RecordType::AppRxCommitAck,
      ack_payload, sizeof(ack_payload), 0, 0, &ack_length));
  CHECK(mailbox.pushRx(ack_frame, static_cast<uint16_t>(ack_length)));
  CHECK(mailbox.queueSnapshot().queued_records == 0);
  CHECK(!mailbox.reliableIntegrityFault());

  PublishedFrameView resumed{bytes, sizeof(bytes), last_accepted + 1,
                             RecordType::BoardEvent,
                             UplinkPriority::Normal};
  CHECK(mailbox.tryOffer(resumed, 20) == WifiMailboxOfferResult::Accepted);
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
                              RecordType::StreamSession,
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
                   csm::kTransportDiagnosticFlagConnected;
  snapshot.connection_epoch = 7;
  snapshot.accepted_bytes_total = 12345;
  snapshot.queue_bytes = 512;
  snapshot.socket_bytes_total = 12000;
  snapshot.queue_high_water_records = 77;
  snapshot.send_request_bytes_total = 14000;
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
            &payload[csm::kTransportDiagnosticLastAcceptedPublishSeqOffset]) ==
        static_cast<uint32_t>(snapshot.last_accepted_publish_seq));
  CHECK(csm::rd_u32_le(
            &payload[csm::kTransportDiagnosticLastSentPublishSeqOffset]) ==
        static_cast<uint32_t>(snapshot.last_sent_publish_seq));
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
  wifi_mailbox_retains_first_boot_anchor_outside_journal();
  wifi_mailbox_recovers_live_admission_after_durable_drain();
  wifi_mailbox_wakes_only_on_actionable_transitions();
  runtime_diagnostic_layout_is_fixed_and_bounded();
  transport_diagnostic_is_single_bounded_wire_record();
  can_segment_batches_with_bounded_latency();
  wifi_queue_snapshot_supports_product_descriptor_capacity();
  if (failures != 0) return 1;
  std::puts("PASS: canonical publisher fanout contract");
  return 0;
}
