#ifndef APPTRAVERSE_EVENT_IDENTITY_H_
#define APPTRAVERSE_EVENT_IDENTITY_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/replica_id.h"

namespace apptraverse {

struct EventIdentity {
  ReplicaId origin{};
  std::uint64_t sequence{0};

  constexpr EventIdentity() = default;
  constexpr EventIdentity(ReplicaId origin_replica, std::uint64_t origin_sequence)
      : origin{origin_replica}, sequence{origin_sequence} {}

  constexpr bool IsValid() const {
    return origin.IsValid() && sequence != 0;
  }

  constexpr bool operator==(EventIdentity const& other) const {
    return origin == other.origin && sequence == other.sequence;
  }
  constexpr bool operator!=(EventIdentity const& other) const {
    return !(*this == other);
  }
  constexpr bool operator<(EventIdentity const& other) const {
    if (origin < other.origin) {
      return true;
    }
    if (other.origin < origin) {
      return false;
    }
    return sequence < other.sequence;
  }

  AE_REFLECT_MEMBERS(origin, sequence)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_IDENTITY_H_
