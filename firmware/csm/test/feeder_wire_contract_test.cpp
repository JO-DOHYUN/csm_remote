#include <cstdio>
#include <cstring>

#include "board/feeder/FeederWireProtocol.h"

using csm::board::feeder::FeederCanFrame;
using csm::board::feeder::FeederWireDecoder;

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
  uint32_t count = 0;
  FeederCanFrame frame = {};
};

bool captureFrame(void* context, const FeederCanFrame& frame) {
  auto* capture = static_cast<Capture*>(context);
  ++capture->count;
  capture->frame = frame;
  return true;
}

constexpr uint8_t kGoldenCanPacket[] = {
    0x03, 0x01, 0x01, 0x07, 0x18, 0x44, 0x33, 0x22, 0x11,
    0x07, 0x01, 0x01, 0x02, 0x18, 0x06, 0x01, 0x18, 0xA0,
    0x86, 0x01, 0x06, 0x31, 0x52, 0x57, 0x46, 0x2A, 0x01,
    0x01, 0x04, 0x3C, 0x86, 0x01, 0x03, 0x23, 0x01, 0x01,
    0x02, 0x08, 0x01, 0x01, 0x0D, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0xC7, 0x57, 0x86, 0x1D, 0x00,
};

void known_crc_and_golden_packet_are_stable() {
  static constexpr uint8_t text[] = "123456789";
  CHECK(FeederWireDecoder::crc32c(text, sizeof(text) - 1U) == 0xE3069283U);

  Capture capture;
  FeederWireDecoder decoder;
  decoder.begin(captureFrame, &capture);
  decoder.push(kGoldenCanPacket, sizeof(kGoldenCanPacket), 1000000ULL);

  CHECK(capture.count == 1U);
  CHECK(capture.frame.source_sequence == 42U);
  CHECK(capture.frame.source_timestamp_us == 99900U);
  CHECK(capture.frame.estimated_mono_us == 999900ULL);
  CHECK(capture.frame.can_id_flags == 0x123U);
  CHECK(capture.frame.dlc_flags == 8U);
  static constexpr uint8_t expected_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(std::memcmp(capture.frame.data, expected_data, sizeof(expected_data)) ==
        0);
  CHECK(decoder.stats().packets_ok == 1U);
  CHECK(decoder.stats().frames_ok == 1U);
  CHECK(decoder.stats().current_boot_id == 0x11223344U);
  CHECK(decoder.stats().last_packet_sequence == 7U);
}

void corruption_and_duplicate_sequences_are_explicit() {
  Capture capture;
  FeederWireDecoder decoder;
  decoder.begin(captureFrame, &capture);
  decoder.push(kGoldenCanPacket, sizeof(kGoldenCanPacket), 1000000ULL);
  decoder.push(kGoldenCanPacket, sizeof(kGoldenCanPacket), 1000100ULL);
  CHECK(capture.count == 2U);
  CHECK(decoder.stats().packet_duplicates_or_reorders == 1U);
  CHECK(decoder.stats().frame_duplicates_or_reorders == 1U);

  uint8_t corrupted[sizeof(kGoldenCanPacket)] = {};
  std::memcpy(corrupted, kGoldenCanPacket, sizeof(corrupted));
  corrupted[30] ^= 0x01U;
  decoder.push(corrupted, sizeof(corrupted), 1000200ULL);
  CHECK(capture.count == 2U);
  CHECK(decoder.stats().crc_failures == 1U);
}

}  // namespace

int main() {
  known_crc_and_golden_packet_are_stable();
  corruption_and_duplicate_sequences_are_explicit();
  if (failures != 0) {
    std::fprintf(stderr, "feeder wire contract failures=%d\n", failures);
    return 1;
  }
  std::puts("feeder wire contract passed");
  return 0;
}

