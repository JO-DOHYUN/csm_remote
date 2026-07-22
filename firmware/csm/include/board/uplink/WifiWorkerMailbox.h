#pragma once

#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "board/uplink/FixedFrameByteQueue.h"
#include "board/uplink/WifiWorkerContract.h"

#ifndef BOARD_WIFI_SINK_QUEUE_RECORDS
#define BOARD_WIFI_SINK_QUEUE_RECORDS 8
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_RECORDS 4
#endif

#ifndef BOARD_WIFI_SINK_QUEUE_BYTES
#define BOARD_WIFI_SINK_QUEUE_BYTES 4096
#endif

#ifndef BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES
#define BOARD_WIFI_SINK_CRITICAL_RESERVE_BYTES 1024
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
  uint32_t high_water_bytes = 0;
  uint32_t first_queued_ms = 0;
  uint8_t queued_records = 0;
  uint8_t high_water_records = 0;
  bool urgent = false;
};

struct WifiMailboxTxLease {
  uint32_t generation = 0;
  uint16_t length = 0;
};

class WifiWorkerMailbox {
 public:
  using TxQueue = FixedFrameByteQueue<BOARD_WIFI_SINK_QUEUE_RECORDS,
                                      BOARD_WIFI_SINK_QUEUE_BYTES>;
  using TxConsumeResult = TxQueue::ConsumeResult;

  WifiWorkerMailbox();

  WifiMailboxOfferResult tryOffer(const PublishedFrameView& frame,
                                  uint32_t now_ms);
  bool tryStageTx(uint8_t* destination, uint16_t capacity,
                  WifiMailboxTxLease& lease);
  bool tryConsumeTx(const WifiMailboxTxLease& lease, uint16_t bytes,
                    TxConsumeResult& result,
                    bool& stale_generation);
  bool tryApplyAbort(uint32_t& aborted_bytes);

  void requestAbort();
  void requestDisconnect();
  uint32_t abortRequestSequence() const;
  uint32_t disconnectRequestSequence() const;

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
  mutable std::atomic_flag queue_lock_ = ATOMIC_FLAG_INIT;
  std::atomic<uint32_t> queue_generation_{0};
  std::atomic<uint32_t> queued_bytes_{0};
  std::atomic<uint32_t> queue_high_water_bytes_{0};
  std::atomic<uint32_t> first_queued_ms_{0};
  std::atomic<uint32_t> queued_records_{0};
  std::atomic<uint32_t> queue_high_water_records_{0};
  std::atomic<bool> urgent_{false};
  std::atomic<uint32_t> abort_request_sequence_{0};
  std::atomic<uint32_t> disconnect_request_sequence_{0};

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

  mutable std::atomic_flag state_lock_ = ATOMIC_FLAG_INIT;
  WifiWorkerStateSnapshot state_;

  void updateQueueSnapshotLocked();
};

}  // namespace csm::board::uplink
