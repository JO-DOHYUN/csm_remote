#pragma once

#include <stdint.h>

namespace csm::board::uplink {

struct LinkReliabilityDiagnosticSnapshot {
  uint64_t mono_us = 0;
  uint64_t boot_session_id = 0;
  uint64_t last_accepted_publish_seq = 0;
  uint64_t highest_sent_publish_seq = 0;
  uint64_t last_acked_publish_seq = 0;
  uint64_t first_not_admitted_publish_seq = 0;
  uint64_t offered_bytes_total = 0;
  uint64_t admitted_bytes_total = 0;
  uint64_t socket_sent_bytes_total = 0;
  uint64_t reclaimed_bytes_total = 0;
  uint32_t retained_bytes = 0;
  uint32_t unsent_bytes = 0;
  uint32_t high_water_bytes = 0;
  uint32_t retained_records = 0;
  uint32_t unsent_records = 0;
  uint32_t high_water_records = 0;
  uint32_t ack_accepted_total = 0;
  uint32_t ack_rejected_total = 0;
  uint32_t rewind_total = 0;
  uint32_t journal_full_total = 0;
  uint32_t connection_epoch = 0;
  uint8_t flags = 0;
  uint8_t close_reason = 0;
};

uint16_t build_link_reliability_diagnostic_payload(
    const LinkReliabilityDiagnosticSnapshot& snapshot, uint8_t* payload,
    uint16_t capacity);

}  // namespace csm::board::uplink
