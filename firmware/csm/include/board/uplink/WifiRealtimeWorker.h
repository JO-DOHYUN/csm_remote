#pragma once

#include <stdint.h>

#include "board/uplink/WifiRealtimeMailbox.h"

#if BOARD_ENABLE_WIFI_UPLINK
#include <NetworkInterface.h>
#include <SocketAddress.h>
#include <UDPSocket.h>
#include <rtos/EventFlags.h>
#include <rtos/Thread.h>
#endif

#ifndef BOARD_WIFI_REALTIME_WORKER_STACK_BYTES
#define BOARD_WIFI_REALTIME_WORKER_STACK_BYTES 6144
#endif

namespace csm::board::uplink {

#if BOARD_ENABLE_WIFI_UPLINK
class WifiRealtimeWorker final {
 public:
  explicit WifiRealtimeWorker(WifiRealtimeMailbox& mailbox)
      : mailbox_(mailbox) {}
  bool start(NetworkInterface* network, uint16_t port,
             uint32_t network_epoch);

 private:
  static constexpr uint32_t kWakeSocket = 1u << 0;
  static constexpr uint32_t kWakeProof = 1u << 1;
  static constexpr uint8_t kRxDatagramsPerTurn = 4;
  static constexpr uint32_t kFallbackMs = 5;
  static constexpr uint32_t kReopenDelayMs = 100;

  WifiRealtimeMailbox& mailbox_;
  NetworkInterface* network_ = nullptr;
  uint16_t port_ = 0;
  uint32_t network_epoch_ = 0;
  UDPSocket socket_;
  bool socket_open_ = false;
  uint32_t socket_open_rx_floor_ = 0;
  uint32_t next_open_ms_ = 0;
  uint8_t rx_buffer_[kRealtimeDatagramCapacity] = {};
  rtos::EventFlags wake_flags_;
  alignas(8) unsigned char thread_stack_[BOARD_WIFI_REALTIME_WORKER_STACK_BYTES] = {};
  alignas(rtos::Thread) unsigned char thread_storage_[sizeof(rtos::Thread)] = {};
  rtos::Thread* thread_ = nullptr;
  bool thread_started_ = false;

  void run();
  bool openSocket();
  void closeSocket(int32_t result);
  void serviceReceive();
  void serviceProof();
  void onSocketStateChanged();
  static void notifyProof(void* context);
};
#endif

}  // namespace csm::board::uplink
