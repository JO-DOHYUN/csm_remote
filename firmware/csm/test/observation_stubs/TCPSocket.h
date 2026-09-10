#pragma once
#include "UDPSocket.h"
class TCPSocket {
 public:
  std::deque<int> send_results;
  std::deque<UDPSocket::Rx> incoming;
  int closes=0;
  int send(const void*,size_t length) {
    if(send_results.empty())return int(length);
    const int r=send_results.front();send_results.pop_front();return r;
  }
  int recv(void* bytes,size_t length) {
    if(incoming.empty())return NSAPI_ERROR_WOULD_BLOCK;
    auto rx=incoming.front();incoming.pop_front();
    if(rx.result>0)memcpy(bytes,rx.bytes.data(),std::min(rx.bytes.size(),length));
    return rx.result;
  }
  int close(){++closes;return 0;}
  template<class F> void sigio(F){}
};
