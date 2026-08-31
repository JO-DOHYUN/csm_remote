#include "board/remote/RemoteTypes.h"

namespace csm::board::remote {
namespace {

static_assert(kRcChannelCount == 16, "RC sample contract is fixed at 16 channels");
static_assert(sizeof(RcSample) <= 64, "RcSample must remain bounded for mailbox handoff");
static_assert(!isUsableRemoteLink(RemoteLinkState::Stale),
              "stale remote link must not be usable");
static_assert(!isUsableRcSampleState(RcSampleState::Failsafe),
              "failsafe RC sample must not be usable");
static_assert(remoteLinkStateForRcSampleState(RcSampleState::Lost) ==
                  RemoteLinkState::Searching,
              "lost RC sample must remain a searching link");
static_assert(remoteLinkStateForRcSampleState(RcSampleState::Ok) ==
                  RemoteLinkState::Valid,
              "OK RC sample must remain the only valid link state");

}  // namespace
}  // namespace csm::board::remote
