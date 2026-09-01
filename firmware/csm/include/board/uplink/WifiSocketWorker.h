#pragma once

#include <atomic>
#include <stdint.h>

#include "board/uplink/WifiWorkerContract.h"
#include "board/uplink/WifiControlPlaneMailbox.h"
#include "board/uplink/WifiWorkerMailbox.h"

#if BOARD_ENABLE_WIFI_UPLINK
#include <TCPSocket.h>
#include <WhdSoftAPInterface.h>
#include <rtos/EventFlags.h>
#include <rtos/Thread.h>
#endif

#ifndef BOARD_WIFI_SOCKET_WORKER_STACK_BYTES
#define BOARD_WIFI_SOCKET_WORKER_STACK_BYTES 16384
#endif

namespace csm::board::uplink {

#if BOARD_ENABLE_WIFI_UPLINK
class WifiSocketWorker final {
 public:
  WifiSocketWorker(WifiWorkerMailbox& mailbox,
                   WifiControlPlaneMailbox& control_mailbox);
  bool start(const WifiTcpSinkConfig& config);

 private:
  WifiWorkerMailbox& mailbox_;
  WifiControlPlaneMailbox& control_mailbox_;
  WifiTcpSinkConfig config_;
  WifiWorkerStateSnapshot state_;
  WhdSoftAPInterface* ap_interface_ = nullptr;
  TCPSocket server_;
  TCPSocket* client_ = nullptr;
  TCPSocket control_server_;
  TCPSocket* control_client_ = nullptr;
  bool ap_started_ = false;
  bool server_opened_ = false;
  bool control_server_opened_ = false;
  uint8_t tx_buffer_[BOARD_WIFI_TX_CHUNK_BYTES] = {};
  uint8_t rx_buffer_[256] = {};
  uint8_t control_tx_buffer_[WifiControlPlaneMailbox::kFrameCapacity] = {};
  uint8_t control_rx_buffer_[256] = {};
  uint16_t control_tx_length_ = 0;
  uint16_t control_tx_offset_ = 0;
  uint16_t control_anchor_length_ = 0;
  uint16_t control_anchor_offset_ = 0;
  bool control_anchor_pending_ = false;
  uint32_t handled_control_disconnect_sequence_ = 0;
  uint32_t last_control_accept_poll_ms_ = 0;
  WifiTxProgressTracker control_tx_progress_;
  WifiMailboxSessionAnchor session_anchor_;
  uint16_t session_anchor_offset_ = 0;
  uint16_t session_anchor_accounted_offset_ = 0;
  bool session_anchor_required_ = false;
  bool session_anchor_loaded_ = false;
  WifiMailboxTxLease pending_lease_;
  WifiMailboxAdmissionSnapshot last_admission_snapshot_;
  uint16_t pending_consumed_bytes_ = 0;
  bool pending_consume_ = false;
  uint32_t handled_abort_sequence_ = 0;
  uint32_t handled_disconnect_sequence_ = 0;
  uint32_t handled_queue_pressure_disconnect_sequence_ = 0;
  WifiQueuePressureTracker queue_pressure_;
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
  void serviceControlRequests();
  void serviceControlAccept(uint32_t now_ms);
  void serviceControlClient(uint32_t now_ms);
  void serviceControlReceive();
  void serviceControlTransmit(uint32_t now_ms);
  WifiTransmitPumpResult serviceSessionAnchor(uint32_t now_ms);
  WifiTransmitPumpResult serviceTransmit(uint32_t now_ms);
  void updateQueuePressure(uint32_t now_ms);
  void notePumpResult(const WifiTransmitPumpResult& result);
  bool closePendingIsolationBeforeSocketSend();
  bool closePendingIsolationAfterPositiveSend();
  bool refreshAdmissionSnapshotForSettlement();
  bool applyPendingConsume();
  void closeClient(WifiCloseReason reason);
  void closeControlClient();
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
