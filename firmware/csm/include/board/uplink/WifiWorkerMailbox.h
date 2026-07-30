#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

#include "board/uplink/FixedFrameByteQueue.h"
#include "board/uplink/ProductDownlinkRouter.h"
#include "board/uplink/WifiWorkerContract.h"

#ifndef BOARD_WIFI_SINK_QUEUE_RECORDS
#define BOARD_WIFI_SINK_QUEUE_RECORDS 128
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS 4
#endif

#ifndef BOARD_WIFI_SINK_QUEUE_BYTES
#define BOARD_WIFI_SINK_QUEUE_BYTES 8192
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES 2112
#endif

#ifndef BOARD_WIFI_RX_MAILBOX_BYTES
#define BOARD_WIFI_RX_MAILBOX_BYTES 1024
#endif

// At the product ingress envelope, the worker must observe pressure early
// enough for one maximum frame plus one fallback interval to arrive without
// entering the critical reserve.
static constexpr uint32_t kWifiPressureThresholdBytes =
    (static_cast<uint64_t>(BOARD_WIFI_SINK_QUEUE_BYTES) *
         BOARD_WIFI_ISOLATE_HIGH_WATER_PERCENT +
     99u) /
    100u;
static constexpr uint32_t kWifiFallbackIngressBytes =
    (static_cast<uint64_t>(BOARD_WIFI_PRODUCT_TARGET_BYTES_PER_SECOND) *
         BOARD_WIFI_CONNECTED_FALLBACK_MS +
     999u) /
    1000u;
static_assert(
    kWifiPressureThresholdBytes +
            csm::encoded_typed_frame_len(csm::kMaxPayloadLen) +
            kWifiFallbackIngressBytes <=
        BOARD_WIFI_SINK_QUEUE_BYTES - BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES,
    "Wi-Fi pressure boundary cannot protect the critical byte reserve");

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

struct WifiMailboxTxLease {
  uint32_t generation = 0;
  uint16_t length = 0;
};

struct WifiMailboxSessionAnchor {
  uint8_t bytes[csm::encoded_typed_frame_len(csm::kMaxPayloadLen)] = {};
  uint64_t publish_seq = 0;
  uint16_t length = 0;
};

struct WifiMailboxAbortResult {
  uint32_t bytes = 0;
  uint32_t records = 0;
  uint64_t first_publish_seq = 0;
  uint64_t last_publish_seq = 0;
  bool sequence_valid = false;
};

struct WifiMailboxAdmissionSnapshot {
  uint64_t accepted_bytes_total = 0;
  uint32_t accepted_records_total = 0;
  uint64_t last_accepted_publish_seq = 0;
  bool sequence_valid = false;
};

class WifiWorkerMailbox {
 public:
  using TxQueue = FixedFrameByteQueue<BOARD_WIFI_SINK_QUEUE_RECORDS,
                                      BOARD_WIFI_SINK_QUEUE_BYTES>;
  using TxStorage = typename TxQueue::Storage;
  using TxConsumeResult = TxQueue::ConsumeResult;

  explicit WifiWorkerMailbox(TxStorage& storage);

  void setNotifier(const WifiWorkerNotifier& notifier);
  void configureSession(uint64_t boot_session_id);
  void activateLiveSession();
  void deactivateLiveSession();
  bool liveSessionActive() const;
  WifiMailboxOfferResult tryOffer(const PublishedFrameView& frame,
                                  uint32_t now_ms);
  bool tryReadSessionAnchor(WifiMailboxSessionAnchor& anchor) const;
  bool tryStageTx(uint8_t* destination, uint16_t capacity,
                  WifiMailboxTxLease& lease);
  bool tryConsumeTx(const WifiMailboxTxLease& lease, uint16_t bytes,
                    TxConsumeResult& result,
                    bool& stale_generation);
  bool tryApplyAbort(uint16_t session_anchor_sent_bytes,
                     WifiMailboxAbortResult& result);
  bool tryReadAdmissionSnapshot(WifiMailboxAdmissionSnapshot& snapshot) const;
  void requestAdmissionReconcile();

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
  void invalidateRxEpoch();
  void resetRxForNewEpoch();
  uint32_t rxEpochGeneration() const;

  void beginCall(WifiWorkerCallPhase phase, uint32_t now_ms);
  void endCall(uint32_t now_ms, uint32_t duration_us, int32_t result);
  WifiWorkerCallSnapshot callSnapshot() const;

  void publishState(const WifiWorkerStateSnapshot& state);
  bool tryReadState(WifiWorkerStateSnapshot& state) const;

 private:
  using Queue = TxQueue;

  Queue queue_;
  ProductDownlinkRouter downlink_router_;
  static constexpr uint32_t kProducerGateActive = 1u << 0;
  static constexpr uint32_t kProducerGateAbort = 1u << 1;
  // One atomic gate prevents the producer and aborter from each observing the
  // other as idle on weakly ordered M7 memory.
  std::atomic<uint32_t> producer_abort_gate_{0};
  std::atomic<bool> live_session_active_{false};
  std::atomic<uint32_t> live_session_generation_{0};
  std::atomic<uint32_t> queue_generation_{0};
  std::atomic<uint32_t> first_queued_ms_{0};
  std::atomic<uint32_t> latency_records_{0};
  uint64_t boot_session_id_ = 0;
  // Each TCP epoch clears this slot. The next canonical STREAM_SESSION is
  // captured here and must be socket-sent before any queued live frame.
  uint8_t session_anchor_bytes_
      [csm::encoded_typed_frame_len(csm::kMaxPayloadLen)] = {};
  uint64_t session_anchor_publish_seq_ = 0;
  std::atomic<uint16_t> session_anchor_length_{0};
  std::atomic<uint32_t> unaccepted_postcommit_bytes_{0};
  std::atomic<uint32_t> unaccepted_postcommit_records_{0};
  std::atomic<uint32_t> admission_revision_{0};
  std::atomic<uint32_t> accepted_bytes_total_low_{0};
  std::atomic<uint32_t> accepted_bytes_total_high_{0};
  std::atomic<uint32_t> accepted_records_total_{0};
  // M7 provides native lock-free 32-bit atomics. Publish the 64-bit sequence
  // as two words under admission_revision_ instead of pulling a hidden
  // libatomic lock into the producer path.
  std::atomic<uint32_t> last_accepted_publish_seq_low_{0};
  std::atomic<uint32_t> last_accepted_publish_seq_high_{0};
  std::atomic<bool> accepted_sequence_valid_{false};
  std::atomic<bool> admission_reconcile_requested_{false};
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
  std::atomic<uint32_t> rx_epoch_generation_{0};

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
  void beginAdmissionTransition();
  void endAdmissionTransition();
  void noteAccepted(const PublishedFrameView& frame);
  void releaseProducerGate();
  void notifyWorker(uint32_t bits) const;
  bool pushPassthroughRx(const uint8_t* bytes, uint16_t length);
  static bool passthroughThunk(void* context, const uint8_t* bytes,
                              uint16_t length);
};

}  // namespace csm::board::uplink
