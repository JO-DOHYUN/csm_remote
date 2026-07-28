#pragma once

#include <atomic>
#include <stdint.h>

#include "board/uplink/WifiWorkerContract.h"
#include "board/uplink/WifiWorkerMailbox.h"

#if BOARD_ENABLE_WIFI_UPLINK
#include <TCPSocket.h>
#include <WhdSoftAPInterface.h>
#include <rtos/EventFlags.h>
#include <rtos/Thread.h>
#endif

#ifndef BOARD_WIFI_TX_CHUNK_BYTES
#define BOARD_WIFI_TX_CHUNK_BYTES 2920
#endif

#ifndef BOARD_WIFI_SOCKET_WORKER_STACK_BYTES
#define BOARD_WIFI_SOCKET_WORKER_STACK_BYTES 16384
#endif

namespace csm::board::uplink {

#if BOARD_ENABLE_WIFI_UPLINK
class WifiSocketWorker final {
 public:
 explicit WifiSocketWorker(WifiWorkerMailbox& mailbox);
  bool start(const WifiTcpSinkConfig& config);

 private:
  WifiWorkerMailbox& mailbox_;
  WifiTcpSinkConfig config_;
  WifiWorkerStateSnapshot state_;
  WhdSoftAPInterface* ap_interface_ = nullptr;
  TCPSocket server_;
  TCPSocket* client_ = nullptr;
  bool ap_started_ = false;
  bool server_opened_ = false;
  uint8_t tx_buffer_[BOARD_WIFI_TX_CHUNK_BYTES] = {};
  uint8_t rx_buffer_[256] = {};
  WifiMailboxTxLease pending_lease_;
  uint16_t pending_consumed_bytes_ = 0;
  bool pending_consume_ = false;
  uint32_t handled_abort_sequence_ = 0;
  uint32_t handled_disconnect_sequence_ = 0;
  uint32_t handled_queue_pressure_disconnect_sequence_ = 0;
  WifiTxProgressTracker tx_progress_;
  uint32_t last_accept_poll_ms_ = 0;
  uint32_t last_state_publish_ms_ = 0;
  uint32_t last_stack_sample_ms_ = 0;
  uint32_t next_startup_attempt_ms_ = 0;
  uint32_t current_call_started_us_ = 0;
  bool startup_complete_ = false;
  bool thread_started_ = false;
  std::atomic<uint32_t> sigio_total_{0};
  rtos::EventFlags wake_flags_;

  alignas(8) unsigned char thread_stack_[BOARD_WIFI_SOCKET_WORKER_STACK_BYTES] = {};
  alignas(rtos::Thread) unsigned char thread_storage_[sizeof(rtos::Thread)] = {};
  rtos::Thread* thread_ = nullptr;

  void run();
  static void notifyFromMailbox(void* context, uint32_t bits);
  void onSocketStateChanged();
  void signalWake(uint32_t bits);
  uint32_t nextWaitTimeoutMs(uint32_t now_ms) const;
  void noteWake(uint32_t flags, bool fallback);
  bool initializeNetwork();
  bool rollbackNetwork();
  void quarantineStartupFailure(nsapi_error_t error);
  void serviceRequests();
  void serviceClient(uint32_t now_ms);
  void serviceAccept(uint32_t now_ms);
  void serviceReceive(uint32_t now_ms);
  WifiTransmitPumpResult serviceTransmit(uint32_t now_ms);
  void notePumpResult(const WifiTransmitPumpResult& result);
  bool applyPendingConsume();
  void closeClient(WifiCloseReason reason);
  void closeSocket(TCPSocket*& socket, WifiWorkerCallPhase close_phase);
  void applyAbortRequest();
  void noteSocketError(nsapi_error_t error);
  void beginCall(WifiWorkerCallPhase phase);
  uint32_t endCall(int32_t result);
  void sampleStack(uint32_t now_ms);
  void publishState(uint32_t now_ms, bool force = false);
};
#endif

}  // namespace csm::board::uplink
