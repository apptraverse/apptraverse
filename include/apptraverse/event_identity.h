#ifndef APPTRAVERSE_EVENT_IDENTITY_H_
#define APPTRAVERSE_EVENT_IDENTITY_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/replica_id.h"

namespace apptraverse {

struct EventIdentity {
  ReplicaId origin_replica;
  std::uint32_t origin_sequence{0};

  bool IsValid() const {
    return origin_replica.IsValid() && origin_sequence != 0;
  }

  bool operator==(EventIdentity const&) const = default;
  bool operator<(EventIdentity const& other) const {
    if (origin_replica < other.origin_replica) {
      return true;
    }
    if (other.origin_replica < origin_replica) {
      return false;
    }
    return origin_sequence < other.origin_sequence;
  }

  AE_REFLECT_MEMBERS(origin_replica, origin_sequence)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_IDENTITY_H_
