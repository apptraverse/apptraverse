#ifndef APPTRAVERSE_TESTS_MEMORY_TEST_ENDPOINT_H_
#define APPTRAVERSE_TESTS_MEMORY_TEST_ENDPOINT_H_

#include <string>

#include "apptraverse/endpoint_uid.h"

namespace apptraverse::test {

inline ae::Uid MemoryEndpointFromTag(char tag) {
  return MemoryTestEndpointUid(static_cast<std::uint8_t>(tag));
}

inline std::string MemoryEndpointTransport(char tag) {
  return FormatEndpointUid(MemoryEndpointFromTag(tag));
}

inline ae::Uid LegacyLabelEndpoint(std::string const& label) {
  return LoadLegacyEndpointUidForMigration(label);
}

inline std::string LegacyLabelTransport(std::string const& label) {
  return FormatEndpointUid(LegacyLabelEndpoint(label));
}

}  // namespace apptraverse::test

#endif  // APPTRAVERSE_TESTS_MEMORY_TEST_ENDPOINT_H_
