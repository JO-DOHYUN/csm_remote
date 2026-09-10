// Actual mailbox/worker/USB owners; only syscalls/outer board adapter are doubles.
#include <cassert>
#include <iostream>
#include <new>
#include <thread>
#include <Arduino.h>
#include <USB/PluggableUSBSerial.h>
#include "board/uplink/WifiRealtimeWorker.h"
#include "board/uplink/WifiControlPlaneMailbox.h"
#include "board/uplink/UsbCdcSink.h"
#include "board/uplink/CanonicalPublisher.h"
#include "board/uplink/WifiTcpSink.h"
#include "board/control_island/ControlPathDiagnostics.h"
#include "board/observability/DebugObservation.h"
#include "../shared/observability/generated/Observation.h"
#include "observation_main_fixture.h"
#include "observation_tcp_fixture.h"
namespace obs=csm::observation;
using namespace csm::board::uplink;
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
namespace trace=csm::board::observation;
struct Event {obs::Meta meta;uint16_t id;uint8_t body[144];};
static std::vector<Event> drain() {
  std::vector<Event> events;uint8_t bytes[523];
  while(const auto n=trace::nextFrame(bytes,sizeof(bytes))) {
    obs::View v{};assert(obs::decode(bytes,n,v));
    Event e{v.meta,v.event_id,{}};memcpy(e.body,v.body,v.body_length);events.push_back(e);
  }
  return events;
}
template<class T> static std::vector<T> of(const std::vector<Event>& events,uint8_t producer=0) {
  std::vector<T> values;
  for(const auto& e:events) if(e.id==T::id && (!producer||producer==e.meta.producer)) {
    T value{};assert(T::read({e.meta,e.id,e.body,T::body_size},value));values.push_back(value);
  }
  return values;
}
static void resetTrace() { trace::state().~State();new(&trace::state()) trace::State;
  trace::begin(17,[]()->uint32_t{return millis();}); }
#else
static void resetTrace() {}
#endif
namespace csm::board::uplink {
struct ObservationOwnerTest {
 static void sinkAdmission() {
  resetTrace();WifiTcpSink::TxStorage storage{};WifiTcpSink sink(storage);
  sink.enabled_=true;sink.connected_=true;sink.mailbox_.configureSession(17);sink.mailbox_.activateLiveSession();
  sink.worker_state_.rx_epoch_generation=sink.mailbox_.rxEpochGeneration();
  uint8_t bytes[43]{};PublishedFrameView frame{bytes,43,0,csm::RecordType::StreamSession,UplinkPriority::Critical,UplinkDeliveryClass::LatencyBounded};
  assert(sink.offer(frame)==SinkOfferResult::Accepted);
  sink.mailbox_.deactivateLiveSession();assert(sink.offer(frame)==SinkOfferResult::Disconnected);
  assert(sink.counters().offer_accept_total==1 && sink.counters().offer_disconnected_total==1);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  const auto st=of<obs::Stream>(drain(),1);assert(st.size()==1 && st[0].stage==6 && st[0].publish_sequence==0);
#endif
  std::cout<<"PRODUCT sink="<<sink.counters().offer_accept_total<<","<<sink.counters().offer_disconnected_total<<"\n";
 }
 static void observer() {
  resetTrace();WifiWorkerMailbox::TxStorage storage{};WifiWorkerMailbox wm(storage);
  WifiControlPlaneMailbox cm;WifiRealtimeMailbox rm;WifiSocketWorker w(wm,cm,rm);TCPSocket socket;
  wm.configureSession(17);wm.activateLiveSession();w.client_=&socket;w.state_.connected=true;
  w.config_.batch_target_bytes=0;w.config_.max_writes_per_pump=1;
  struct Sink: IFrameSink {
    WifiWorkerMailbox& m;explicit Sink(WifiWorkerMailbox& x):m(x){}
    bool enabled() const override{return true;}bool connected() const override{return true;}
    SinkOfferResult offer(const PublishedFrameView& f) override {
      return m.tryOffer(f,millis())==WifiMailboxOfferResult::Accepted?SinkOfferResult::Accepted:SinkOfferResult::Overflow;
    }
  } sink(wm);
  CanonicalPublisher pub;pub.begin(17,&sink);assert(pub.service(1).record_published);
  w.session_anchor_required_=true;socket.send_results.push_back(5);w.serviceSessionAnchor(millis());
  assert(w.session_anchor_required_ && w.state_.counters.frame_sent_total==0);
  w.serviceSessionAnchor(millis());assert(!w.session_anchor_required_ && w.state_.counters.frame_sent_total==1);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  auto st=of<obs::Stream>(drain(),3);assert(st.size()==4 && st[0].publish_sequence==0 && st[3].byte_start==5);
  for(unsigned i=0;i<100;++i)trace::emit(1,0,1,obs::Capacity{});
  (void)drain(); // debug overflow and drain must not allocate canonical publication IDs
#endif
  uint8_t p[2]{1,2};assert(pub.enqueueRecord(csm::RecordType::ControlAck,p,2,UplinkPriority::Normal));
  assert(pub.service(2).publish_seq==1 && pub.nextPublishSeq()==2);
  socket.send_results.push_back(5);w.serviceTransmit(millis());
  assert(wm.queueSnapshot().unsent_bytes==8 && w.state_.counters.frame_sent_total==1);
  socket.send_results.push_back(NSAPI_ERROR_WOULD_BLOCK);w.serviceTransmit(millis());
  assert(wm.queueSnapshot().unsent_bytes==8);
  w.serviceTransmit(millis());assert(wm.queueSnapshot().unsent_bytes==0 && w.state_.counters.frame_sent_total==2);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  st=of<obs::Stream>(drain(),3);assert(st.size()==6 && st[0].byte_start==43 && st[3].result==NSAPI_ERROR_WOULD_BLOCK);
  assert(st[4].byte_start==48 && st[5].result==8);
#endif
  socket.incoming.push_back({-45,{}});w.serviceReceive(millis());
  assert(w.client_==nullptr && w.state_.last_close_reason==WifiCloseReason::SocketError);
  std::cout<<"PRODUCT observer="<<pub.nextPublishSeq()<<","<<w.state_.counters.frame_sent_total<<","<<w.state_.counters.bytes_sent_total<<","<<socket.closes<<"\n";
 }
 static void tcp() {
  resetTrace();WifiWorkerMailbox::TxStorage storage{};WifiWorkerMailbox wm(storage);WifiControlPlaneMailbox cm;WifiRealtimeMailbox rm;
  WifiSocketWorker w(wm,cm,rm);TCPSocket socket;
  cm.configure(17);assert(cm.activate(1));w.control_client_=&socket;
  uint8_t ack[csm::kControlAckPayloadLen]{};csm::wr_u32_le(ack+8,99);assert(cm.offerAck(ack,sizeof(ack)));
  socket.send_results.push_back(5);w.serviceControlTransmit(millis());
  assert(cm.evidence().ack_sent_id==0 && w.control_tx_offset_==5);
  socket.send_results.push_back(NSAPI_ERROR_WOULD_BLOCK);w.serviceControlTransmit(millis());assert(w.control_tx_offset_==5);
  socket.send_results.push_back(34);w.serviceControlTransmit(millis());assert(cm.evidence().ack_sent_id==99 && cm.queuedRecords()==0);
  socket.incoming.push_back({3,{4,5,6}});w.serviceControlReceive();assert(cm.available()==3 && cm.read()==4);
  socket.incoming.push_back({0,{}});w.serviceControlReceive();assert(w.control_client_==nullptr && socket.closes==1);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  auto events=drain();const auto st=of<obs::Stream>(events,3);assert(st.size()==11);
  assert(st[0].stage==1 && st[0].byte_start==0 && st[1].result==5);
  assert(st[2].byte_start==5 && st[3].result==NSAPI_ERROR_WOULD_BLOCK);
  assert(st[4].byte_start==5 && st[5].result==34);
  assert(st.back().stage==10);
  const auto tx=of<obs::Transaction>(events,3);assert(tx.size()==3 && tx[2].stage==2 && tx[2].command_id==99);
  const auto calls=of<obs::Resource>(events,3);assert(calls.size()==10);
  assert(calls[0].stage==1 && calls[1].stage==2 && calls[0].operation_id==calls[1].operation_id && calls[1].result==5);
#endif
  assert(cm.activate(2));w.control_client_=&socket;assert(cm.offerAck(ack,sizeof(ack)));
  socket.send_results.push_back(-44);w.serviceControlTransmit(millis());assert(w.control_client_==nullptr && cm.evidence().close_reason==4);
  std::cout<<"PRODUCT tcp="<<cm.evidence().ack_sent_id<<","<<socket.closes<<","<<cm.evidence().close_reason<<"\n";
 }
 static void run() {
  resetTrace();WifiRealtimeMailbox m; m.reset();
  WifiRealtimeWorker w(m);NetworkInterface net;
  assert(w.start(&net,3335,3));assert(w.openSocket());
  w.socket_.incoming.push_back({2,{0xaa,0xbb}});
  w.socket_.incoming.push_back({2,{0xcc,0xdd}});
  w.serviceReceive();
  WifiRealtimeDatagram d;assert(m.takeLatestRx(&d));assert(d.token==2 && d.bytes[0]==0xcc);
  assert(m.evidence().rx_overwrite==1);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  auto events=drain();auto io=of<obs::DatagramIo>(events,2);
  assert(io.size()==8 && io[0].stage==1 && io[1].stage==2 && io[2].rx_token==1);
  assert(io[3].operation_id==2 && io[5].rx_token==2 && io.back().result==NSAPI_ERROR_WOULD_BLOCK);
  auto a=of<obs::Admission>(events,2);assert(a.size()==3);
  assert(a[0].stage==3 && a[1].stage==4 && a[1].rx_token==1 && a[2].rx_token==2);
  assert(of<obs::Admission>(events,1).back().stage==5);
  const auto raw=of<obs::DatagramBytes>(events,2);assert(raw.size()==2);
  assert(raw[0].operation_id==1 && raw[0].captured_length==2 && raw[0].data[0]==0xaa && raw[0].data[127]==0);
#endif
  m.rx_guard_.test_and_set();assert(!m.publishRx(d.bytes,d.length,2,d.peer,nullptr));m.rx_guard_.clear();
  assert(m.evidence().rx_overwrite==2);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  a=of<obs::Admission>(drain(),2);assert(a.size()==1 && a[0].stage==7 && a[0].reason==1);
#endif
  uint8_t proof[kRealtimeProofFrameBytes]{};
  assert(csm::encode_realtime_proof_v1(proof,sizeof(proof),0,1,17,0,7,2,0,0,0,0)==sizeof(proof));
  assert(m.stageProof(proof,sizeof(proof),d,7,2));
  m.proof_guard_.test_and_set();assert(!m.stageProof(proof,sizeof(proof),d,7,2));m.proof_guard_.clear();
  assert(m.stageProof(proof,sizeof(proof),d,7,2));
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  auto staged=drain();const auto ps=of<obs::Proof>(staged,1);
  assert(ps.size()==3 && ps[0].stage==3 && ps[1].stage==4 && ps[2].stage==3);
  assert(of<obs::Admission>(staged,1).back().reason==2);
#endif
  w.socket_.results.push_back(NSAPI_ERROR_WOULD_BLOCK);w.serviceProof();
  WifiRealtimeProof pending;assert(m.peekProof(&pending));
  w.socket_.results.push_back(5);w.serviceProof(); // Partial UDP acceptance is NOT successful completion.
  assert(!m.peekProof(&pending) && !m.evidence().socket_ready && w.socket_.closed==1);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  io=of<obs::DatagramIo>(drain(),2);assert(io.back().stage==10 && io.back().result==5);
#endif
  assert(w.openSocket());assert(m.stageProof(proof,sizeof(proof),d,7,3));
  const auto attempts=w.socket_.sends.size();w.serviceProof();assert(w.socket_.sends.size()==attempts);
  w.socket_.incoming.push_back({2,{1,2}});w.serviceReceive();assert(m.takeLatestRx(&d));
  assert(m.stageProof(proof,sizeof(proof),d,7,4));w.serviceProof();assert(!m.peekProof(&pending));
  w.socket_.incoming.push_back({-55,{}});w.serviceReceive();assert(!m.evidence().socket_ready);
  std::cout<<"PRODUCT udp="<<m.evidence().rx_datagrams<<","<<m.evidence().rx_overwrite<<","<<w.socket_.sends.size()<<","<<w.socket_.closed<<"\n";
 }
};
}
static std::vector<uint8_t> statePacket(uint32_t seq,uint32_t ref,uint8_t mode=0,uint32_t epoch=0) {
  uint8_t p[csm::kHostRealtimeStateV1PayloadLen]{};
  p[0]=1;p[1]=mode;csm::wr_u64_le(p+4,17);csm::wr_u32_le(p+12,epoch);
  csm::wr_u32_le(p+16,seq);csm::wr_u32_le(p+20,seq);csm::wr_u32_le(p+24,ref);
  csm::wr_u32_le(p+32,csm::kHostControlStateContractId);p[36]=7;
  p[40]=0xaa;p[48]=0x82;p[56]=0x12;
  uint8_t frame[75]{};size_t n=0;
  assert(csm::encode_typed_frame(frame,sizeof(frame),csm::RecordType::HostRealtimeStateV1,p,64,0,0,&n));
  return {frame,frame+n};
}
static void mainOwner() {
  resetTrace();assert(host_realtime_authority.begin(17,350));control_source_manager.begin(17);
  wifi_tcp_sink.mailbox.reset();WifiRealtimePeer peer{};strcpy(peer.address,"192.0.2.1");peer.port=3335;
  auto send=[&](const std::vector<uint8_t>& b){assert(wifi_tcp_sink.mailbox.publishRx(b.data(),uint16_t(b.size()),millis(),peer,nullptr));service_host_realtime();};
  send(statePacket(1,0));send(statePacket(2,host_realtime_authority.proofSequence()));
  uint8_t reason=0;assert(host_realtime_authority.arm(1,millis(),true,true,&reason));
  send(statePacket(3,host_realtime_authority.proofSequence(),1,1));
  assert(control_source_manager.host().valid && host_control_state_request_total==1);
  send({1,2});assert(realtime_decode_reject_total==1);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  auto e=drain();auto a=of<obs::Admission>(e,1);bool applied=false,reject=false;
  for(const auto& v:a){applied|=v.stage==8 && v.wire_sequence==3;reject|=v.stage==7 && v.valid_fields==16;}
  assert(applied && reject && !of<obs::Proof>(e,1).empty());
#endif
  std::cout<<"PRODUCT main="<<realtime_processed_total<<","<<host_control_state_request_total<<","<<realtime_decode_reject_total<<","<<host_realtime_authority.lastAppliedGeneration()<<"\n";
}
static void usbOwner() {
  resetTrace();_SerialUSB={};UsbCdcSink sink;UsbCdcSinkConfig config{};config.max_writes_per_pump=2;config.max_bytes_per_pump=512;sink.begin(config);
  uint8_t bytes[13]{};size_t n=0;uint8_t p[2]{1,2};
  assert(csm::encode_typed_frame(bytes,sizeof(bytes),csm::RecordType::ControlAck,p,2,44,0,&n));
  PublishedFrameView f{bytes,uint16_t(n),44,csm::RecordType::ControlAck,UplinkPriority::Normal,UplinkDeliveryClass::Batchable};
  assert(sink.offer(f)==SinkOfferResult::Accepted);
  CSM_OBS(trace::emit(1,0,1,obs::Capacity{1,1,1,0,0}));
  _SerialUSB.write_limit=5;sink.service(512,1,1000);
  assert(_SerialUSB.bytes.size()==5 && sink.counters().frame_sent_total==0);
  _SerialUSB.write_limit=0xffffffffu;sink.service(512,1,1000);
  assert(sink.counters().frame_sent_total==1 && sink.counters().last_sent_publish_seq==44);
  assert(memcmp(_SerialUSB.bytes.data(),bytes,n)==0);
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  assert(_SerialUSB.bytes.size()>n);obs::View view{};
  assert(obs::decode(_SerialUSB.bytes.data()+n,_SerialUSB.bytes.size()-n,view));
  trace::emit(1,0,1,obs::Capacity{1,1,1,0,0});
  _SerialUSB.bytes.clear();_SerialUSB.write_limit=5;sink.service(512,1,1000);
  assert(sink.offer(f)==SinkOfferResult::Accepted);
  _SerialUSB.write_limit=0xffffffffu;sink.service(512,1,1000);
  const size_t debugLength=83+obs::Capacity::body_size;
  assert(obs::decode(_SerialUSB.bytes.data(),debugLength,view));
  assert(memcmp(_SerialUSB.bytes.data()+debugLength,bytes,n)==0);
  trace::emit(1,0,1,obs::Capacity{1,1,1,0,0});_SerialUSB.write_limit=1;sink.service(512,1,1000);
  _SerialUSB.online=false;sink.service(512,1,1000);assert(trace::state().usb_aborted==1);
#else
  assert(_SerialUSB.bytes.size()==n);
#endif
  std::cout<<"PASS: actual USB partial-frame separation/canonical accounting\n";
}
static void usbDebugStall() {
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  resetTrace();_SerialUSB={};UsbCdcSink sink;UsbCdcSinkConfig config{};
  config.max_writes_per_pump=2;config.max_bytes_per_pump=512;sink.begin(config);
  trace::emit(1,0,1,obs::Capacity{1,1,1,0,0});
  _SerialUSB.write_limit=5;sink.service(512,10,1000);
  assert(sink.counters().write_attempt_total==1 && sink.counters().partial_write_total==1);
  assert(sink.backpressureActive());
  _SerialUSB.write_limit=0;sink.service(512,11,1000);
  assert(sink.counters().zero_write_total==1);
  // The existing USB stall contract must also reconcile an unfinished debug frame.
  const auto closed=sink.service(512,10+config.stall_timeout_ms,1000);
  assert(closed.epoch_changed && trace::state().usb_aborted==1 && !sink.backpressureActive());
  assert(sink.counters().frame_sent_total==0); // Debug admission is never canonical success.
  _SerialUSB.write_limit=0xffffffffu;uint8_t bytes[13]{};uint8_t p[2]{1,2};size_t n=0;
  assert(csm::encode_typed_frame(bytes,sizeof(bytes),csm::RecordType::ControlAck,p,2,44,0,&n));
  PublishedFrameView f{bytes,uint16_t(n),44,csm::RecordType::ControlAck,UplinkPriority::Normal,UplinkDeliveryClass::Batchable};
  assert(sink.offer(f)==SinkOfferResult::Accepted);sink.service(512,300,1000);
  assert(sink.counters().frame_sent_total==1 && sink.counters().last_sent_publish_seq==44);
  std::cout<<"PASS: actual USB debug zero/partial/stall reconciles without canonical success inflation\n";
#endif
}
static void retainedFirst() {
  resetTrace();WifiControlPlaneMailbox m;m.configure(17);assert(m.activate(1));
  uint8_t p[csm::kControlAckPayloadLen]{};csm::wr_u32_le(p+8,99);assert(m.offerAck(p,sizeof(p)));
  m.noteSend(3,1,1,99,3,false);assert(m.evidence().ack_sent_id==0);
  m.noteSend(36,2,1,99,39,true);assert(m.evidence().ack_sent_id==99);
  m.noteClose(6,-1,3);m.deactivate();assert(m.activate(4));m.noteClose(1,0,5);assert(m.evidence().first[0]==6);
  csm::board::control_island::ControlPathFirstFailure latch{};uint32_t context[24]{};
  assert(latch.record(7,1,context));assert(!latch.record(8,2,context));assert(latch.reason==7);
  std::cout<<"PRODUCT ack="<<m.evidence().ack_sent_id<<","<<m.evidence().first[0]<<","<<latch.reason<<"\n";
}
static void capacityAndWrap() {
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
  resetTrace();auto& r=trace::state().rings[1];r.sequence=65534;
  trace::capacity(1);assert(of<obs::Capacity>(drain(),1).size()==4);
  std::cout<<"DEBUG storage="<<sizeof(trace::State)<<",slot="<<sizeof(trace::Slot)<<"\n";
  for(unsigned i=0;i<40;++i)trace::emit(2,1,1,obs::Capacity{1,0,0,0,0});
  assert(r.dropped==8 && r.high_water==32);auto events=drain();assert(events.size()==32);
  assert(events[0].meta.trace_sequence==65535 && events[2].meta.trace_sequence==65537);
  trace::emit(2,1,1,obs::Capacity{1,0,0,0,0});auto loss=of<obs::TraceLoss>(drain());
  assert(loss.size()==1 && loss[0].dropped==8 && loss[0].last_missing_sequence-loss[0].first_missing_sequence==7);
  r.sequence=0xfffffffeu;trace::emit(2,1,1,obs::Capacity{});trace::emit(2,1,1,obs::Capacity{});
  events=drain();assert(events[0].meta.trace_sequence==0xffffffffu && events[1].meta.trace_sequence==0);
  resetTrace();std::atomic<bool> done{false};
  std::thread producer([&]{for(unsigned i=0;i<10000;++i)trace::emit(2,1,i,obs::Capacity{1,i,i,0,0});done=true;});
  do { (void)drain(); } while(!done);producer.join();(void)drain();
  std::cout<<"PASS: fixed ring overflow/ranges/u32 wrap/concurrent SPSC\n";
#else
  unsigned sideEffect=0;CSM_OBS(++sideEffect);assert(sideEffect==0);
  std::cout<<"PASS: OFF argument evaluation absent\n";
#endif
}
int main(){ObservationOwnerTest::run();ObservationOwnerTest::tcp();ObservationOwnerTest::observer();ObservationOwnerTest::sinkAdmission();mainOwner();usbOwner();usbDebugStall();retainedFirst();capacityAndWrap();
 std::cout<<"PASS: observation owner suite\n";}
