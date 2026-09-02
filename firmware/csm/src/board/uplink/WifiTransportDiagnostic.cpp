#include "board/uplink/WifiTransportDiagnostic.h"

#include <string.h>

#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

uint16_t build_wifi_transport_diagnostic_payload(
    const WifiTransportDiagnosticSnapshot& snapshot, uint8_t* payload,
    uint16_t capacity) {
  if (payload == nullptr || capacity < csm::kTransportDiagnosticPayloadLen) {
    return 0;
  }
  memset(payload, 0, csm::kTransportDiagnosticPayloadLen);
  csm::wr_u64_le(&payload[csm::kTransportDiagnosticMonoUsOffset],
                 snapshot.mono_us);
  payload[csm::kTransportDiagnosticSchemaOffset] =
      csm::kTransportDiagnosticSchema;
  payload[csm::kTransportDiagnosticFlagsOffset] = snapshot.flags;
  payload[csm::kTransportDiagnosticCloseReasonOffset] = snapshot.close_reason;
  payload[csm::kTransportDiagnosticRuntimeModeOffset] = snapshot.runtime_mode;
#define CSM_WIFI_DIAG_U32(offset, value) \
  csm::wr_u32_le(&payload[(offset)], (value))
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticConnectionEpochOffset,
                    snapshot.connection_epoch);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticOfferBytesOffset,
                    snapshot.offer_bytes_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticAcceptedBytesOffset,
                    snapshot.accepted_bytes_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticAcceptedRecordsOffset,
                    snapshot.accepted_records_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticRejectedRecordsOffset,
                    snapshot.rejected_records_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticQueueBytesOffset,
                    snapshot.queue_bytes);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticQueueRecordsOffset,
                    snapshot.queue_records);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticQueueHighWaterBytesOffset,
                    snapshot.queue_high_water_bytes);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticQueueOldestAgeMsOffset,
                    snapshot.queue_oldest_age_ms);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticSocketBytesOffset,
                    snapshot.socket_bytes_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticSocketFramesOffset,
                    snapshot.socket_frames_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticPositiveWritesOffset,
                    snapshot.positive_write_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticWouldBlockOffset,
                    snapshot.would_block_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticSocketErrorsOffset,
                    snapshot.socket_error_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticSendCallMaxUsOffset,
                    snapshot.send_call_max_us);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticNoProgressMaxMsOffset,
                    snapshot.no_progress_max_ms);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticStallCloseOffset,
                    snapshot.stall_close_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticQueuePressureCloseOffset,
                    snapshot.queue_pressure_close_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticQueueHighWaterRecordsOffset,
                    snapshot.queue_high_water_records);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticAbortedBytesOffset,
                    snapshot.aborted_bytes_total);
  CSM_WIFI_DIAG_U32(csm::kTransportDiagnosticAbortedRecordsOffset,
                    snapshot.aborted_records_total);
#undef CSM_WIFI_DIAG_U32
  csm::wr_u64_le(&payload[csm::kTransportDiagnosticFirstLostPublishSeqOffset],
                 snapshot.first_lost_publish_seq);
  csm::wr_u64_le(&payload[csm::kTransportDiagnosticLastLostPublishSeqOffset],
                 snapshot.last_lost_publish_seq);
  csm::wr_u64_le(
      &payload[csm::kTransportDiagnosticLastAcceptedPublishSeqOffset],
      snapshot.last_accepted_publish_seq);
  csm::wr_u64_le(&payload[csm::kTransportDiagnosticLastSentPublishSeqOffset],
                 snapshot.last_sent_publish_seq);
  csm::wr_i32_le(&payload[csm::kTransportDiagnosticLastNetworkErrorOffset],
                 snapshot.last_network_error);
  payload[csm::kTransportDiagnosticLastFailurePhaseOffset] =
      snapshot.last_failure_phase;
  csm::wr_i32_le(&payload[csm::kTransportDiagnosticLastFailureResultOffset],
                 snapshot.last_failure_result);
  payload[csm::kTransportDiagnosticCurrentCallPhaseOffset] =
      snapshot.current_call_phase;
  payload[csm::kTransportDiagnosticCurrentCallFlagsOffset] =
      snapshot.current_call_flags;
  csm::wr_u32_le(&payload[csm::kTransportDiagnosticCurrentCallSequenceOffset],
                 snapshot.current_call_sequence);
  csm::wr_u32_le(&payload[csm::kTransportDiagnosticCurrentCallStartedMsOffset],
                 snapshot.current_call_started_ms);
  csm::wr_u32_le(&payload[csm::kTransportDiagnosticCurrentCallDurationUsOffset],
                 snapshot.current_call_duration_us);
  csm::wr_i32_le(&payload[csm::kTransportDiagnosticCurrentCallResultOffset],
                 snapshot.current_call_result);
  csm::wr_u32_le(
      &payload[csm::kTransportDiagnosticWorkerHeartbeatAgeMsOffset],
      snapshot.worker_heartbeat_age_ms);
  csm::wr_u32_le(
      &payload[csm::kTransportDiagnosticControlConnectionEpochOffset],
      snapshot.control_connection_epoch);
  payload[csm::kTransportDiagnosticControlFlagsOffset] = snapshot.control_flags;
  payload[csm::kTransportDiagnosticConfiguredSocketMaxOffset] =
      snapshot.configured_socket_max;
  payload[csm::kTransportDiagnosticConfiguredTcpSocketMaxOffset] =
      snapshot.configured_tcp_socket_max;
  payload[csm::kTransportDiagnosticConfiguredTcpServerMaxOffset] =
      snapshot.configured_tcp_server_max;
  payload[csm::kTransportDiagnosticRequiredApplicationSocketsOffset] =
      snapshot.required_application_sockets;
  payload[csm::kTransportDiagnosticRequiredTotalSocketArenaOffset] =
      snapshot.required_total_socket_arena;
  csm::wr_u32_le(
      &payload[csm::kTransportDiagnosticSocketArenaCapacityOffset],
      snapshot.socket_arena_capacity);
  csm::wr_u32_le(&payload[csm::kTransportDiagnosticSocketArenaUsedOffset],
                 snapshot.socket_arena_used);
  csm::wr_u32_le(
      &payload[csm::kTransportDiagnosticSocketArenaHighWaterOffset],
      snapshot.socket_arena_high_water);
  csm::wr_u32_le(
      &payload[csm::kTransportDiagnosticSocketArenaAllocationFailuresOffset],
      snapshot.socket_arena_allocation_failures);
  return csm::kTransportDiagnosticPayloadLen;
}

}  // namespace csm::board::uplink
