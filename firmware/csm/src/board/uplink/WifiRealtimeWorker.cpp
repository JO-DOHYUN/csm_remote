#include "board/uplink/WifiRealtimeWorker.h"
#include "board/observability/DebugObservation.h"

#if BOARD_ENABLE_WIFI_UPLINK

#include <Arduino.h>
#include <new>
#include <string.h>

namespace csm::board::uplink {

bool WifiRealtimeWorker::start(NetworkInterface* network, uint16_t port,
                               uint32_t network_epoch) {
  if (thread_started_ || network == nullptr || port == 0u ||
      network_epoch == 0u) return false;
  network_ = network;
  port_ = port;
  network_epoch_ = network_epoch;
  WifiRealtimeNotifier notifier;
  notifier.context = this;
  notifier.notify = &WifiRealtimeWorker::notifyProof;
  mailbox_.setNotifier(notifier);
  mailbox_.noteWorkerStarted(true);
  thread_ = new (thread_storage_)
      rtos::Thread(osPriorityAboveNormal, sizeof(thread_stack_), thread_stack_,
                   "wifi-realtime");
  const osStatus status =
      thread_->start(mbed::callback(this, &WifiRealtimeWorker::run));
  if (status != osOK) {
    thread_->~Thread();
    thread_ = nullptr;
    mailbox_.noteWorkerStarted(false);
    return false;
  }
  thread_started_ = true;
  wake_flags_.set(kWakeSocket);
  return true;
}

void WifiRealtimeWorker::run() {
  while (true) {
    const uint32_t now_ms = millis();
    mailbox_.noteWorkerHeartbeat(now_ms);
    if (!socket_open_ &&
        static_cast<int32_t>(now_ms - next_open_ms_) >= 0) {
      (void)openSocket();
    }
    (void)wake_flags_.wait_any(kWakeSocket | kWakeProof, kFallbackMs, true);
    if (!socket_open_) continue;
    serviceReceive();
    if (socket_open_) serviceProof();
  }
}

bool WifiRealtimeWorker::openSocket() {
  const nsapi_error_t opened = socket_.open(network_);
  if (opened != NSAPI_ERROR_OK) {
    mailbox_.noteSocketError(opened);
    next_open_ms_ = millis() + kReopenDelayMs;
    return false;
  }
  socket_.set_blocking(false);
  const nsapi_error_t bound = socket_.bind(port_);
  if (bound != NSAPI_ERROR_OK) {
    mailbox_.noteSocketError(bound);
    (void)socket_.close();
    next_open_ms_ = millis() + kReopenDelayMs;
    return false;
  }
  socket_.sigio(mbed::callback(this, &WifiRealtimeWorker::onSocketStateChanged));
  socket_open_ = true;
  socket_open_rx_floor_ = mailbox_.evidence().rx_last_token;
  mailbox_.discardProof();
  mailbox_.noteSocketReady(true, network_epoch_);
  return true;
}

void WifiRealtimeWorker::closeSocket(int32_t result) {
  // OBS_BOUNDARY:udp_close DEBUG_TRACE only; preserve existing close/reopen policy.
  CSM_OBS(observation::datagram(network_epoch_,10,observation_operation_,0,0,result,nullptr,0));
  if (socket_open_) {
    socket_.sigio(nullptr);
    (void)socket_.close();
  }
  socket_open_ = false;
  mailbox_.discardProof();
  mailbox_.noteSocketReady(false, network_epoch_);
  mailbox_.noteSocketError(result);
  next_open_ms_ = millis() + kReopenDelayMs;
}

void WifiRealtimeWorker::serviceReceive() {
  uint8_t serviced = 0;
  while (serviced < kRxDatagramsPerTurn) {
    SocketAddress peer;
    // OBS_BOUNDARY:udp_receive DEBUG_TRACE: both sides of actual recvfrom syscall.
    CSM_OBS(++observation_operation_;
      observation::datagram(network_epoch_,1,observation_operation_,0,sizeof(rx_buffer_),0,nullptr,0));
    const nsapi_size_or_error_t received =
        socket_.recvfrom(&peer, rx_buffer_, sizeof(rx_buffer_));
    CSM_OBS(observation::datagram(network_epoch_,2,observation_operation_,0,
      received>0?static_cast<uint16_t>(received):0,received,peer.get_ip_address(),peer.get_port(),
      received>0?rx_buffer_:nullptr,received>0?static_cast<uint16_t>(received):0));
    if (received == NSAPI_ERROR_WOULD_BLOCK) return;
    if (received < 0) {
      closeSocket(received);
      return;
    }
    ++serviced;
    if (received == 0 ||
        received > static_cast<nsapi_size_or_error_t>(sizeof(rx_buffer_))) {
      continue;
    }
    WifiRealtimePeer endpoint;
    const char* address = peer.get_ip_address();
    if (address == nullptr) continue;
    strncpy(endpoint.address, address, sizeof(endpoint.address) - 1u);
    endpoint.port = peer.get_port();
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
    uint32_t observation_token=0;
    const bool observation_staged=mailbox_.publishRx(rx_buffer_,static_cast<uint16_t>(received),
                            millis(),endpoint,&observation_token);
    observation::datagram(network_epoch_,observation_staged?3:7,observation_operation_,
      observation_token,static_cast<uint16_t>(received),observation_staged?1:0,
      endpoint.address,endpoint.port);
#else
    (void)mailbox_.publishRx(rx_buffer_, static_cast<uint16_t>(received),
                            millis(), endpoint, nullptr);
#endif
  }
  mailbox_.noteRxBudgetHit();
}

void WifiRealtimeWorker::serviceProof() {
  WifiRealtimeProof proof;
  if (!mailbox_.peekProof(&proof)) return;
  if (!realtimeRxAfterSocketOpen(socket_open_rx_floor_, proof.rx_token) ||
      proof.peer.address[0] == '\0' || proof.peer.port == 0u) {
    CSM_OBS(observation::proof(2,network_epoch_,9,proof.rx_token,proof.bytes));
    mailbox_.consumeProof(proof.token);
    return;
  }
  const SocketAddress destination(proof.peer.address, proof.peer.port);
  // OBS_BOUNDARY:proof_send DEBUG_TRACE: selected payload/destination and syscall outcome.
  CSM_OBS(++observation_operation_;
    observation::proof(2,network_epoch_,5,proof.rx_token,proof.bytes);
    observation::datagram(network_epoch_,1,observation_operation_,proof.rx_token,
      proof.length,0,proof.peer.address,proof.peer.port,proof.bytes,proof.length));
  const nsapi_size_or_error_t sent =
      socket_.sendto(destination, proof.bytes, proof.length);
  CSM_OBS(observation::datagram(network_epoch_,2,observation_operation_,proof.rx_token,
    proof.length,sent,proof.peer.address,proof.peer.port));
  const bool would_block = sent == NSAPI_ERROR_WOULD_BLOCK;
  const uint32_t proof_sequence = csm::rd_u32_le(
      proof.bytes + 9u + csm::kRealtimeProofSequenceOffset);
  mailbox_.noteProofSend(sent, proof_sequence, millis(), would_block);
  if (sent == static_cast<nsapi_size_or_error_t>(proof.length)) {
    mailbox_.consumeProof(proof.token);
  } else if (would_block) {
    // Keep exactly one newest proof pending. Any later stage replaces it.
  } else {
    mailbox_.consumeProof(proof.token);
    closeSocket(sent);
  }
}

void WifiRealtimeWorker::onSocketStateChanged() {
  wake_flags_.set(kWakeSocket);
}

void WifiRealtimeWorker::notifyProof(void* context) {
  if (context != nullptr) {
    static_cast<WifiRealtimeWorker*>(context)->wake_flags_.set(kWakeProof);
  }
}

}  // namespace csm::board::uplink

#endif
