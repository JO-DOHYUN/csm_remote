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
  int32_t last_network_error = 0;
  uint8_t last_failure_phase = 0;
  int32_t last_failure_result = 0;
  uint8_t current_call_phase = 0;
  uint8_t current_call_flags = 0;
  uint32_t current_call_sequence = 0;
  uint32_t current_call_started_ms = 0;
  uint32_t current_call_duration_us = 0;
  int32_t current_call_result = 0;
  uint32_t worker_heartbeat_age_ms = 0;
  uint32_t control_connection_epoch = 0;
  uint8_t control_flags = 0;
  uint8_t configured_socket_max = 0;
  uint8_t configured_tcp_socket_max = 0;
  uint8_t configured_tcp_server_max = 0;
  uint8_t required_application_sockets = 0;
  uint8_t required_total_socket_arena = 0;
  uint32_t socket_arena_capacity = 0;
  uint32_t socket_arena_used = 0;
  uint32_t socket_arena_high_water = 0;
  uint32_t socket_arena_allocation_failures = 0;
};

uint16_t build_wifi_transport_diagnostic_payload(
    const WifiTransportDiagnosticSnapshot& snapshot, uint8_t* payload,
    uint16_t capacity);

}  // namespace csm::board::uplink
