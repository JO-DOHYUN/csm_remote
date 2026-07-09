#include "board/uplink/UplinkPriorityPolicy.h"

namespace csm::board::uplink {

UplinkPriority default_priority_for_record(csm::RecordType type) {
  switch (type) {
    case csm::RecordType::CanRxRaw:
    case csm::RecordType::CanRxSegment:
      return UplinkPriority::CanTruth;
    case csm::RecordType::CanTxRaw:
    case csm::RecordType::ControlAck:
      return UplinkPriority::Critical;
    case csm::RecordType::Capability:
      return UplinkPriority::Critical;
    case csm::RecordType::BoardHealth:
      return UplinkPriority::Normal;
    case csm::RecordType::BoardEvent:
      return UplinkPriority::Normal;
    default:
      return UplinkPriority::Normal;
  }
}

UplinkPriority priority_for_board_event(uint16_t event_code) {
  switch (event_code) {
    case 3:   // CAN_RX_QUEUE_DROP
    case 4:   // ENCODER_FAULT_ASSERTED
    case 5:   // FIELD_POWER_LOST
    case 6:   // ESTOP_ASSERTED
    case 12:  // BUILTIN_CAN_TX_FAILED
    case 13:  // HOST_FRAME_CRC_FAILED
    case 14:  // HOST_CAN_TX_REJECTED
    case 15:  // HOST_CAN_TX_ACCEPTED
    case 17:  // MCP2515_TX_FAILED
    case 18:  // SAFETY_STATE_CHANGED
    case 22:  // FAULT_LOCKOUT_CLEARED
    case 25:  // UPLINK_STAGING_CLEAR
      return UplinkPriority::Critical;
    case 24:  // SERIAL_TX_BACKPRESSURE
    case 9:   // MCP2515_ERROR
    case 10:  // MCP2515_SPI_SNAPSHOT
      return UplinkPriority::Diagnostic;
    default:
      return UplinkPriority::Normal;
  }
}

}  // namespace csm::board::uplink
