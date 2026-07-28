#include "board/uplink/LinkReliabilityDiagnostic.h"

#include <string.h>

#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

uint16_t build_link_reliability_diagnostic_payload(
    const LinkReliabilityDiagnosticSnapshot& snapshot, uint8_t* payload,
    uint16_t capacity) {
  if (payload == nullptr ||
      capacity < csm::kLinkReliabilityDiagnosticPayloadLen) {
    return 0;
  }
  memset(payload, 0, csm::kLinkReliabilityDiagnosticPayloadLen);
  csm::wr_u64_le(
      &payload[csm::kLinkReliabilityDiagnosticMonoUsOffset],
      snapshot.mono_us);
  payload[csm::kLinkReliabilityDiagnosticSchemaOffset] =
      csm::kLinkReliabilityDiagnosticSchema;
  payload[csm::kLinkReliabilityDiagnosticFlagsOffset] = snapshot.flags;
  payload[csm::kLinkReliabilityDiagnosticCloseReasonOffset] =
      snapshot.close_reason;
  csm::wr_u32_le(
      &payload[csm::kLinkReliabilityDiagnosticConnectionEpochOffset],
      snapshot.connection_epoch);
#define CSM_LINK_DIAG_U64(offset, value) \
  csm::wr_u64_le(&payload[(offset)], (value))
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticBootSessionOffset,
                    snapshot.boot_session_id);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticLastAcceptedSeqOffset,
                    snapshot.last_accepted_publish_seq);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticHighestSentSeqOffset,
                    snapshot.highest_sent_publish_seq);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticLastAckedSeqOffset,
                    snapshot.last_acked_publish_seq);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticFirstNotAdmittedSeqOffset,
                    snapshot.first_not_admitted_publish_seq);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticOfferedBytesOffset,
                    snapshot.offered_bytes_total);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticAdmittedBytesOffset,
                    snapshot.admitted_bytes_total);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticSocketSentBytesOffset,
                    snapshot.socket_sent_bytes_total);
  CSM_LINK_DIAG_U64(csm::kLinkReliabilityDiagnosticReclaimedBytesOffset,
                    snapshot.reclaimed_bytes_total);
#undef CSM_LINK_DIAG_U64
#define CSM_LINK_DIAG_U32(offset, value) \
  csm::wr_u32_le(&payload[(offset)], (value))
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticRetainedBytesOffset,
                    snapshot.retained_bytes);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticUnsentBytesOffset,
                    snapshot.unsent_bytes);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticHighWaterBytesOffset,
                    snapshot.high_water_bytes);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticRetainedRecordsOffset,
                    snapshot.retained_records);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticUnsentRecordsOffset,
                    snapshot.unsent_records);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticHighWaterRecordsOffset,
                    snapshot.high_water_records);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticAckAcceptedOffset,
                    snapshot.ack_accepted_total);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticAckRejectedOffset,
                    snapshot.ack_rejected_total);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticRewindTotalOffset,
                    snapshot.rewind_total);
  CSM_LINK_DIAG_U32(csm::kLinkReliabilityDiagnosticJournalFullOffset,
                    snapshot.journal_full_total);
#undef CSM_LINK_DIAG_U32
  return csm::kLinkReliabilityDiagnosticPayloadLen;
}

}  // namespace csm::board::uplink
