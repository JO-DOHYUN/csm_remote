#include "board/remote/RemoteTypes.h"

namespace csm::board::remote {
namespace {

static_assert(kRcChannelCount == 16, "RC sample contract is fixed at 16 channels");
static_assert(sizeof(RcSample) <= 64, "RcSample must remain bounded for mailbox handoff");
static_assert(!isUsableRemoteLink(RemoteLinkState::Stale),
              "stale remote link must not be usable");
static_assert(!isUsableRcSampleState(RcSampleState::Failsafe),
              "failsafe RC sample must not be usable");

}  // namespace
}  // namespace csm::board::remote

