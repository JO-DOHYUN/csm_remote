#include <cstdio>
#include <cstring>
#include <vector>

#include "board/uplink/ProductDownlinkRouter.h"
#include "protocol/HostCommands.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

struct Capture {
  uint32_t ack_total = 0;
  uint64_t boot = 0;
  uint64_t seq = 0;
  std::vector<uint8_t> passthrough;
};

bool capture_ack(void* context, uint64_t boot, uint64_t seq) {
  auto* capture = static_cast<Capture*>(context);
  ++capture->ack_total;
  capture->boot = boot;
  capture->seq = seq;
  return true;
}

bool capture_passthrough(void* context, const uint8_t* bytes,
                         uint16_t length) {
  auto* capture = static_cast<Capture*>(context);
  capture->passthrough.insert(capture->passthrough.end(), bytes,
                              bytes + length);
  return true;
}

std::vector<uint8_t> encode(csm::RecordType type, const uint8_t* payload,
                            uint16_t length) {
  std::vector<uint8_t> frame(csm::encoded_typed_frame_len(length));
  size_t written = 0;
  CHECK(csm::encode_typed_frame(frame.data(), frame.size(), type, payload,
                                length, 17, 0, &written));
  frame.resize(written);
  return frame;
}

void ack_is_consumed_and_never_reaches_motion_lane() {
  Capture capture;
  csm::board::uplink::ProductDownlinkRouter router;
  router.begin(capture_ack, capture_passthrough, &capture);
  uint8_t payload[csm::kAppRxCommitAckPayloadLen] = {};
  csm::wr_u64_le(
      &payload[csm::kAppRxCommitAckBootSessionOffset],
      0x1122334455667788ULL);
  csm::wr_u64_le(
      &payload[csm::kAppRxCommitAckPublishSeqOffset],
      0x0102030405060708ULL);
  const auto frame = encode(csm::RecordType::AppRxCommitAck, payload,
                            sizeof(payload));

  for (const uint8_t byte : frame) CHECK(router.feed(&byte, 1));
  CHECK(capture.ack_total == 1);
  CHECK(capture.boot == 0x1122334455667788ULL);
  CHECK(capture.seq == 0x0102030405060708ULL);
  CHECK(capture.passthrough.empty());
  CHECK(router.counters().app_ack_total == 1);
}

void service_command_remains_byte_identical() {
  Capture capture;
  csm::board::uplink::ProductDownlinkRouter router;
  router.begin(capture_ack, capture_passthrough, &capture);
  const uint8_t payload[] = {1, 2, 3, 4};
  const auto frame =
      encode(csm::RecordType::HostQueryCapability, payload, sizeof(payload));
  CHECK(router.feed(frame.data(), frame.size()));
  CHECK(capture.ack_total == 0);
  CHECK(capture.passthrough == frame);
  CHECK(router.counters().passthrough_total == 1);
}

void corrupt_ack_cannot_reclaim() {
  Capture capture;
  csm::board::uplink::ProductDownlinkRouter router;
  router.begin(capture_ack, capture_passthrough, &capture);
  uint8_t payload[csm::kAppRxCommitAckPayloadLen] = {};
  const auto valid =
      encode(csm::RecordType::AppRxCommitAck, payload, sizeof(payload));
  auto corrupt = valid;
  corrupt[9] ^= 0x80;
  CHECK(router.feed(corrupt.data(), corrupt.size()));
  CHECK(capture.ack_total == 0);
  CHECK(router.counters().crc_failure_total == 1);
  CHECK(router.feed(valid.data(), valid.size()));
  CHECK(capture.ack_total == 1);
}

}  // namespace

int main() {
  ack_is_consumed_and_never_reaches_motion_lane();
  service_command_remains_byte_identical();
  corrupt_ack_cannot_reclaim();
  if (failures != 0) return 1;
  std::puts("PASS: product downlink ACK isolation contract");
  return 0;
}
