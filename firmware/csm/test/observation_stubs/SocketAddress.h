#pragma once
#include <cstring>
#include <cstdint>
class SocketAddress {
 public:
  SocketAddress(const char* ip="192.0.2.1",uint16_t port=3335):port_(port) {
    std::strncpy(ip_,ip,sizeof(ip_)-1);
  }
  const char* get_ip_address() const {return ip_;}
  uint16_t get_port() const {return port_;}
 private: char ip_[48]{};uint16_t port_;
};
