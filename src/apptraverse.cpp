#include "apptraverse/object_macros.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"
#include "apptraverse/sync_packet.h"
#include "apptraverse/sync_session_state.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Event);
APPTRAVERSE_REGISTER(Node);
APPTRAVERSE_REGISTER(SyncPacket);
APPTRAVERSE_REGISTER(NodeStatePacket);
APPTRAVERSE_REGISTER(EventPacket);
APPTRAVERSE_REGISTER(AckPacket);
APPTRAVERSE_REGISTER(SyncSessionState);
APPTRAVERSE_REGISTER(SetSyncSessionDataEvent);

}  // namespace

// Forces the static library object file (and its Registrars) to be linked.
void EnsureObjectRegistration() {}

}  // namespace apptraverse
