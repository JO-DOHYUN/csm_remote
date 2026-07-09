#pragma once

#include <stdint.h>

#include "board/authority/AuthorityTypes.h"
#include "board/can/CanTypes.h"

namespace csm::board::authority {

class AutonomyAuthorityMonitor {
 public:
  void begin(uint32_t now_ms);

  bool configure(const AutonomyCommandProfile& profile);
  void clearProfile();

  void observeFrame(uint32_t now_ms,
                    const can::CanFrame8& frame,
                    bool local_authority_active = false);
  void update(uint32_t now_ms);

  void latchLocalTxInhibit(uint16_t detail);
  void clearLocalTxInhibitForService();

  AutonomyAuthorityState state() const { return state_; }
  bool profileConfigured() const { return profile_configured_; }
  bool quietWindowSatisfied() const { return quiet_window_satisfied_; }
  bool localTxInhibitLatched() const { return local_tx_inhibit_latched_; }
  uint32_t lastUpstreamCommandMs() const { return last_upstream_command_ms_; }
  uint16_t ambiguousCount() const { return ambiguous_count_; }
  uint16_t protocolFaultCount() const { return protocol_fault_count_; }
  uint16_t inhibitDetail() const { return inhibit_detail_; }

 private:
  static bool isValidProfile(const AutonomyCommandProfile& profile);
  bool matchesProfile(const can::CanFrame8& frame);
  void setState(AutonomyAuthorityState state);
  void noteProtocolFault(uint16_t detail);

  AutonomyCommandProfile profile_ = {};
  bool profile_configured_ = false;
  AutonomyAuthorityState state_ = AutonomyAuthorityState::Unknown;
  bool quiet_window_satisfied_ = false;
  bool local_tx_inhibit_latched_ = false;
  uint32_t last_upstream_command_ms_ = 0;
  uint16_t ambiguous_count_ = 0;
  uint16_t protocol_fault_count_ = 0;
  uint16_t inhibit_detail_ = 0;
};

}  // namespace csm::board::authority

