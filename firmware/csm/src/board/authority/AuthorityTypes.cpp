#include "board/authority/AuthorityTypes.h"

namespace csm::board::authority {
namespace {

static_assert(!mayConsiderLocalControl(AutonomyAuthorityState::Unknown),
              "Unknown autonomy must deny local control consideration");
static_assert(mayConsiderLocalControl(AutonomyAuthorityState::InactiveConfirmed),
              "InactiveConfirmed is the only local-control candidate state");

}  // namespace
}  // namespace csm::board::authority

