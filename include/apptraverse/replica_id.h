#ifndef APPTRAVERSE_REPLICA_ID_H_
#define APPTRAVERSE_REPLICA_ID_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

struct ReplicaId {
  std::uint64_t value{0};

  bool IsValid() const { return value != 0; }

  bool operator==(ReplicaId const&) const = default;
  bool operator<(ReplicaId const& other) const { return value < other.value; }

  AE_REFLECT_MEMBERS(value)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_REPLICA_ID_H_
