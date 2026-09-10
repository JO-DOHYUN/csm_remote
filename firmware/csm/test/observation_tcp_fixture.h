#include "board/uplink/WifiSocketWorker.h"
// These are the outer clock/call persistence/network teardown collaborators only.
// The real class and its current RX/TX/progress/close method bodies are compiled below.
namespace csm::board::uplink {
WifiSocketWorker::WifiSocketWorker(WifiWorkerMailbox& m,WifiControlPlaneMailbox& c,WifiRealtimeMailbox& r)
 :mailbox_(m),control_mailbox_(c),realtime_mailbox_(r),realtime_worker_(r) {}
void WifiSocketWorker::publishState(uint32_t,bool) {} // outer summary publication, NOT claimed tested
bool WifiSocketWorker::start(const WifiTcpSinkConfig&) {return false;} // not exercised
void WifiSocketWorker::signalWake(uint32_t) {}
void WifiSocketWorker::closeSocket(TCPSocket*& s,WifiWorkerCallPhase) {if(s)s->close();s=nullptr;}
#include "observation_tcp_body.inc"
}
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
extern "C" void csm_lwip_socket_arena_snapshot(uint32_t*,uint32_t*,uint32_t*,uint32_t*) {}
#endif
