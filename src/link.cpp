#include "apptraverse/link.h"

#include "apptraverse/endpoint_uid.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Link);
APPTRAVERSE_REGISTER(MemoryLink);

ae::Uid const kNoEndpointUid{};

}  // namespace

ae::Uid const& Link::EndpointUid() const { return kNoEndpointUid; }

void ForceLinkRegistration() {}

}  // namespace apptraverse
