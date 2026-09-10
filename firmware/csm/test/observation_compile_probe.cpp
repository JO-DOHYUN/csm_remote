// P1 target-header compile only. This is not firmware inclusion or runtime coverage.
#include "../shared/observability/generated/Observation.h"
namespace obs = csm::observation;
template<class Event>
size_t probe(uint8_t* output, size_t capacity, const obs::Meta& meta, const Event& event) {
  return obs::encode(meta, event, output, capacity);
}
#define COMPILE_EVENT(Name) template size_t probe(uint8_t*, size_t, const obs::Meta&, const obs::Name&)
COMPILE_EVENT(Intent);
COMPILE_EVENT(DatagramIo);
COMPILE_EVENT(DatagramBytes);
COMPILE_EVENT(Admission);
COMPILE_EVENT(Proof);
COMPILE_EVENT(Transaction);
COMPILE_EVENT(Stream);
COMPILE_EVENT(Resource);
COMPILE_EVENT(Capacity);
COMPILE_EVENT(TraceLoss);
#undef COMPILE_EVENT
bool decode_probe(const uint8_t* input, size_t length, obs::View& view) {
  return obs::decode(input, length, view);
}
