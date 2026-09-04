// Observation-only reproducer against current source; not a product PASS gate.
#include <cstdio>
#include "board/uplink/RecordAdmission.h"
#include "board/uplink/UplinkPriorityPolicy.h"
int main() {
  using namespace csm::board::uplink;
  RecordAdmission admission;
  admission.begin();
  const uint8_t payload[csm::kControlPathDiagnosticPayloadLen] = {};
  const auto priority = default_priority_for_record(csm::RecordType::ControlPathDiagnostic);
  const bool accepted = admission.enqueue(csm::RecordType::ControlPathDiagnostic,
      payload, sizeof(payload), priority);
  std::printf("record=27 payload=%zu medium=%d accepted=%d alloc_fail=%lu queued=%lu pool_used=%lu\n",
      sizeof(payload), BOARD_UPLINK_POOL_MEDIUM_PAYLOAD_BYTES, accepted,
      static_cast<unsigned long>(admission.counters().pool_alloc_fail_total),
      static_cast<unsigned long>(admission.queuedRecordCount()),
      static_cast<unsigned long>(admission.poolUsedBytes()));
  // Required product admission is deliberately asserted, not weakened to fit failure.
  return accepted ? 0 : 1;
}
