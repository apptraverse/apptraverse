#include "apptraverse/link.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Link);
APPTRAVERSE_REGISTER(MemoryLink);

}  // namespace

void ForceLinkRegistration() {}

}  // namespace apptraverse
