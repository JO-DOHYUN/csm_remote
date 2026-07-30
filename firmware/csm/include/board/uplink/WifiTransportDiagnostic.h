#pragma once

#include <stdint.h>

namespace csm::board::uplink {

struct WifiTransportDiagnosticSnapshot {
  uint64_t mono_us = 0;
  uint64_t last_accepted_publish_seq = 0;
  uint64_t last_sent_publish_seq = 0;
  uint32_t connection_epoch = 0;
  uint32_t offer_bytes_total = 0;
  uint32_t accepted_bytes_total = 0;
  uint32_t accepted_records_total = 0;
  uint32_t rejected_records_total = 0;
  uint32_t queue_bytes = 0;
  uint32_t queue_records = 0;
  uint32_t queue_high_water_bytes = 0;
  uint32_t queue_oldest_age_ms = 0;
  uint32_t socket_bytes_total = 0;
  uint32_t socket_frames_total = 0;
  uint32_t positive_write_total = 0;
  uint32_t would_block_total = 0;
  uint32_t socket_error_total = 0;
  uint32_t send_call_max_us = 0;
  uint32_t no_progress_max_ms = 0;
  uint32_t stall_close_total = 0;
  uint32_t queue_pressure_close_total = 0;
  uint32_t queue_high_water_records = 0;
  uint32_t aborted_bytes_total = 0;
  uint32_t aborted_records_total = 0;
  uint64_t first_lost_publish_seq = 0;
  uint64_t last_lost_publish_seq = 0;
  uint8_t flags = 0;
  uint8_t close_reason = 0;
  uint8_t runtime_mode = 0;
};

uint16_t build_wifi_transport_diagnostic_payload(
    const WifiTransportDiagnosticSnapshot& snapshot, uint8_t* payload,
    uint16_t capacity);

}  // namespace csm::board::uplink
