#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#include "board/feeder/FeederUartIngress.h"
#include "board/feeder/FeederWireProtocol.h"

using csm::board::feeder::FeederCanFrame;
using csm::board::feeder::FeederStatus;
using csm::board::feeder::FeederDmaErrorEvent;
using csm::board::feeder::FeederWireDecoder;
using csm::board::feeder::feederSourceHealthy;
using csm::board::feeder::reconcileFeederDmaCursor;
using csm::board::feeder::saturatingFeederCounterAdd;

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
    0x03, 0x02, 0x01, 0x07, 0x18, 0x44, 0x33, 0x22, 0x11,
    0x07, 0x01, 0x01, 0x02, 0x18, 0x06, 0x01, 0x18, 0xA0,
    0x86, 0x01, 0x06, 0x32, 0x52, 0x57, 0x46, 0x2A, 0x01,
    0x01, 0x04, 0x3C, 0x86, 0x01, 0x03, 0x23, 0x01, 0x01,
    0x02, 0x08, 0x01, 0x01, 0x0D, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x66, 0x9B, 0x2F, 0xDA, 0x00,
};

void writeU16(uint8_t* output, uint16_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
}

void writeU32(uint8_t* output, uint32_t value) {
  output[0] = static_cast<uint8_t>(value);
  output[1] = static_cast<uint8_t>(value >> 8U);
  output[2] = static_cast<uint8_t>(value >> 16U);
  output[3] = static_cast<uint8_t>(value >> 24U);
}

std::vector<uint8_t> cobsEncode(const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> encoded;
  encoded.reserve(raw.size() + raw.size() / 254U + 2U);
  size_t code_index = 0;
  uint8_t code = 1U;
  encoded.push_back(0U);

  for (const uint8_t byte : raw) {
    if (byte == 0U) {
      encoded[code_index] = code;
      code_index = encoded.size();
      encoded.push_back(0U);
      code = 1U;
      continue;
    }

    encoded.push_back(byte);
    ++code;
    if (code == 0xFFU) {
      encoded[code_index] = code;
      code_index = encoded.size();
      encoded.push_back(0U);
      code = 1U;
    }
  }

  encoded[code_index] = code;
  encoded.push_back(0U);
  return encoded;
}

std::vector<uint8_t> makeCanPacket(
    uint32_t boot_id, uint32_t packet_sequence,
    std::initializer_list<uint32_t> frame_sequences) {
  const size_t payload_size =
      frame_sequences.size() * csm::board::feeder::kFeederWireCanItemSize;
  std::vector<uint8_t> raw(
      csm::board::feeder::kFeederWireHeaderSize + payload_size +
          sizeof(uint32_t),
      0U);
  raw[0] = csm::board::feeder::kFeederWireVersion;
  raw[1] = csm::board::feeder::kFeederWireTypeCanBatch;
  raw[3] = csm::board::feeder::kFeederWireHeaderSize;
  writeU32(&raw[4], boot_id);
  writeU32(&raw[8], packet_sequence);
  writeU16(&raw[12], static_cast<uint16_t>(payload_size));
  raw[14] = static_cast<uint8_t>(frame_sequences.size());
  raw[15] = csm::board::feeder::kFeederWireCanItemSize;
  writeU32(&raw[16], 100000U);
  writeU32(&raw[20], csm::board::feeder::kFeederWireContractId);

  size_t index = 0;
  for (const uint32_t frame_sequence : frame_sequences) {
    uint8_t* item =
        &raw[csm::board::feeder::kFeederWireHeaderSize +
             index * csm::board::feeder::kFeederWireCanItemSize];
    writeU32(&item[0], frame_sequence);
    writeU32(&item[4], 99900U + static_cast<uint32_t>(index));
    writeU32(&item[8], 0x100U + static_cast<uint32_t>(index));
    item[12] = 8U;
    for (size_t data_index = 0; data_index < 8U; ++data_index) {
      item[16U + data_index] =
          static_cast<uint8_t>(frame_sequence + data_index);
    }
    ++index;
  }

  const uint32_t crc =
      FeederWireDecoder::crc32c(raw.data(), raw.size() - sizeof(uint32_t));
  writeU32(&raw[raw.size() - sizeof(uint32_t)], crc);
  return cobsEncode(raw);
}

std::vector<uint8_t> makeStatusPacket(
    uint32_t boot_id, uint32_t packet_sequence, uint32_t uptime_ms,
    uint32_t ring_overflow = 0U) {
  constexpr size_t payload_size =
      csm::board::feeder::kFeederWireStatusPayloadSize;
  std::vector<uint8_t> raw(
      csm::board::feeder::kFeederWireHeaderSize + payload_size +
          sizeof(uint32_t),
      0U);
  raw[0] = csm::board::feeder::kFeederWireVersion;
  raw[1] = csm::board::feeder::kFeederWireTypeStatus;
  raw[3] = csm::board::feeder::kFeederWireHeaderSize;
  writeU32(&raw[4], boot_id);
  writeU32(&raw[8], packet_sequence);
  writeU16(&raw[12], static_cast<uint16_t>(payload_size));
  writeU32(&raw[16], uptime_ms * 1000U);
  writeU32(&raw[20], csm::board::feeder::kFeederWireContractId);
  uint8_t* payload = &raw[csm::board::feeder::kFeederWireHeaderSize];
  writeU32(&payload[0], uptime_ms);
  writeU32(&payload[8], ring_overflow);
  writeU32(&payload[52], csm::board::feeder::kFeederWireContractId);
  writeU32(&payload[56],
           csm::board::feeder::kFeederFirmwareProfileProduct);
  writeU32(&payload[60], 0x10203040U);
  writeU32(&payload[64], 0x50607080U);

  const uint32_t crc =
      FeederWireDecoder::crc32c(raw.data(), raw.size() - sizeof(uint32_t));
  writeU32(&raw[raw.size() - sizeof(uint32_t)], crc);
  return cobsEncode(raw);
}

void pushPacket(FeederWireDecoder& decoder,
                const std::vector<uint8_t>& packet,
                uint64_t arrival_mono_us) {
  decoder.push(packet.data(), packet.size(), arrival_mono_us);
}

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

void packet_duplicate_and_backward_are_rejected_before_publication() {
  Capture capture;
  FeederWireDecoder decoder;
  decoder.begin(captureFrame, &capture);

  pushPacket(decoder, makeCanPacket(1U, 10U, {100U}), 1000000ULL);
  pushPacket(decoder, makeCanPacket(1U, 10U, {101U}), 1000100ULL);
  CHECK(capture.count == 1U);
  CHECK(decoder.stats().packets_ok == 1U);
  CHECK(decoder.stats().can_batches == 1U);
  CHECK(decoder.stats().packet_duplicates_or_reorders == 1U);
  CHECK(decoder.stats().frame_duplicates_or_reorders == 0U);
  CHECK(decoder.stats().last_packet_sequence == 10U);
  CHECK(decoder.stats().last_frame_sequence == 100U);

  pushPacket(decoder, makeCanPacket(1U, 12U, {101U}), 1000200ULL);
  CHECK(capture.count == 2U);
  CHECK(decoder.stats().packet_sequence_gaps == 1U);
  CHECK(decoder.stats().last_packet_sequence == 12U);

  pushPacket(decoder, makeCanPacket(1U, 11U, {102U}), 1000300ULL);
  CHECK(capture.count == 2U);
  CHECK(decoder.stats().packets_ok == 2U);
  CHECK(decoder.stats().packet_duplicates_or_reorders == 2U);
  CHECK(decoder.stats().last_packet_sequence == 12U);
  CHECK(decoder.stats().last_frame_sequence == 101U);
}

void frame_duplicate_and_backward_are_rejected_before_callback() {
  Capture capture;
  FeederWireDecoder decoder;
  decoder.begin(captureFrame, &capture);

  pushPacket(decoder, makeCanPacket(2U, 1U, {100U}), 2000000ULL);
  pushPacket(decoder, makeCanPacket(2U, 2U, {100U}), 2000100ULL);
  pushPacket(decoder, makeCanPacket(2U, 3U, {99U}), 2000200ULL);
  CHECK(capture.count == 1U);
  CHECK(decoder.stats().packets_ok == 3U);
  CHECK(decoder.stats().frames_ok == 1U);
  CHECK(decoder.stats().frame_duplicates_or_reorders == 2U);
  CHECK(decoder.stats().last_frame_sequence == 100U);

  pushPacket(decoder, makeCanPacket(2U, 4U, {103U}), 2000300ULL);
  CHECK(capture.count == 2U);
  CHECK(capture.frame.source_sequence == 103U);
  CHECK(decoder.stats().frames_ok == 2U);
  CHECK(decoder.stats().frame_sequence_gaps == 2U);
  CHECK(decoder.stats().last_frame_sequence == 103U);
}

void sequence_wrap_and_boot_change_start_clean_epochs() {
  Capture capture;
  FeederWireDecoder decoder;
  decoder.begin(captureFrame, &capture);
  constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();

  pushPacket(decoder, makeCanPacket(3U, kMax, {kMax}), 3000000ULL);
  pushPacket(decoder, makeCanPacket(3U, 0U, {0U}), 3000100ULL);
  pushPacket(decoder, makeCanPacket(4U, 42U, {77U}), 3000200ULL);

  CHECK(capture.count == 3U);
  CHECK(decoder.stats().packets_ok == 3U);
  CHECK(decoder.stats().frames_ok == 3U);
  CHECK(decoder.stats().packet_sequence_gaps == 0U);
  CHECK(decoder.stats().packet_duplicates_or_reorders == 0U);
  CHECK(decoder.stats().frame_sequence_gaps == 0U);
  CHECK(decoder.stats().frame_duplicates_or_reorders == 0U);
  CHECK(decoder.stats().boot_changes == 1U);
  CHECK(decoder.stats().current_boot_id == 4U);
  CHECK(decoder.stats().last_packet_sequence == 42U);
  CHECK(decoder.stats().last_frame_sequence == 77U);
}

void boot_change_invalidates_prior_status_epoch() {
  Capture capture;
  FeederWireDecoder decoder;
  decoder.begin(captureFrame, &capture);

  pushPacket(decoder, makeStatusPacket(10U, 1U, 123U, 7U), 4000000ULL);
  CHECK(decoder.statusValid());
  CHECK(decoder.status().uptime_ms == 123U);
  CHECK(decoder.status().ring_overflow == 7U);
  CHECK(decoder.lastStatusMonoUs() == 4000000ULL);

  pushPacket(decoder, makeCanPacket(10U, 2U, {1U}), 4000100ULL);
  CHECK(decoder.statusValid());
  CHECK(decoder.lastStatusMonoUs() == 4000000ULL);

  pushPacket(decoder, makeCanPacket(11U, 40U, {80U}), 4000200ULL);
  CHECK(!decoder.statusValid());
  CHECK(decoder.status().uptime_ms == 0U);
  CHECK(decoder.status().ring_overflow == 0U);
  CHECK(decoder.lastStatusMonoUs() == 0U);
  CHECK(decoder.stats().boot_changes == 1U);
  CHECK(decoder.stats().current_boot_id == 11U);

  pushPacket(decoder, makeStatusPacket(11U, 41U, 5U), 4000300ULL);
  CHECK(decoder.statusValid());
  CHECK(decoder.status().uptime_ms == 5U);
  CHECK(decoder.lastStatusMonoUs() == 4000300ULL);
}

void dma_cursor_reconciles_only_one_pending_wrap() {
  auto result =
      reconcileFeederDmaCursor(2U, 100U, 4096U, false, 8190U);
  CHECK(result.valid);
  CHECK(!result.reconciled_pending_wrap);
  CHECK(result.produced_total == 8292U);

  result = reconcileFeederDmaCursor(0U, 32U, 4096U, true, 4090U);
  CHECK(result.valid);
  CHECK(result.reconciled_pending_wrap);
  CHECK(result.produced_total == 4128U);

  result = reconcileFeederDmaCursor(0U, 0U, 4096U, true, 4096U);
  CHECK(result.valid);
  CHECK(result.reconciled_pending_wrap);
  CHECK(result.produced_total == 4096U);

  result = reconcileFeederDmaCursor(0U, 32U, 4096U, false, 4090U);
  CHECK(!result.valid);
  result = reconcileFeederDmaCursor(0U, 32U, 4096U, true, 9000U);
  CHECK(!result.valid);
  result = reconcileFeederDmaCursor(0U, 4097U, 4096U, true, 0U);
  CHECK(!result.valid);
}

void feeder_source_health_is_fail_closed_for_loss_and_mcp_faults() {
  const auto healthy_status = [] {
    FeederStatus value;
    value.runtime_contract_id = csm::board::feeder::kFeederWireContractId;
    value.firmware_profile_id =
        csm::board::feeder::kFeederFirmwareProfileProduct;
    value.firmware_source_id = 1U;
    value.firmware_build_id = 1U;
    return value;
  };
  FeederStatus status = healthy_status();
  CHECK(feederSourceHealthy(status));

  status.ring_overflow = 1U;
  CHECK(!feederSourceHealthy(status));
  status = healthy_status();
  status.mcp_overflow_events = 1U;
  CHECK(!feederSourceHealthy(status));
  status = healthy_status();
  status.mcp_error_irq_events = 1U;
  CHECK(!feederSourceHealthy(status));
  status = healthy_status();
  status.mcp_bus_off_events = 1U;
  CHECK(!feederSourceHealthy(status));
  status = healthy_status();
  status.eflg_or = 1U;
  CHECK(!feederSourceHealthy(status));
  status = healthy_status();
  status.firmware_build_id = 0U;
  CHECK(!feederSourceHealthy(status));
}

void dma_error_isr_handoff_is_atomic_and_main_owned() {
  FeederDmaErrorEvent event;
  event.publishFromIsr();
  event.publishFromIsr();
  event.publishFromIsr();
  CHECK(event.consume() == 3U);
  CHECK(event.consume() == 0U);
  event.publishFromIsr();
  event.reset();
  CHECK(event.consume() == 0U);
  CHECK(saturatingFeederCounterAdd(UINT32_MAX - 1U, 8U) == UINT32_MAX);
}

void corruption_does_not_publish_or_advance_sequence() {
  Capture capture;
  FeederWireDecoder decoder;
  decoder.begin(captureFrame, &capture);
  decoder.push(kGoldenCanPacket, sizeof(kGoldenCanPacket), 1000000ULL);

  uint8_t corrupted[sizeof(kGoldenCanPacket)] = {};
  std::memcpy(corrupted, kGoldenCanPacket, sizeof(corrupted));
  corrupted[30] ^= 0x01U;
  decoder.push(corrupted, sizeof(corrupted), 1000100ULL);
  CHECK(capture.count == 1U);
  CHECK(decoder.stats().crc_failures == 1U);
  CHECK(decoder.stats().packets_ok == 1U);
  CHECK(decoder.stats().last_packet_sequence == 7U);
  CHECK(decoder.stats().last_frame_sequence == 42U);
}

}  // namespace

int main() {
  known_crc_and_golden_packet_are_stable();
  packet_duplicate_and_backward_are_rejected_before_publication();
  frame_duplicate_and_backward_are_rejected_before_callback();
  sequence_wrap_and_boot_change_start_clean_epochs();
  boot_change_invalidates_prior_status_epoch();
  dma_cursor_reconciles_only_one_pending_wrap();
  feeder_source_health_is_fail_closed_for_loss_and_mcp_faults();
  dma_error_isr_handoff_is_atomic_and_main_owned();
  corruption_does_not_publish_or_advance_sequence();
  if (failures != 0) {
    std::fprintf(stderr, "feeder wire contract failures=%d\n", failures);
    return 1;
  }
  std::puts("feeder wire contract passed");
  return 0;
}
