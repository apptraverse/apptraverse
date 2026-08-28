#ifndef APPTRAVERSE_SHARED_EVENT_ORDER_H_
#define APPTRAVERSE_SHARED_EVENT_ORDER_H_

#include <cstdint>
#include <functional>
#include <string>

namespace apptraverse {

// Canonical sort key for shared journal entries on every replica.
struct SharedEventOrder {
  std::uint64_t lamport{0};
  std::string origin_uid;
  std::uint64_t origin_sequence{0};
};

inline bool SharedEventOrderLess(SharedEventOrder const& a,
                                 SharedEventOrder const& b) noexcept {
  if (a.lamport != b.lamport) {
    return a.lamport < b.lamport;
  }
  if (a.origin_uid != b.origin_uid) {
    return a.origin_uid < b.origin_uid;
  }
  return a.origin_sequence < b.origin_sequence;
}

inline std::uint64_t EncodeOrderTimestamp(SharedEventOrder const& order) {
  std::uint64_t const uid_hash =
      static_cast<std::uint64_t>(std::hash<std::string>{}(order.origin_uid));
  return (order.lamport << 24U) | ((uid_hash & 0xFFFFU) << 8U) |
         (order.origin_sequence & 0xFFU);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_EVENT_ORDER_H_
