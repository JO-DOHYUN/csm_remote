#include "board/control/HostRealtimeAuthority.h"
#include "board/control_island/ControlSourceManager.h"
#include "protocol/RealtimeControl.h"
static uint32_t realtime_last_processed_ms=0,realtime_process_max_gap_ms=0;
static uint32_t realtime_processed_total=0,realtime_decode_reject_total=0;
static uint32_t host_control_state_request_total=0,realtime_state_reject_total=0;
static uint32_t realtime_reorder_reject_total=0,realtime_boot_reject_total=0;
static uint32_t realtime_contract_reject_total=0,realtime_epoch_reject_total=0;
static uint16_t realtime_proof_frame_sequence=0;
static uint64_t product_boot_session_id=17;
static uint64_t mono64_us(){return millis()*1000ULL;}
static csm::board::control::HostRealtimeAuthority host_realtime_authority;
static csm::board::control_island::ControlSourceManager control_source_manager;
struct ObservationMainTransport {
  csm::board::uplink::WifiRealtimeMailbox mailbox;
  bool takeRealtimeDatagram(csm::board::uplink::WifiRealtimeDatagram* d){return mailbox.takeLatestRx(d);}
  bool offerRealtimeProof(const uint8_t* b,uint16_t n,const csm::board::uplink::WifiRealtimeDatagram& d,uint32_t seq,uint32_t ms){return mailbox.stageProof(b,n,d,seq,ms);}
};
static ObservationMainTransport wifi_tcp_sink;
enum class HostControlCloseReason {FreshnessFault};
static void close_host_control_epoch(HostControlCloseReason,uint32_t){host_realtime_authority.disarm();}
#include "observation_main_body.inc"
