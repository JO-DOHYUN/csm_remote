// Generated from the CSM observation contract. DEBUG_TRACE codec; no runtime hook.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace csm { namespace observation {
namespace stage {
static constexpr uint32_t begin = 1u;
static constexpr uint32_t complete = 2u;
static constexpr uint32_t staged = 3u;
static constexpr uint32_t replaced = 4u;
static constexpr uint32_t taken = 5u;
static constexpr uint32_t accepted = 6u;
static constexpr uint32_t rejected = 7u;
static constexpr uint32_t applied = 8u;
static constexpr uint32_t canceled = 9u;
static constexpr uint32_t closed = 10u;
static constexpr uint32_t lost = 11u;
static constexpr uint32_t snapshot = 12u;
}
namespace producer {
static constexpr uint32_t m7_foreground = 1u;
static constexpr uint32_t m7_realtime_worker = 2u;
static constexpr uint32_t m7_socket_worker = 3u;
static constexpr uint32_t app_intent = 16u;
static constexpr uint32_t app_realtime_plane = 17u;
static constexpr uint32_t app_udp_send = 18u;
static constexpr uint32_t app_udp_receive = 19u;
static constexpr uint32_t app_control_send = 20u;
static constexpr uint32_t app_control_receive = 21u;
static constexpr uint32_t app_observer_receive = 22u;
static constexpr uint32_t app_truth_worker = 23u;
static constexpr uint32_t app_network_lifecycle = 24u;
static constexpr uint32_t app_capture_worker = 25u;
static constexpr uint32_t app_control_plane = 26u;
}
namespace clock {
static constexpr uint32_t m7_millis_u32 = 1u;
static constexpr uint32_t android_elapsed_realtime_nanos = 2u;
static constexpr uint32_t android_system_nano_time = 3u;
}
namespace admission_field {
static constexpr uint32_t wire_sequence = 1u;
static constexpr uint32_t generation = 2u;
static constexpr uint32_t authority_epoch = 4u;
static constexpr uint32_t proof_reference = 8u;
static constexpr uint32_t rx_token = 16u;
static constexpr uint32_t source_epoch = 32u;
static constexpr uint32_t activation_epoch = 64u;
static constexpr uint32_t forward_age_ms = 128u;
static constexpr uint32_t proof_age_ms = 256u;
}
static constexpr uint8_t kRecordType = 30;
static constexpr size_t kHeaderBytes = 72;
static constexpr const char* kSchemaHash = "b2cf35a8531d16a4fd1856101f7e02c7d110bf689bce403bd00c038ac083c34b";
static constexpr uint8_t kHash[32] = {0xb2,0xcf,0x35,0xa8,0x53,0x1d,0x16,0xa4,0xfd,0x18,0x56,0x10,0x1f,0x7e,0x02,0xc7,0xd1,0x10,0xbf,0x68,0x9b,0xce,0x40,0x3b,0xd0,0x0c,0x03,0x8a,0xc0,0x83,0xc3,0x4b};
struct Meta { uint8_t producer, clock; uint32_t trace_sequence; uint64_t epoch, boot_id, ticks; };
struct View { Meta meta; uint16_t event_id; const uint8_t* body; uint16_t body_length; };
inline uint64_t get(const uint8_t* p, size_t n) {
  uint64_t v=0; for(size_t i=0;i<n;++i) v |= uint64_t(p[i])<<(8*i); return v;
}
inline void put(uint8_t* p,uint64_t v,size_t n) {
  for(size_t i=0;i<n;++i) p[i]=uint8_t(v>>(8*i));
}
inline int32_t signed32(uint32_t v) {
  return v <= 0x7fffffffu ? int32_t(v) : int32_t(int64_t(v)-4294967296LL);
}
inline uint16_t crc16(const uint8_t* p,size_t n) {
  uint16_t c=0xffff;
  for(size_t i=0;i<n;++i) {
    c^=uint16_t(p[i])<<8;
    for(unsigned b=0;b<8;++b) c=uint16_t((c&0x8000)?(c<<1)^0x1021:c<<1);
  }
  return c;
}
inline int body_size(uint16_t id) { switch(id) {
    case 1: return 21;
    case 2: return 37;
    case 3: return 144;
    case 4: return 45;
    case 5: return 33;
    case 6: return 27;
    case 7: return 37;
    case 8: return 27;
    case 9: return 24;
    case 10: return 18;
    case 11: return 50;
    default: return -1;
} }
inline bool owns(uint16_t id,uint8_t p) { switch(id) {
    case 1: return p == 16;
    case 2: return p == 2 || p == 18 || p == 19;
    case 3: return p == 2 || p == 18 || p == 19;
    case 4: return p == 1 || p == 2 || p == 17 || p == 23;
    case 5: return p == 1 || p == 2 || p == 17 || p == 19;
    case 6: return p == 1 || p == 3 || p == 20 || p == 21 || p == 16 || p == 26;
    case 7: return p == 1 || p == 3 || p == 20 || p == 21 || p == 22 || p == 23 || p == 25 || p == 26;
    case 8: return p == 3 || p == 24;
    case 9: return p == 1 || p == 2 || p == 3 || p == 17 || p == 24 || p == 25;
    case 10: return p == 1 || p == 2 || p == 3 || p == 16 || p == 17 || p == 18 || p == 19 || p == 20 || p == 21 || p == 22 || p == 23 || p == 24 || p == 25 || p == 26;
    case 11: return p == 16;
    default: return false;
} }
inline bool clock_valid(uint8_t p,uint8_t c) { switch(p) {
    case 1: return c == 1;
    case 2: return c == 1;
    case 3: return c == 1;
    case 16: return c == 3;
    case 17: return c == 3;
    case 18: return c == 2;
    case 19: return c == 2;
    case 20: return c == 2;
    case 21: return c == 2;
    case 22: return c == 2;
    case 23: return c == 3;
    case 24: return c == 2;
    case 25: return c == 3;
    case 26: return c == 3;
    default: return false;
} }
inline bool meta_valid(const Meta& m) {
  return clock_valid(m.producer,m.clock) && (m.clock != 1 || m.ticks <= 0xffffffffULL);
}
inline bool decode(const uint8_t* raw,size_t n,View& view) {
  if(!raw || n < 83 || n > 523 || raw[0]!=165 || raw[1]!=90 ||
     raw[2]!=1 || raw[3]!=kRecordType || raw[4]!=0 ||
     get(raw+7,2)!=n-11 || get(raw+n-2,2)!=crc16(raw+2,n-4)) return false;
  const uint8_t* p=raw+9;
  if(p[0]!=1 || p[3]!=0 || get(p+4,4)!=0x01020304 ||
     memcmp(p+40,kHash,32)!=0) return false;
  Meta m{p[1],p[2],uint32_t(get(p+8,4)),get(p+12,8),get(p+20,8),get(p+28,8)};
  const uint16_t id=uint16_t(get(p+36,2)), len=uint16_t(get(p+38,2));
  if(!meta_valid(m) || !owns(id,m.producer) || body_size(id)!=len ||
     n!=size_t(83+len) || get(raw+5,2)!=(m.trace_sequence&0xffffu)) return false;
  view={m,id,p+72,len}; return true;
}
struct Intent {
  static constexpr uint16_t id = 1;
  static constexpr size_t body_size = 21;
  uint32_t generation{};
  uint32_t authority_epoch{};
  int32_t drive{};
  int32_t steering{};
  uint32_t ehb{};
  uint8_t lane_mask{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(generation), 4);
    put(p + 4, static_cast<uint64_t>(authority_epoch), 4);
    put(p + 8, static_cast<uint64_t>(drive), 4);
    put(p + 12, static_cast<uint64_t>(steering), 4);
    put(p + 16, static_cast<uint64_t>(ehb), 4);
    put(p + 20, static_cast<uint64_t>(lane_mask), 1);
  }
  static bool read(const View& v, Intent& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.generation = static_cast<uint32_t>(get(v.body + 0, 4));
    out.authority_epoch = static_cast<uint32_t>(get(v.body + 4, 4));
    out.drive = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 8, 4))));
    out.steering = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 12, 4))));
    out.ehb = static_cast<uint32_t>(get(v.body + 16, 4));
    out.lane_mask = static_cast<uint8_t>(get(v.body + 20, 1));
    return true;
  }
};

struct DatagramIo {
  static constexpr uint16_t id = 2;
  static constexpr size_t body_size = 37;
  uint8_t stage{};
  uint32_t operation_id{};
  uint32_t wire_sequence{};
  uint32_t generation{};
  uint32_t authority_epoch{};
  uint32_t proof_reference{};
  uint32_t rx_token{};
  uint16_t length{};
  int32_t result{};
  uint32_t peer_ipv4{};
  uint16_t peer_port{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(stage), 1);
    put(p + 1, static_cast<uint64_t>(operation_id), 4);
    put(p + 5, static_cast<uint64_t>(wire_sequence), 4);
    put(p + 9, static_cast<uint64_t>(generation), 4);
    put(p + 13, static_cast<uint64_t>(authority_epoch), 4);
    put(p + 17, static_cast<uint64_t>(proof_reference), 4);
    put(p + 21, static_cast<uint64_t>(rx_token), 4);
    put(p + 25, static_cast<uint64_t>(length), 2);
    put(p + 27, static_cast<uint64_t>(result), 4);
    put(p + 31, static_cast<uint64_t>(peer_ipv4), 4);
    put(p + 35, static_cast<uint64_t>(peer_port), 2);
  }
  static bool read(const View& v, DatagramIo& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.stage = static_cast<uint8_t>(get(v.body + 0, 1));
    out.operation_id = static_cast<uint32_t>(get(v.body + 1, 4));
    out.wire_sequence = static_cast<uint32_t>(get(v.body + 5, 4));
    out.generation = static_cast<uint32_t>(get(v.body + 9, 4));
    out.authority_epoch = static_cast<uint32_t>(get(v.body + 13, 4));
    out.proof_reference = static_cast<uint32_t>(get(v.body + 17, 4));
    out.rx_token = static_cast<uint32_t>(get(v.body + 21, 4));
    out.length = static_cast<uint16_t>(get(v.body + 25, 2));
    out.result = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 27, 4))));
    out.peer_ipv4 = static_cast<uint32_t>(get(v.body + 31, 4));
    out.peer_port = static_cast<uint16_t>(get(v.body + 35, 2));
    return true;
  }
};

struct DatagramBytes {
  static constexpr uint16_t id = 3;
  static constexpr size_t body_size = 144;
  uint32_t operation_id{};
  uint32_t wire_sequence{};
  uint32_t rx_token{};
  uint16_t wire_length{};
  uint16_t captured_length{};
  uint8_t data[128]{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(operation_id), 4);
    put(p + 4, static_cast<uint64_t>(wire_sequence), 4);
    put(p + 8, static_cast<uint64_t>(rx_token), 4);
    put(p + 12, static_cast<uint64_t>(wire_length), 2);
    put(p + 14, static_cast<uint64_t>(captured_length), 2);
    memcpy(p + 16, data, 128);
  }
  static bool read(const View& v, DatagramBytes& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.operation_id = static_cast<uint32_t>(get(v.body + 0, 4));
    out.wire_sequence = static_cast<uint32_t>(get(v.body + 4, 4));
    out.rx_token = static_cast<uint32_t>(get(v.body + 8, 4));
    out.wire_length = static_cast<uint16_t>(get(v.body + 12, 2));
    out.captured_length = static_cast<uint16_t>(get(v.body + 14, 2));
    memcpy(out.data, v.body + 16, 128);
    return true;
  }
};

struct Admission {
  static constexpr uint16_t id = 4;
  static constexpr size_t body_size = 45;
  uint8_t stage{};
  uint32_t reason{};
  uint32_t wire_sequence{};
  uint32_t generation{};
  uint32_t authority_epoch{};
  uint32_t proof_reference{};
  uint32_t rx_token{};
  uint32_t source_epoch{};
  uint32_t activation_epoch{};
  uint32_t forward_age_ms{};
  uint32_t proof_age_ms{};
  uint32_t valid_fields{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(stage), 1);
    put(p + 1, static_cast<uint64_t>(reason), 4);
    put(p + 5, static_cast<uint64_t>(wire_sequence), 4);
    put(p + 9, static_cast<uint64_t>(generation), 4);
    put(p + 13, static_cast<uint64_t>(authority_epoch), 4);
    put(p + 17, static_cast<uint64_t>(proof_reference), 4);
    put(p + 21, static_cast<uint64_t>(rx_token), 4);
    put(p + 25, static_cast<uint64_t>(source_epoch), 4);
    put(p + 29, static_cast<uint64_t>(activation_epoch), 4);
    put(p + 33, static_cast<uint64_t>(forward_age_ms), 4);
    put(p + 37, static_cast<uint64_t>(proof_age_ms), 4);
    put(p + 41, static_cast<uint64_t>(valid_fields), 4);
  }
  static bool read(const View& v, Admission& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.stage = static_cast<uint8_t>(get(v.body + 0, 1));
    out.reason = static_cast<uint32_t>(get(v.body + 1, 4));
    out.wire_sequence = static_cast<uint32_t>(get(v.body + 5, 4));
    out.generation = static_cast<uint32_t>(get(v.body + 9, 4));
    out.authority_epoch = static_cast<uint32_t>(get(v.body + 13, 4));
    out.proof_reference = static_cast<uint32_t>(get(v.body + 17, 4));
    out.rx_token = static_cast<uint32_t>(get(v.body + 21, 4));
    out.source_epoch = static_cast<uint32_t>(get(v.body + 25, 4));
    out.activation_epoch = static_cast<uint32_t>(get(v.body + 29, 4));
    out.forward_age_ms = static_cast<uint32_t>(get(v.body + 33, 4));
    out.proof_age_ms = static_cast<uint32_t>(get(v.body + 37, 4));
    out.valid_fields = static_cast<uint32_t>(get(v.body + 41, 4));
    return true;
  }
};

struct Proof {
  static constexpr uint16_t id = 5;
  static constexpr size_t body_size = 33;
  uint8_t stage{};
  uint32_t sequence{};
  uint32_t admitted_sequence{};
  uint32_t generation{};
  uint32_t authority_epoch{};
  uint32_t rx_token{};
  uint32_t status{};
  uint32_t reason{};
  uint32_t flags{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(stage), 1);
    put(p + 1, static_cast<uint64_t>(sequence), 4);
    put(p + 5, static_cast<uint64_t>(admitted_sequence), 4);
    put(p + 9, static_cast<uint64_t>(generation), 4);
    put(p + 13, static_cast<uint64_t>(authority_epoch), 4);
    put(p + 17, static_cast<uint64_t>(rx_token), 4);
    put(p + 21, static_cast<uint64_t>(status), 4);
    put(p + 25, static_cast<uint64_t>(reason), 4);
    put(p + 29, static_cast<uint64_t>(flags), 4);
  }
  static bool read(const View& v, Proof& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.stage = static_cast<uint8_t>(get(v.body + 0, 1));
    out.sequence = static_cast<uint32_t>(get(v.body + 1, 4));
    out.admitted_sequence = static_cast<uint32_t>(get(v.body + 5, 4));
    out.generation = static_cast<uint32_t>(get(v.body + 9, 4));
    out.authority_epoch = static_cast<uint32_t>(get(v.body + 13, 4));
    out.rx_token = static_cast<uint32_t>(get(v.body + 17, 4));
    out.status = static_cast<uint32_t>(get(v.body + 21, 4));
    out.reason = static_cast<uint32_t>(get(v.body + 25, 4));
    out.flags = static_cast<uint32_t>(get(v.body + 29, 4));
    return true;
  }
};

struct Transaction {
  static constexpr uint16_t id = 6;
  static constexpr size_t body_size = 27;
  uint8_t stage{};
  uint32_t command_id{};
  uint32_t transaction_id{};
  uint16_t record_type{};
  int32_t result{};
  uint32_t authority_epoch{};
  uint32_t generation{};
  uint32_t reason{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(stage), 1);
    put(p + 1, static_cast<uint64_t>(command_id), 4);
    put(p + 5, static_cast<uint64_t>(transaction_id), 4);
    put(p + 9, static_cast<uint64_t>(record_type), 2);
    put(p + 11, static_cast<uint64_t>(result), 4);
    put(p + 15, static_cast<uint64_t>(authority_epoch), 4);
    put(p + 19, static_cast<uint64_t>(generation), 4);
    put(p + 23, static_cast<uint64_t>(reason), 4);
  }
  static bool read(const View& v, Transaction& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.stage = static_cast<uint8_t>(get(v.body + 0, 1));
    out.command_id = static_cast<uint32_t>(get(v.body + 1, 4));
    out.transaction_id = static_cast<uint32_t>(get(v.body + 5, 4));
    out.record_type = static_cast<uint16_t>(get(v.body + 9, 2));
    out.result = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 11, 4))));
    out.authority_epoch = static_cast<uint32_t>(get(v.body + 15, 4));
    out.generation = static_cast<uint32_t>(get(v.body + 19, 4));
    out.reason = static_cast<uint32_t>(get(v.body + 23, 4));
    return true;
  }
};

struct Stream {
  static constexpr uint16_t id = 7;
  static constexpr size_t body_size = 37;
  uint8_t stage{};
  uint16_t channel{};
  uint32_t operation_id{};
  uint64_t publish_sequence{};
  uint64_t byte_start{};
  uint32_t length{};
  int32_t result{};
  uint16_t record_type{};
  uint32_t reason{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(stage), 1);
    put(p + 1, static_cast<uint64_t>(channel), 2);
    put(p + 3, static_cast<uint64_t>(operation_id), 4);
    put(p + 7, static_cast<uint64_t>(publish_sequence), 8);
    put(p + 15, static_cast<uint64_t>(byte_start), 8);
    put(p + 23, static_cast<uint64_t>(length), 4);
    put(p + 27, static_cast<uint64_t>(result), 4);
    put(p + 31, static_cast<uint64_t>(record_type), 2);
    put(p + 33, static_cast<uint64_t>(reason), 4);
  }
  static bool read(const View& v, Stream& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.stage = static_cast<uint8_t>(get(v.body + 0, 1));
    out.channel = static_cast<uint16_t>(get(v.body + 1, 2));
    out.operation_id = static_cast<uint32_t>(get(v.body + 3, 4));
    out.publish_sequence = static_cast<uint64_t>(get(v.body + 7, 8));
    out.byte_start = static_cast<uint64_t>(get(v.body + 15, 8));
    out.length = static_cast<uint32_t>(get(v.body + 23, 4));
    out.result = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 27, 4))));
    out.record_type = static_cast<uint16_t>(get(v.body + 31, 2));
    out.reason = static_cast<uint32_t>(get(v.body + 33, 4));
    return true;
  }
};

struct Resource {
  static constexpr uint16_t id = 8;
  static constexpr size_t body_size = 27;
  uint8_t stage{};
  uint64_t resource_id{};
  uint16_t resource_kind{};
  uint32_t operation_id{};
  int32_t result{};
  uint64_t owner_epoch{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(stage), 1);
    put(p + 1, static_cast<uint64_t>(resource_id), 8);
    put(p + 9, static_cast<uint64_t>(resource_kind), 2);
    put(p + 11, static_cast<uint64_t>(operation_id), 4);
    put(p + 15, static_cast<uint64_t>(result), 4);
    put(p + 19, static_cast<uint64_t>(owner_epoch), 8);
  }
  static bool read(const View& v, Resource& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.stage = static_cast<uint8_t>(get(v.body + 0, 1));
    out.resource_id = static_cast<uint64_t>(get(v.body + 1, 8));
    out.resource_kind = static_cast<uint16_t>(get(v.body + 9, 2));
    out.operation_id = static_cast<uint32_t>(get(v.body + 11, 4));
    out.result = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 15, 4))));
    out.owner_epoch = static_cast<uint64_t>(get(v.body + 19, 8));
    return true;
  }
};

struct Capacity {
  static constexpr uint16_t id = 9;
  static constexpr size_t body_size = 24;
  uint32_t resource_id{};
  uint32_t used{};
  uint32_t high_water{};
  uint32_t failures{};
  uint64_t service_gap_ticks{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(resource_id), 4);
    put(p + 4, static_cast<uint64_t>(used), 4);
    put(p + 8, static_cast<uint64_t>(high_water), 4);
    put(p + 12, static_cast<uint64_t>(failures), 4);
    put(p + 16, static_cast<uint64_t>(service_gap_ticks), 8);
  }
  static bool read(const View& v, Capacity& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.resource_id = static_cast<uint32_t>(get(v.body + 0, 4));
    out.used = static_cast<uint32_t>(get(v.body + 4, 4));
    out.high_water = static_cast<uint32_t>(get(v.body + 8, 4));
    out.failures = static_cast<uint32_t>(get(v.body + 12, 4));
    out.service_gap_ticks = static_cast<uint64_t>(get(v.body + 16, 8));
    return true;
  }
};

struct TraceLoss {
  static constexpr uint16_t id = 10;
  static constexpr size_t body_size = 18;
  uint32_t first_missing_sequence{};
  uint32_t last_missing_sequence{};
  uint32_t dropped{};
  uint16_t reason{};
  uint32_t high_water{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(first_missing_sequence), 4);
    put(p + 4, static_cast<uint64_t>(last_missing_sequence), 4);
    put(p + 8, static_cast<uint64_t>(dropped), 4);
    put(p + 12, static_cast<uint64_t>(reason), 2);
    put(p + 14, static_cast<uint64_t>(high_water), 4);
  }
  static bool read(const View& v, TraceLoss& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.first_missing_sequence = static_cast<uint32_t>(get(v.body + 0, 4));
    out.last_missing_sequence = static_cast<uint32_t>(get(v.body + 4, 4));
    out.dropped = static_cast<uint32_t>(get(v.body + 8, 4));
    out.reason = static_cast<uint16_t>(get(v.body + 12, 2));
    out.high_water = static_cast<uint32_t>(get(v.body + 14, 4));
    return true;
  }
};

struct IntentState {
  static constexpr uint16_t id = 11;
  static constexpr size_t body_size = 50;
  uint8_t stage{};
  uint64_t revision{};
  uint32_t generation{};
  uint32_t authority_epoch{};
  int32_t drive{};
  int32_t steering{};
  uint8_t lane_mask{};
  uint8_t data005[8]{};
  uint8_t data007[8]{};
  uint8_t data364[8]{};
  void write(uint8_t* p) const {
    put(p + 0, static_cast<uint64_t>(stage), 1);
    put(p + 1, static_cast<uint64_t>(revision), 8);
    put(p + 9, static_cast<uint64_t>(generation), 4);
    put(p + 13, static_cast<uint64_t>(authority_epoch), 4);
    put(p + 17, static_cast<uint64_t>(drive), 4);
    put(p + 21, static_cast<uint64_t>(steering), 4);
    put(p + 25, static_cast<uint64_t>(lane_mask), 1);
    memcpy(p + 26, data005, 8);
    memcpy(p + 34, data007, 8);
    memcpy(p + 42, data364, 8);
  }
  static bool read(const View& v, IntentState& out) {
    if (v.event_id != id || v.body_length != body_size) return false;
    out.stage = static_cast<uint8_t>(get(v.body + 0, 1));
    out.revision = static_cast<uint64_t>(get(v.body + 1, 8));
    out.generation = static_cast<uint32_t>(get(v.body + 9, 4));
    out.authority_epoch = static_cast<uint32_t>(get(v.body + 13, 4));
    out.drive = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 17, 4))));
    out.steering = static_cast<int32_t>(signed32(static_cast<uint32_t>(get(v.body + 21, 4))));
    out.lane_mask = static_cast<uint8_t>(get(v.body + 25, 1));
    memcpy(out.data005, v.body + 26, 8);
    memcpy(out.data007, v.body + 34, 8);
    memcpy(out.data364, v.body + 42, 8);
    return true;
  }
};
template<class Event>
inline size_t encode(const Meta& m,const Event& event,uint8_t* raw,size_t capacity) {
  const size_t n=83+Event::body_size;
  if(!raw || capacity<n || !meta_valid(m) || !owns(Event::id,m.producer)) return 0;
  raw[0]=165;raw[1]=90;raw[2]=1;raw[3]=kRecordType;raw[4]=0;
  put(raw+5,m.trace_sequence&0xffff,2);put(raw+7,72+Event::body_size,2);
  uint8_t* p=raw+9;
  p[0]=1;p[1]=m.producer;p[2]=m.clock;p[3]=0;put(p+4,0x01020304,4);
  put(p+8,m.trace_sequence,4);put(p+12,m.epoch,8);put(p+20,m.boot_id,8);put(p+28,m.ticks,8);
  put(p+36,Event::id,2);put(p+38,Event::body_size,2);memcpy(p+40,kHash,32);
  event.write(p+72);put(raw+n-2,crc16(raw+2,n-4),2);return n;
}
} }
