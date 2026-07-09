#pragma once

#include <stdint.h>

namespace csm::board {

enum class SafetyState : uint8_t {
  Boot = 0,
  MonitorOnly = 1,
  Ready = 2,
  Armed = 3,
  ControlActive = 4,
  HostTimeout = 5,
  FaultLockout = 6,
  Estop = 7,
};

struct SafetyInputs {
  bool estop_asserted = false;
  bool field_power_ok = true;
  bool encoder_fault = false;
  bool arm_key = false;
  bool control_backend_ready = false;
};

class SafetySupervisor {
 public:
  void begin(uint32_t now_ms);
  void update(uint32_t now_ms, const SafetyInputs& inputs);

  uint8_t heartbeat(uint32_t now_ms);
  uint8_t arm(uint32_t now_ms, uint16_t lease_ms, bool control_backend_ready);
  uint8_t renewLease(uint32_t now_ms, uint16_t lease_ms);
  void disarm(uint32_t now_ms);
  void clearFaultLockout(uint32_t now_ms);
  void setFaultLockout(uint32_t now_ms);
  void noteControlTx(uint32_t now_ms);

  bool canAcceptTx(uint32_t now_ms, bool control_backend_ready, uint8_t* reason) const;
  bool canDriveTxGate() const;

  SafetyState state() const { return state_; }
  SafetyState previousState() const { return previous_state_; }
  bool stateChanged() const { return state_ != previous_state_; }
  bool heartbeatAlive(uint32_t now_ms) const;
  bool leaseAlive(uint32_t now_ms) const;
  uint32_t heartbeatAgeMs(uint32_t now_ms) const;
  uint32_t leaseRemainingMs(uint32_t now_ms) const;
  uint32_t transitionCounter() const { return transition_counter_; }
  uint8_t faultBits() const;

 private:
  void setState(SafetyState state);
  uint16_t normalizeLease(uint16_t lease_ms) const;

  SafetyState state_ = SafetyState::Boot;
  SafetyState previous_state_ = SafetyState::Boot;
  SafetyInputs inputs_ = {};
  bool has_heartbeat_ = false;
  bool armed_ = false;
  bool fault_lockout_ = false;
  uint32_t last_heartbeat_ms_ = 0;
  uint32_t lease_until_ms_ = 0;
  uint32_t last_control_tx_ms_ = 0;
  uint32_t transition_counter_ = 0;
};

}  // namespace csm::board
