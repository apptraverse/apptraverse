#ifndef APPTRAVERSE_REPLICA_ID_H_
#define APPTRAVERSE_REPLICA_ID_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

struct ReplicaId {
  std::uint32_t value{0};

  constexpr ReplicaId() = default;
  constexpr explicit ReplicaId(std::uint32_t v) : value{v} {}

  constexpr bool IsValid() const { return value != 0; }

  constexpr bool operator==(ReplicaId const& other) const {
    return value == other.value;
  }
  constexpr bool operator!=(ReplicaId const& other) const {
    return value != other.value;
  }
  constexpr bool operator<(ReplicaId const& other) const {
    return value < other.value;
  }

  AE_REFLECT_MEMBERS(value)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICA_ID_H_
