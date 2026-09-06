#pragma once

#include <atomic>
#include <stdint.h>

#include "protocol/TypedFrame.h"
#include "protocol/TypedRecords.h"

namespace csm::board::uplink {

static constexpr uint16_t kRealtimeStateFrameBytes =
    static_cast<uint16_t>(
        csm::encoded_typed_frame_len(csm::kHostRealtimeStateV1PayloadLen));
static constexpr uint16_t kRealtimeProofFrameBytes =
    static_cast<uint16_t>(
        csm::encoded_typed_frame_len(csm::kRealtimeProofV1PayloadLen));
static constexpr uint16_t kRealtimeDatagramCapacity = 128;

struct WifiRealtimePeer {
  char address[48] = {};
  uint16_t port = 0;
};

constexpr bool realtimeRxAfterSocketOpen(uint32_t floor, uint32_t token) {
  const uint32_t delta = token - floor;
  return token != 0u && delta != 0u && delta < 0x80000000u;
}

struct WifiRealtimeDatagram {
  uint8_t bytes[kRealtimeDatagramCapacity] = {};
  uint16_t length = 0;
  uint32_t token = 0;
  uint32_t arrival_ms = 0;
  WifiRealtimePeer peer = {};
};

struct WifiRealtimeProof {
  uint8_t bytes[kRealtimeProofFrameBytes] = {};
  uint16_t length = 0;
  uint32_t token = 0;
  uint32_t rx_token = 0;
  uint32_t staged_ms = 0;
  WifiRealtimePeer peer = {};
};

struct WifiRealtimeEvidence {
  bool worker_started = false;
  bool socket_ready = false;
  uint32_t network_epoch = 0;
  uint32_t rx_datagrams = 0;
  uint32_t rx_bytes = 0;
  uint32_t rx_overwrite = 0;
  uint32_t rx_last_token = 0;
  uint32_t rx_last_ms = 0;
  uint32_t rx_max_gap_ms = 0;
  uint32_t rx_budget_hits = 0;
  uint32_t proof_staged = 0;
  uint32_t proof_sent = 0;
  uint32_t proof_would_block = 0;
  uint32_t proof_socket_error = 0;
  uint32_t proof_last_sequence = 0;
  uint32_t proof_last_sent_ms = 0;
  uint32_t socket_error = 0;
  int32_t last_socket_result = 0;
  uint32_t worker_heartbeat_ms = 0;
};

struct WifiRealtimeNotifier {
  void* context = nullptr;
  void (*notify)(void* context) = nullptr;
};

// Single-producer/single-consumer, depth-1 handoff in each direction. A new
// datagram replaces an unread old datagram; proofs remain pending only until
// sent or superseded and are never replayed across a socket epoch.
class WifiRealtimeMailbox {
 public:
  void reset();
  void setNotifier(WifiRealtimeNotifier notifier) { notifier_ = notifier; }
  bool publishRx(const uint8_t* bytes, uint16_t length,
                 uint32_t arrival_ms, const WifiRealtimePeer& peer,
                 uint32_t* token);
  bool takeLatestRx(WifiRealtimeDatagram* datagram);
  bool stageProof(const uint8_t* bytes, uint16_t length,
                  const WifiRealtimeDatagram& request, uint32_t proof_sequence,
                  uint32_t now_ms);
  bool peekProof(WifiRealtimeProof* proof) const;
  void consumeProof(uint32_t token);
  void discardProof();

  void noteWorkerStarted(bool started);
  void noteSocketReady(bool ready, uint32_t network_epoch);
  void noteRxBudgetHit();
  void noteProofSend(int32_t result, uint32_t proof_sequence,
                     uint32_t now_ms, bool would_block);
  void noteSocketError(int32_t result);
  void noteWorkerHeartbeat(uint32_t now_ms);
  WifiRealtimeEvidence evidence() const;

 private:
  static void incrementSaturating(std::atomic<uint32_t>* value);
  void notify();

  WifiRealtimeNotifier notifier_ = {};
  std::atomic_flag rx_guard_ = ATOMIC_FLAG_INIT;
  WifiRealtimeDatagram rx_slot_ = {};
  std::atomic<uint32_t> rx_next_token_{0};
  std::atomic<uint32_t> rx_consumed_token_{0};
  mutable std::atomic_flag proof_guard_ = ATOMIC_FLAG_INIT;
  WifiRealtimeProof proof_slot_ = {};
  std::atomic<uint32_t> proof_next_token_{0};
  std::atomic<uint32_t> proof_consumed_token_{0};

  std::atomic<bool> worker_started_{false};
  std::atomic<bool> socket_ready_{false};
  std::atomic<uint32_t> network_epoch_{0};
  std::atomic<uint32_t> rx_datagrams_{0};
  std::atomic<uint32_t> rx_bytes_{0};
  std::atomic<uint32_t> rx_overwrite_{0};
  std::atomic<uint32_t> rx_last_ms_{0};
  std::atomic<uint32_t> rx_max_gap_ms_{0};
  std::atomic<uint32_t> rx_budget_hits_{0};
  std::atomic<uint32_t> proof_staged_{0};
  std::atomic<uint32_t> proof_sent_{0};
  std::atomic<uint32_t> proof_would_block_{0};
  std::atomic<uint32_t> proof_socket_error_{0};
  std::atomic<uint32_t> proof_last_sequence_{0};
  std::atomic<uint32_t> proof_last_sent_ms_{0};
  std::atomic<uint32_t> socket_error_{0};
  std::atomic<int32_t> last_socket_result_{0};
  std::atomic<uint32_t> worker_heartbeat_ms_{0};
};

}  // namespace csm::board::uplink
