#pragma once

#include <stddef.h>
#include <stdint.h>

namespace csm::board::feeder {

static constexpr uint8_t kFeederWireVersion = 1;
static constexpr uint8_t kFeederWireTypeCanBatch = 1;
static constexpr uint8_t kFeederWireTypeStatus = 2;
static constexpr uint8_t kFeederWireHeaderSize = 24;
static constexpr uint8_t kFeederWireCanItemSize = 24;
static constexpr uint8_t kFeederWireMaxCanItems = 15;
static constexpr uint16_t kFeederWireStatusPayloadSize = 52;
static constexpr uint32_t kFeederWireContractId = 0x46575231U;
static constexpr size_t kFeederWireMaxRawPacket =
    kFeederWireHeaderSize +
    kFeederWireCanItemSize * kFeederWireMaxCanItems + sizeof(uint32_t);
static constexpr size_t kFeederWireMaxEncodedPacket =
    kFeederWireMaxRawPacket + (kFeederWireMaxRawPacket / 254U) + 1U;

struct FeederCanFrame {
  uint32_t source_sequence = 0;
  uint32_t source_timestamp_us = 0;
  uint64_t estimated_mono_us = 0;
  uint32_t can_id_flags = 0;
  uint8_t dlc_flags = 0;
  uint8_t data[8] = {};
};

struct FeederStatus {
  uint32_t uptime_ms = 0;
  uint32_t rx_total = 0;
  uint32_t ring_overflow = 0;
  uint32_t mcp_overflow_events = 0;
  uint32_t mcp_error_irq_events = 0;
  uint32_t mcp_bus_off_events = 0;
  uint32_t eflg_or = 0;
  uint32_t tec_max = 0;
  uint32_t rec_max = 0;
  uint32_t max_irq_service_us = 0;
  uint32_t tx_packet_total = 0;
  uint32_t tx_bytes_total = 0;
  uint32_t ring_high_water = 0;
};

struct FeederWireStats {
  uint32_t packets_ok = 0;
  uint32_t can_batches = 0;
  uint32_t status_packets = 0;
  uint32_t frames_ok = 0;
  uint32_t empty_delimiters = 0;
  uint32_t encoded_overflow = 0;
  uint32_t cobs_failures = 0;
  uint32_t crc_failures = 0;
  uint32_t contract_failures = 0;
  uint32_t length_failures = 0;
  uint32_t packet_sequence_gaps = 0;
  uint32_t packet_duplicates_or_reorders = 0;
  uint32_t frame_sequence_gaps = 0;
  uint32_t frame_duplicates_or_reorders = 0;
  uint32_t callback_rejects = 0;
  uint32_t boot_changes = 0;
  uint32_t current_boot_id = 0;
  uint32_t last_packet_sequence = 0;
  uint32_t last_frame_sequence = 0;
};

using FeederCanFrameFn = bool (*)(void* context,
                                  const FeederCanFrame& frame);

class FeederWireDecoder {
 public:
  void begin(FeederCanFrameFn frame_fn, void* context);
  void setCallback(FeederCanFrameFn frame_fn, void* context);
  void push(const uint8_t* bytes, size_t length, uint64_t arrival_mono_us);
  void resetFraming();

  const FeederWireStats& stats() const { return stats_; }
  const FeederStatus& status() const { return status_; }
  bool sessionValid() const { return session_valid_; }
  bool statusValid() const { return status_valid_; }
  uint64_t lastPacketMonoUs() const { return last_packet_mono_us_; }
  uint64_t lastStatusMonoUs() const { return last_status_mono_us_; }

  static uint32_t crc32c(const uint8_t* data, size_t length);
  static size_t cobsDecode(const uint8_t* input, size_t length,
                           uint8_t* output, size_t capacity);

 private:
  uint8_t encoded_[kFeederWireMaxEncodedPacket] = {};
  uint8_t raw_[kFeederWireMaxRawPacket] = {};
  size_t encoded_length_ = 0;
  bool discard_until_delimiter_ = false;
  bool session_valid_ = false;
  bool packet_sequence_valid_ = false;
  bool frame_sequence_valid_ = false;
  bool status_valid_ = false;
  uint32_t expected_packet_sequence_ = 0;
  uint32_t expected_frame_sequence_ = 0;
  uint64_t last_packet_mono_us_ = 0;
  uint64_t last_status_mono_us_ = 0;
  FeederCanFrameFn frame_fn_ = nullptr;
  void* context_ = nullptr;
  FeederWireStats stats_ = {};
  FeederStatus status_ = {};

  void finishPacket(uint64_t arrival_mono_us);
  bool acceptHeader(const uint8_t* raw, size_t raw_length,
                    uint64_t arrival_mono_us);
  void notePacketSequence(uint32_t sequence);
  void noteFrameSequence(uint32_t sequence);
};

}  // namespace csm::board::feeder

