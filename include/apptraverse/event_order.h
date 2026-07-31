#ifndef APPTRAVERSE_EVENT_ORDER_H_
#define APPTRAVERSE_EVENT_ORDER_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/replica_id.h"

namespace apptraverse {

struct EventOrder {
  std::uint64_t logical_time{0};
  ReplicaId origin_replica;
  std::uint32_t origin_sequence{0};

  bool IsValid() const {
    return logical_time != 0 && origin_replica.IsValid() &&
           origin_sequence != 0;
  }

  bool operator==(EventOrder const&) const = default;

  bool operator<(EventOrder const& other) const {
    if (logical_time < other.logical_time) {
      return true;
    }
    if (other.logical_time < logical_time) {
      return false;
    }
    if (origin_replica < other.origin_replica) {
      return true;
    }
    if (other.origin_replica < origin_replica) {
      return false;
    }
    return origin_sequence < other.origin_sequence;
  }

  AE_REFLECT_MEMBERS(logical_time, origin_replica, origin_sequence)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_ORDER_H_
