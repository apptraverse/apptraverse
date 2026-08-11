#include "apptraverse/object_macros.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/sync_packet.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);
APPTRAVERSE_REGISTER(SyncPacket);
APPTRAVERSE_REGISTER(NodeStatePacket);
APPTRAVERSE_REGISTER(EventPacket);
APPTRAVERSE_REGISTER(NodeStateRequestPacket);
APPTRAVERSE_REGISTER(AckPacket);

}  // namespace

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {}

}  // namespace apptraverse
