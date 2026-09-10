#include "../shared/observability/generated/Observation.h"
#include "protocol/TypedFrame.h"
#include <assert.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace obs = csm::observation;
template<class E> void roundtrip(const obs::View& view,const std::vector<uint8_t>& input) {
  E event{};
  assert(E::read(view,event));
  uint8_t out[523]{};
  const size_t count=obs::encode(view.meta,event,out,sizeof(out));
  assert(count==input.size() && memcmp(out,input.data(),count)==0);
  // Compare the outer format to the real existing product encoder.
  uint8_t product[523]{}; size_t written=0;
  assert(csm::encode_typed_frame(product,sizeof(product),static_cast<csm::RecordType>(obs::kRecordType),
      out+9,uint16_t(count-11),uint16_t(view.meta.trace_sequence),0,&written));
  assert(written==count && memcmp(out,product,count)==0);
}
int main(int argc,char** argv) {
  assert(argc==2); std::ifstream stream(argv[1]); assert(stream.good());
  unsigned tested=0; std::string line;
  while(std::getline(stream,line)) {
    if(line.empty())continue;
    const auto split=line.find('\t');assert(split!=std::string::npos);
    const int id=std::stoi(line.substr(0,split));
    std::vector<uint8_t> raw;
    for(size_t i=split+1;i+1<line.size();i+=2)raw.push_back(uint8_t(std::stoul(line.substr(i,2),nullptr,16)));
    obs::View view{};assert(obs::decode(raw.data(),raw.size(),view));assert(view.event_id==id);
    assert(view.meta.boot_id==0x1122334455667788ULL && view.meta.trace_sequence==0xffffffffu);
    switch(id) {
      case 1:roundtrip<obs::Intent>(view,raw);break;
      case 2:roundtrip<obs::DatagramIo>(view,raw);break;
      case 3:roundtrip<obs::DatagramBytes>(view,raw);break;
      case 4:roundtrip<obs::Admission>(view,raw);break;
      case 5:roundtrip<obs::Proof>(view,raw);break;
      case 6:roundtrip<obs::Transaction>(view,raw);break;
      case 7:roundtrip<obs::Stream>(view,raw);break;
      case 8:roundtrip<obs::Resource>(view,raw);break;
      case 9:roundtrip<obs::Capacity>(view,raw);break;
      case 10:roundtrip<obs::TraceLoss>(view,raw);break;
      case 11:roundtrip<obs::IntentState>(view,raw);break;
      default:assert(false);
    }
    for(const size_t offset : {size_t(3),size_t(4),size_t(5),size_t(9),size_t(10),size_t(11),size_t(12),size_t(13),size_t(45),size_t(47),size_t(49)}) {
      auto broken=raw;broken[offset]^=0x80;
      assert(!obs::decode(broken.data(),broken.size(),view));
      obs::put(broken.data()+broken.size()-2,obs::crc16(broken.data()+2,broken.size()-4),2);
      assert(!obs::decode(broken.data(),broken.size(),view));
    }
    assert(!obs::decode(raw.data(),raw.size()-1,view));
    ++tested;
  }
  assert(tested==11);
  for(uint32_t value : {65535u,65536u,65537u,0xffffffffu,0u}) {
    obs::Meta meta{16,3,value,0x100000001ULL,0,0};obs::TraceLoss loss{};loss.dropped=value;
    uint8_t bytes[523]{};obs::View view{};
    const auto n=obs::encode(meta,loss,bytes,sizeof(bytes));assert(n);
    assert(obs::decode(bytes,n,view));obs::TraceLoss decoded{};
    assert(view.meta.epoch==0x100000001ULL);
    assert(obs::TraceLoss::read(view,decoded) && decoded.dropped==value);
    assert(!obs::encode(meta,loss,bytes,1));
  }
  std::cout<<"PASS: 11 canonical C++ vectors; real TypedFrame equivalence; negative formats; counter/wrap\n";
}
