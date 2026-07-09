#include "board/control/OperatorCommand.h"

namespace csm::board::control {
namespace {

static_assert(sizeof(OperatorCommand) <= 32,
              "OperatorCommand must remain a small normalized intent object");
static_assert(isWithinOperatorCommandRange(OperatorCommand{}),
              "default OperatorCommand must be neutral and in range");
static_assert(!hasLocalControlSource(OperatorCommand{}),
              "default OperatorCommand must not claim a local source");

}  // namespace
}  // namespace csm::board::control

