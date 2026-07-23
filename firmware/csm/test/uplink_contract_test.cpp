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
#include "board/uplink/WifiWorkerMailbox.h"
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

void late_joining_sink_must_receive_its_own_session_anchor() {
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
  const auto retried = publisher.service(3);
  CHECK(retried.session_record);
  CHECK((retried.sink_accept_mask & (1u << 1)) != 0);

  const uint8_t payload[] = {0x55};
  CHECK(publisher.enqueueRecord(RecordType::BoardEvent, payload, sizeof(payload),
                                UplinkPriority::Normal));
  CHECK(!publisher.service(4).session_record);
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
  FixedFrameByteQueue<4, 523> queue;
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

void byte_queue_spsc_preserves_order_without_shared_lock() {
  using namespace csm::board::uplink;
  constexpr uint32_t kFrames = 10000;
  constexpr uint16_t kFrameBytes = 16;
  FixedFrameByteQueue<32, 2048> queue;
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

void wifi_mailbox_critical_urgency_tracks_consumer_completion() {
  using namespace csm::board::uplink;
  WifiWorkerMailbox mailbox;
  uint8_t bytes[16] = {};
  PublishedFrameView frame{bytes, sizeof(bytes), 1, RecordType::StreamSession,
                           UplinkPriority::Critical};
  CHECK(mailbox.tryOffer(frame, 10) == WifiMailboxOfferResult::Accepted);
  CHECK(mailbox.queueSnapshot().urgent);
  uint8_t staged[32] = {};
  WifiMailboxTxLease lease;
  CHECK(mailbox.tryStageTx(staged, sizeof(staged), lease));
  WifiWorkerMailbox::TxConsumeResult consumed;
  bool stale = false;
  CHECK(mailbox.tryConsumeTx(lease, lease.length, consumed, stale));
  CHECK(!stale);
  CHECK(consumed.frames == 1);
  CHECK(consumed.critical_frames == 1);
  CHECK(!mailbox.queueSnapshot().urgent);
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
  late_joining_sink_must_receive_its_own_session_anchor();
  explicit_session_refresh_precedes_queued_handshake_records();
  one_sink_overflow_does_not_block_other_sink();
  disconnected_sink_preserves_admitted_record();
  fixed_queue_batches_without_losing_frame_boundaries();
  byte_queue_wraps_without_losing_frame_boundaries();
  byte_queue_spsc_preserves_order_without_shared_lock();
  wifi_mailbox_critical_urgency_tracks_consumer_completion();
  runtime_diagnostic_layout_is_fixed_and_bounded();
  can_segment_batches_with_bounded_latency();
  wifi_queue_snapshot_supports_product_descriptor_capacity();
  if (failures != 0) return 1;
  std::puts("PASS: canonical publisher fanout contract");
  return 0;
}
