#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "board/uplink/ProductDownlinkRouter.h"
#include "board/uplink/ReliableFrameJournal.h"
#include "board/uplink/WifiWorkerContract.h"

#ifndef BOARD_WIFI_SINK_QUEUE_RECORDS
#define BOARD_WIFI_SINK_QUEUE_RECORDS 1024
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS 4
#endif

#ifndef BOARD_WIFI_SINK_QUEUE_BYTES
#define BOARD_WIFI_SINK_QUEUE_BYTES 65520
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES 2112
#endif

#ifndef BOARD_WIFI_RX_MAILBOX_BYTES
#define BOARD_WIFI_RX_MAILBOX_BYTES 1024
#endif

namespace csm::board::uplink {

enum class WifiMailboxOfferResult : uint8_t {
  Accepted = 0,
  Busy,
  Reserved,
  Full,
  Invalid,
};

struct WifiMailboxQueueSnapshot {
  uint32_t queued_bytes = 0;
  uint32_t unsent_bytes = 0;
  uint32_t high_water_bytes = 0;
  uint32_t first_queued_ms = 0;
  uint32_t empty_to_nonempty_wake_total = 0;
  uint32_t latency_wake_total = 0;
  uint16_t queued_records = 0;
  uint16_t unsent_records = 0;
  uint16_t high_water_records = 0;
  bool latency_bounded = false;
};

struct WifiMailboxReliabilitySnapshot {
  uint64_t boot_session_id = 0;
  uint64_t highest_sent_publish_seq = 0;
  uint64_t last_acked_publish_seq = 0;
  uint64_t reclaimed_bytes_total = 0;
  uint32_t ack_accepted_total = 0;
  uint32_t ack_rejected_total = 0;
  uint32_t rewind_total = 0;
  bool session_active = false;
  bool ack_valid = false;
  bool integrity_fault = false;
  bool replay_active = false;
};

struct WifiMailboxTxLease {
  uint32_t generation = 0;
  uint16_t length = 0;
};

struct WifiMailboxSessionAnchor {
  uint8_t bytes[csm::encoded_typed_frame_len(csm::kMaxPayloadLen)] = {};
  uint64_t publish_seq = 0;
  uint16_t length = 0;
};

class WifiWorkerMailbox {
 public:
  using TxQueue = ReliableFrameJournal<BOARD_WIFI_SINK_QUEUE_RECORDS,
                                       BOARD_WIFI_SINK_QUEUE_BYTES>;
  using TxStorage = typename TxQueue::Storage;
  using TxConsumeResult = TxQueue::SendResult;

  explicit WifiWorkerMailbox(TxStorage& storage);

  void setNotifier(const WifiWorkerNotifier& notifier);
  void configureReliableSession(uint64_t boot_session_id);
  void activateReliableSession();
  void rewindUnacked();
  bool reliableSessionActive() const;
  bool reliableIntegrityFault() const;
  WifiMailboxReliabilitySnapshot workerReliabilitySnapshot() const;
  WifiMailboxOfferResult tryOffer(const PublishedFrameView& frame,
                                  uint32_t now_ms);
  bool tryReadSessionAnchor(WifiMailboxSessionAnchor& anchor) const;
  bool tryStageTx(uint8_t* destination, uint16_t capacity,
                  WifiMailboxTxLease& lease);
  bool tryConsumeTx(const WifiMailboxTxLease& lease, uint16_t bytes,
                    TxConsumeResult& result,
                    bool& stale_generation);
  bool tryApplyAbort(uint32_t& aborted_bytes);

  void requestAbort();
  void requestDisconnect();
  bool acknowledgeQueuePressureDisconnect(uint32_t handled_sequence);
  void markQueuePressureDisconnectHandled(uint32_t handled_sequence);
  uint32_t abortRequestSequence() const;
  uint32_t disconnectRequestSequence() const;
  uint32_t queuePressureDisconnectRequestSequence() const;
  uint32_t queuePressureDisconnectHandledSequence() const;
  bool queuePressureDisconnectLatched() const;

  WifiMailboxQueueSnapshot queueSnapshot() const;

  bool pushRx(const uint8_t* bytes, uint16_t length);
  uint32_t rxAvailable() const;
  int readRx();
  int peekRx() const;
  void discardRx();

  void beginCall(WifiWorkerCallPhase phase, uint32_t now_ms);
  void endCall(uint32_t now_ms, uint32_t duration_us, int32_t result);
  WifiWorkerCallSnapshot callSnapshot() const;

  void publishState(const WifiWorkerStateSnapshot& state);
  bool tryReadState(WifiWorkerStateSnapshot& state) const;

 private:
  using Queue = TxQueue;

  Queue queue_;
  ProductDownlinkRouter downlink_router_;
  std::atomic<bool> producer_active_{false};
  std::atomic<bool> abort_in_progress_{false};
  std::atomic<bool> reliable_session_active_{false};
  std::atomic<bool> reliable_integrity_fault_{false};
  uint64_t boot_session_id_ = 0;
  uint64_t reclaimed_bytes_total_ = 0;
  uint32_t ack_accepted_total_ = 0;
  uint32_t ack_rejected_total_ = 0;
  uint32_t rewind_total_ = 0;
  // The first canonical STREAM_SESSION is the immutable boot identity.
  // It is retained outside the reclaimable journal so every TCP epoch can
  // establish identity before replaying older unacknowledged records.
  uint8_t session_anchor_bytes_
      [csm::encoded_typed_frame_len(csm::kMaxPayloadLen)] = {};
  uint64_t session_anchor_publish_seq_ = 0;
  std::atomic<uint16_t> session_anchor_length_{0};
  std::atomic<uint32_t> abort_request_sequence_{0};
  std::atomic<uint32_t> disconnect_request_sequence_{0};
  std::atomic<uint32_t> queue_pressure_disconnect_request_sequence_{0};
  std::atomic<uint32_t> queue_pressure_disconnect_handled_sequence_{0};
  std::atomic<bool> queue_pressure_disconnect_latched_{false};
  WifiWorkerNotifier notifier_;
  std::atomic<uint32_t> empty_to_nonempty_wake_total_{0};
  std::atomic<uint32_t> latency_wake_total_{0};

  static_assert((BOARD_WIFI_RX_MAILBOX_BYTES & (BOARD_WIFI_RX_MAILBOX_BYTES - 1u)) == 0,
                "Wi-Fi RX mailbox capacity must be a power of two");
  uint8_t rx_bytes_[BOARD_WIFI_RX_MAILBOX_BYTES] = {};
  std::atomic<uint32_t> rx_head_{0};
  std::atomic<uint32_t> rx_tail_{0};

  std::atomic<uint32_t> call_revision_{0};
  std::atomic<uint32_t> call_phase_{0};
  std::atomic<uint32_t> call_in_progress_{0};
  std::atomic<uint32_t> call_sequence_{0};
  std::atomic<uint32_t> call_started_ms_{0};
  std::atomic<uint32_t> call_duration_us_{0};
  std::atomic<int32_t> call_result_{0};
  std::atomic<uint32_t> call_heartbeat_ms_{0};

  static_assert(std::is_trivially_copyable<WifiWorkerStateSnapshot>::value,
                "Wi-Fi worker state must support atomic word publication");
  static constexpr size_t kStateWordCount =
      (sizeof(WifiWorkerStateSnapshot) + sizeof(uint32_t) - 1u) /
      sizeof(uint32_t);
  // One worker publishes. Readers may retry, but publication is never dropped.
  std::atomic<uint32_t> state_revision_{0};
  std::atomic<uint32_t> state_words_[kStateWordCount] = {};

  void requestQueuePressureDisconnect();
  void latchReliableIntegrityFault();
  void notifyWorker(uint32_t bits) const;
  bool pushPassthroughRx(const uint8_t* bytes, uint16_t length);
  bool applyAppAck(uint64_t boot_session_id, uint64_t publish_seq);
  static bool appAckThunk(void* context, uint64_t boot_session_id,
                          uint64_t publish_seq);
  static bool passthroughThunk(void* context, const uint8_t* bytes,
                              uint16_t length);
};

}  // namespace csm::board::uplink
