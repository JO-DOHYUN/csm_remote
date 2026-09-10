#pragma once
#include "NetworkInterface.h"
#include "SocketAddress.h"
#include <algorithm>
#include <deque>
#include <vector>
// Syscall-only test double: the REAL WifiRealtimeWorker owns transitions.
class UDPSocket {
 public:
  struct Rx { int result; std::vector<uint8_t> bytes; };
  std::deque<Rx> incoming;
  std::deque<int> results;
  std::vector<std::vector<uint8_t>> sends;
  int closed=0,opened=0;
  int open(NetworkInterface*) {++opened;return 0;}
  int bind(uint16_t) {return 0;}
  void set_blocking(bool) {}
  template<class F> void sigio(F) {}
  int close() {++closed;return 0;}
  int recvfrom(SocketAddress*,void* out,size_t length) {
    if(incoming.empty())return NSAPI_ERROR_WOULD_BLOCK;
    const auto rx=incoming.front();incoming.pop_front();
    if(rx.result>0)std::memcpy(out,rx.bytes.data(),std::min(length,rx.bytes.size()));
    return rx.result;
  }
  int sendto(const SocketAddress&,const void* bytes,size_t length) {
    const auto* p=static_cast<const uint8_t*>(bytes);sends.emplace_back(p,p+length);
    if(results.empty())return int(length);
    const int result=results.front();results.pop_front();return result;
  }
};
