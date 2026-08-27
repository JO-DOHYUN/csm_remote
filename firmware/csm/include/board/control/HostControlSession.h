#pragma once

#include <stdint.h>

namespace csm::board::control {

class HostControlSession {
 public:
  void begin(uint32_t now_ms);
  void update(uint32_t now_ms);
  void heartbeat(uint32_t now_ms);
  uint8_t arm(uint32_t now_ms, uint16_t lease_ms, bool backend_ready);
  uint8_t renew(uint32_t now_ms, uint16_t lease_ms);
  void disarm(uint32_t now_ms);
  void invalidate(uint32_t now_ms);

  bool heartbeatAlive(uint32_t now_ms) const;
  bool leaseAlive(uint32_t now_ms) const;
  bool canAccept(uint32_t now_ms, bool backend_ready, uint8_t* reason) const;
  uint32_t heartbeatAgeMs(uint32_t now_ms) const;
  uint32_t heartbeatMaxGapMs() const { return heartbeat_max_gap_ms_; }
  uint32_t leaseRemainingMs(uint32_t now_ms) const;
  uint32_t timeoutCount() const { return timeout_count_; }
  uint32_t activationEpoch() const { return activation_epoch_; }

 private:
  uint16_t normalizeLease(uint16_t lease_ms) const;
  bool has_heartbeat_ = false;
  bool armed_ = false;
  uint32_t last_heartbeat_ms_ = 0;
  uint32_t heartbeat_max_gap_ms_ = 0;
  uint32_t lease_until_ms_ = 0;
  uint32_t timeout_count_ = 0;
  uint32_t activation_epoch_ = 0;
};

}  // namespace csm::board::control
