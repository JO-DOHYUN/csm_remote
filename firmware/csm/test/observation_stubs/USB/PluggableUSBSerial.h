#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>
struct ObservationTestUsb {
  bool online=true; uint32_t write_limit=0xffffffffu;
  std::vector<uint8_t> bytes;
  bool connected() const {return online;}
  void send_nb(const uint8_t* data,uint32_t requested,uint32_t* actual,bool) {
    *actual=std::min(requested,write_limit);bytes.insert(bytes.end(),data,data+*actual);
  }
};
inline ObservationTestUsb _SerialUSB;
