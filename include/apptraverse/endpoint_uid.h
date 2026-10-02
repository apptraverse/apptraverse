#ifndef APPTRAVERSE_ENDPOINT_UID_H_
#define APPTRAVERSE_ENDPOINT_UID_H_

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "aether/types/uid.h"

namespace apptraverse {

std::string FormatEndpointUid(ae::Uid uid);
ae::Uid ParseEndpointUid(std::string_view text);

// Deterministic test / memory-transport identities (not RFC display strings).
ae::Uid MemoryTestEndpointUid(std::uint8_t tag);

bool EndpointMatchesTransport(ae::Uid const& endpoint,
                              std::string const& transport_text);

ae::Uid LoadLegacyEndpointUidForMigration(std::string const& legacy);

}  // namespace apptraverse

#endif  // APPTRAVERSE_ENDPOINT_UID_H_
