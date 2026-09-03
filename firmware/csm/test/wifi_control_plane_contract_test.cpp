#include <cstdint>
#include <cstring>
#include <iostream>

#include "board/uplink/WifiControlPlaneMailbox.h"
#include "protocol/TypedRecords.h"

namespace {
int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "FAIL line " << __LINE__ << ": " #condition << '\n';      \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

void activationAnchorsIdentityAndAckSequence() {
  csm::board::uplink::WifiControlPlaneMailbox mailbox;
  mailbox.configure(0x1122334455667788ULL);
  CHECK(mailbox.activate(123456));
  CHECK(mailbox.connected());
  CHECK(mailbox.connectionEpoch() == 1);

  uint8_t frame[64] = {};
  uint16_t length = 0;
  CHECK(mailbox.copyAnchor(frame, sizeof(frame), length));
  CHECK(frame[3] == static_cast<uint8_t>(csm::RecordType::StreamSession));
  CHECK(csm::rd_u16_le(&frame[7]) == csm::kStreamSessionPayloadLen);
  CHECK(csm::rd_u64_le(&frame[9 + csm::kStreamSessionBootIdOffset]) ==
        0x1122334455667788ULL);

  uint8_t ack[csm::kControlAckPayloadLen] = {};
  csm::wr_u32_le(&ack[csm::kControlAckCommandIdOffset], 77);
  CHECK(mailbox.offerAck(ack, sizeof(ack)));
  CHECK(mailbox.peekTx(frame, sizeof(frame), length));
  CHECK(frame[3] == static_cast<uint8_t>(csm::RecordType::ControlAck));
  CHECK(csm::rd_u16_le(&frame[5]) == 1);
  CHECK(csm::rd_u32_le(&frame[9 + csm::kControlAckCommandIdOffset]) == 77);
  mailbox.consumeTx();
  CHECK(mailbox.queuedRecords() == 0);
  mailbox.noteReceive(13, 100);
  mailbox.noteSend(8, 110, 10, 77, 8, false);
  CHECK(mailbox.evidence().ack_sent_id == 0); // Partial send is not terminal TX.
  mailbox.noteSend(31, 111, 12, 77, 39, true);
  CHECK(mailbox.evidence().ack_sent_id == 77);
  CHECK(mailbox.evidence().ack_admitted == 1);
  mailbox.noteClose(6, -3001, 400);
  mailbox.deactivate();
  CHECK(mailbox.activate(500000));
  mailbox.noteClose(1, 0, 600);
  const auto retained = mailbox.evidence();
  CHECK(retained.first[0] == 6 && retained.first[1] == 400);
  CHECK(retained.first[3] == 13 && retained.first[5] == 77);
  CHECK(retained.close_reason == 1 && retained.close_total == 2);
  CHECK(retained.first[7] == static_cast<uint32_t>(-3001));
}

void overflowClosesEpochAndReconnectDoesNotReplay() {
  csm::board::uplink::WifiControlPlaneMailbox mailbox;
  mailbox.configure(9);
  CHECK(mailbox.activate(1));
  uint8_t ack[csm::kControlAckPayloadLen] = {};
  for (uint32_t i = 0; i < mailbox.kQueueRecords; ++i) {
    csm::wr_u32_le(&ack[csm::kControlAckCommandIdOffset], i + 1);
    CHECK(mailbox.offerAck(ack, sizeof(ack)));
  }
  const uint32_t disconnect_sequence = mailbox.disconnectRequestSequence();
  CHECK(!mailbox.offerAck(ack, sizeof(ack)));
  CHECK(!mailbox.connected());
  CHECK(mailbox.disconnectRequestSequence() == disconnect_sequence + 1);
  CHECK(mailbox.connectionEpoch() == 2);

  mailbox.deactivate();
  CHECK(mailbox.activate(2));
  CHECK(mailbox.connectionEpoch() == 3);
  uint8_t frame[64] = {};
  uint16_t length = 0;
  CHECK(!mailbox.peekTx(frame, sizeof(frame), length));
}

void rxIsBoundedAndClearedAcrossEpochs() {
  csm::board::uplink::WifiControlPlaneMailbox mailbox;
  mailbox.configure(1);
  CHECK(mailbox.activate(1));
  const uint8_t bytes[] = {1, 2, 3};
  CHECK(mailbox.pushRx(bytes, sizeof(bytes)));
  CHECK(mailbox.available() == 3);
  CHECK(mailbox.read() == 1);
  mailbox.deactivate();
  CHECK(mailbox.available() == 0);
  CHECK(!mailbox.pushRx(bytes, sizeof(bytes)));
}
}  // namespace

int main() {
  activationAnchorsIdentityAndAckSequence();
  overflowClosesEpochAndReconnectDoesNotReplay();
  rxIsBoundedAndClearedAcrossEpochs();
  if (failures != 0) return 1;
  std::cout << "Wi-Fi control-plane contract PASS\n";
  return 0;
}
