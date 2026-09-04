#include <cstdint>
#include <cstring>
#include <iostream>

#include "board/uplink/WifiControlPlaneMailbox.h"
#include "board/uplink/WifiRealtimeMailbox.h"
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

void realtimeHandoffsAreDepthOneAndNeverReplay() {
  csm::board::uplink::WifiRealtimeMailbox mailbox;
  mailbox.reset();
  const uint8_t first[] = {1, 2, 3};
  const uint8_t latest[] = {7, 8, 9, 10};
  uint32_t first_token = 0;
  uint32_t latest_token = 0;
  CHECK(mailbox.publishRx(first, sizeof(first), 100, &first_token));
  CHECK(mailbox.publishRx(latest, sizeof(latest), 120, &latest_token));
  CHECK(first_token != 0 && latest_token != first_token);
  csm::board::uplink::WifiRealtimeDatagram datagram;
  CHECK(mailbox.takeLatestRx(&datagram));
  CHECK(datagram.token == latest_token && datagram.length == sizeof(latest));
  CHECK(std::memcmp(datagram.bytes, latest, sizeof(latest)) == 0);
  CHECK(!mailbox.takeLatestRx(&datagram));
  CHECK(mailbox.evidence().rx_datagrams == 2);
  CHECK(mailbox.evidence().rx_overwrite == 1);
  CHECK(mailbox.evidence().rx_max_gap_ms == 20);

  uint8_t proof[csm::board::uplink::kRealtimeProofFrameBytes] = {};
  CHECK(mailbox.stageProof(proof, sizeof(proof), latest_token, 1, 121));
  csm::board::uplink::WifiRealtimeProof first_proof;
  CHECK(mailbox.peekProof(&first_proof));
  proof[0] = 0x55;
  CHECK(mailbox.stageProof(proof, sizeof(proof), latest_token, 2, 122));
  csm::board::uplink::WifiRealtimeProof latest_proof;
  CHECK(mailbox.peekProof(&latest_proof));
  CHECK(latest_proof.token != first_proof.token && latest_proof.bytes[0] == 0x55);
  mailbox.consumeProof(first_proof.token);
  CHECK(mailbox.peekProof(&latest_proof));
  mailbox.consumeProof(latest_proof.token);
  CHECK(!mailbox.peekProof(&latest_proof));

  CHECK(mailbox.stageProof(proof, sizeof(proof), latest_token, 3, 123));
  mailbox.discardProof();
  CHECK(!mailbox.peekProof(&latest_proof));
  mailbox.noteSocketReady(true, 9);
  mailbox.noteProofSend(-3001, 3, 124, true);
  mailbox.noteProofSend(sizeof(proof), 3, 125, false);
  const auto evidence = mailbox.evidence();
  CHECK(evidence.network_epoch == 9 && evidence.socket_ready);
  CHECK(evidence.proof_staged == 3 && evidence.proof_would_block == 1);
  CHECK(evidence.proof_sent == 1 && evidence.proof_last_sequence == 3);
}
}  // namespace

int main() {
  activationAnchorsIdentityAndAckSequence();
  overflowClosesEpochAndReconnectDoesNotReplay();
  rxIsBoundedAndClearedAcrossEpochs();
  realtimeHandoffsAreDepthOneAndNeverReplay();
  if (failures != 0) return 1;
  std::cout << "Wi-Fi control-plane contract PASS\n";
  return 0;
}
