#include "apptraverse/link.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Link);
APPTRAVERSE_REGISTER(MemoryLink);

std::string const kNoEndpointUid{};

}  // namespace

std::string const& Link::EndpointUid() const { return kNoEndpointUid; }

void ForceLinkRegistration() {}

}  // namespace apptraverse
