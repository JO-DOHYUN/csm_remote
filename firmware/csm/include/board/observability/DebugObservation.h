#pragma once
// DEBUG_TRACE removal boundary. OFF removes argument evaluation and all storage.
#ifndef BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
#define BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY 0
#endif
#if BOARD_ENABLE_SERVICE_HIL_OBSERVABILITY
#include <atomic>
#include "../../../shared/observability/generated/Observation.h"
#include "protocol/TypedRecords.h"
namespace csm::board::observation {
namespace wire = csm::observation;
using Clock = uint32_t (*)();
struct Slot { wire::Meta meta{}; uint16_t id=0; uint8_t body[144]{}; };
// Memory ceiling only, NOT a qualified drain-gap/throughput guarantee.
constexpr uint32_t kSlots = 32;
static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2,
              "debug indices must not introduce library locks");
struct Ring {
  Slot slots[kSlots];
  std::atomic<uint32_t> head{0}, tail{0}, dropped{0}, high_water{0};
  uint32_t sequence=0, first_lost=0, last_lost=0, pending_loss=0;
  // Exactly one existing execution context writes each ring; USB foreground reads.
  template<class E> void append(uint8_t producer, uint64_t boot, uint64_t epoch,
                                uint32_t ticks, const E& event) {
    static_assert(E::body_size<=sizeof(Slot::body), "P1 body storage exceeded");
    const uint32_t seq=++sequence;
    const uint32_t t=tail.load(std::memory_order_relaxed);
    const uint32_t h=head.load(std::memory_order_acquire);
    if(t-h>=kSlots) {
      if(pending_loss==0) first_lost=seq;
      last_lost=seq; ++pending_loss;
      dropped.store(dropped.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);
      return;
    }
    Slot& slot=slots[t%kSlots];
    slot.meta={producer,1,seq,epoch,boot,ticks}; slot.id=E::id;
    event.write(slot.body);
    tail.store(t+1,std::memory_order_release);
    if(t+1-h>high_water.load(std::memory_order_relaxed))
      high_water.store(t+1-h,std::memory_order_relaxed);
  }
  template<class E> void emit(uint8_t producer,uint64_t boot,uint64_t epoch,
                              uint32_t ticks,const E& event) {
    if(pending_loss && tail.load(std::memory_order_relaxed)-head.load(std::memory_order_acquire)<kSlots) {
      const wire::TraceLoss loss{first_lost,last_lost,pending_loss,1,
                                high_water.load(std::memory_order_relaxed)};
      pending_loss=0;
      append(producer,boot,epoch,ticks,loss);
    }
    append(producer,boot,epoch,ticks,event);
  }
};
struct State { Ring rings[3]; uint64_t boot=0; Clock clock=nullptr; uint8_t drain_next=0;
  uint32_t usb_aborted=0; };
static_assert(sizeof(State)<24576,"debug storage ceiling exceeded");
inline State& state() { static State value; return value; }
inline uint32_t now() { return state().clock ? state().clock() : 0; }
// Startup-only, before either Wi-Fi worker starts; never reset on DISARM/reconnect.
inline void begin(uint64_t boot,Clock clock=nullptr) { state().boot=boot;state().clock=clock; }
template<class E> inline void emit(uint8_t producer,uint64_t epoch,uint32_t ticks,const E& event) {
  if(producer<1 || producer>3 || !wire::owns(E::id,producer)) return;
  state().rings[producer-1].emit(producer,state().boot,epoch,ticks,event);
}
template<class E> inline size_t encodeSlot(const Slot& slot,uint8_t* bytes,size_t capacity) {
  E event{};
  const wire::View view{slot.meta,slot.id,slot.body,E::body_size};
  return E::read(view,event) ? wire::encode(slot.meta,event,bytes,capacity) : 0;
}
inline size_t nextFrame(uint8_t* bytes,size_t capacity) {
  for(unsigned i=0;i<3;++i) {
    const uint8_t index=state().drain_next++%3;
    Ring& ring=state().rings[index];
    const uint32_t h=ring.head.load(std::memory_order_relaxed);
    if(h==ring.tail.load(std::memory_order_acquire)) continue;
    const Slot& slot=ring.slots[h%kSlots]; size_t n=0;
#define OBS_ENCODE(Name) case wire::Name::id: n=encodeSlot<wire::Name>(slot,bytes,capacity);break
    switch(slot.id) {
      OBS_ENCODE(Intent); OBS_ENCODE(DatagramIo); OBS_ENCODE(DatagramBytes);
      OBS_ENCODE(Admission); OBS_ENCODE(Proof); OBS_ENCODE(Transaction);
      OBS_ENCODE(Stream); OBS_ENCODE(Resource); OBS_ENCODE(Capacity); OBS_ENCODE(TraceLoss);
      default: break;
    }
#undef OBS_ENCODE
    if(n) ring.head.store(h+1,std::memory_order_release);
    return n;
  }
  return 0;
}
inline void capacity(uint32_t ticks) {
  for(unsigned i=0;i<3;++i) {
    const Ring& r=state().rings[i];
    const uint32_t head=r.head.load(); // this same foreground owns consumption
    emit(1,0,ticks,wire::Capacity{100+i,r.tail.load()-head,
      r.high_water.load(),r.dropped.load(),0});
  }
  emit(1,0,ticks,wire::Capacity{104,0,0,state().usb_aborted,0});
}
inline void admission(uint8_t p,uint64_t epoch,uint8_t stage,uint32_t reason,
                      uint32_t token,uint32_t seq=0,uint32_t generation=0,
                      uint32_t authority=0,uint32_t proof=0,uint32_t valid=16) {
  emit(p,epoch,now(),wire::Admission{stage,reason,seq,generation,authority,proof,
       token,0,0,0,0,valid});
}
inline void stream(uint8_t p,uint64_t epoch,uint8_t stage,uint16_t channel,
                   uint32_t operation,uint64_t sequence,uint64_t offset,
                   uint32_t length,int32_t result,uint16_t type,uint32_t reason=0) {
  emit(p,epoch,now(),wire::Stream{stage,channel,operation,sequence,offset,length,result,type,reason});
}
inline void transaction(uint8_t p,uint64_t epoch,uint8_t stage,uint32_t command,
                        uint32_t transaction_id,uint16_t type,int32_t result,uint32_t reason=0) {
  emit(p,epoch,now(),wire::Transaction{stage,command,transaction_id,type,result,0,0,reason});
}
inline uint32_t ipv4(const char* s) {
  if(!s) return 0;
  uint32_t out=0,part=0,parts=0;
  for(unsigned i=0;i<16;++i) {
    const char c=s[i];
    if(c>='0' && c<='9') { part=part*10+uint32_t(c-'0');if(part>255)return 0; }
    else if(c=='.' || c==0) {out=(out<<8)|part;part=0;++parts;if(c==0)return parts==4?out:0;}
    else return 0;
  }
  return 0;
}
inline void datagram(uint64_t epoch,uint8_t stage,uint32_t operation,uint32_t token,
                     uint16_t length,int32_t result,const char* peer,uint16_t port,
                     const uint8_t* bytes=nullptr,uint16_t captured=0) {
  emit(2,epoch,now(),wire::DatagramIo{stage,operation,0,0,0,0,token,length,result,ipv4(peer),port});
  if(bytes && captured) {
    wire::DatagramBytes raw{};raw.operation_id=operation;raw.rx_token=token;
    raw.wire_length=length;raw.captured_length=captured>128?128:captured;
    memcpy(raw.data,bytes,raw.captured_length); emit(2,epoch,now(),raw);
  }
}
inline void proof(uint8_t p,uint64_t epoch,uint8_t stage,uint32_t token,const uint8_t* bytes) {
  const uint8_t* b=bytes+9;
  emit(p,epoch,now(),wire::Proof{stage,csm::rd_u32_le(b+csm::kRealtimeProofSequenceOffset),
    csm::rd_u32_le(b+csm::kRealtimeProofHighestRxSequenceOffset),
    csm::rd_u32_le(b+csm::kRealtimeProofStateGenerationOffset),
    csm::rd_u32_le(b+csm::kRealtimeProofAuthorityEpochOffset),token,
    b[csm::kRealtimeProofStatusOffset],b[csm::kRealtimeProofReasonOffset],
    csm::rd_u16_le(b+csm::kRealtimeProofFlagsOffset)});
}
}
#define CSM_OBS(...) do { __VA_ARGS__; } while(0)
#else
#define CSM_OBS(...) do {} while(0)
#endif
