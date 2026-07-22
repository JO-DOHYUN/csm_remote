#include <cstdio>
#include <cstring>

#include "board/uplink/CanonicalPublisher.h"
#include "board/uplink/FixedFrameQueue.h"
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

}  // namespace

int main() {
  session_is_identical_before_fanout();
  one_sink_overflow_does_not_block_other_sink();
  disconnected_sink_preserves_admitted_record();
  fixed_queue_batches_without_losing_frame_boundaries();
  runtime_diagnostic_layout_is_fixed_and_bounded();
  if (failures != 0) return 1;
  std::puts("PASS: canonical publisher fanout contract");
  return 0;
}
