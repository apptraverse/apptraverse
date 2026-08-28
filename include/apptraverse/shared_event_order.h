#ifndef APPTRAVERSE_SHARED_EVENT_ORDER_H_
#define APPTRAVERSE_SHARED_EVENT_ORDER_H_

#include <cstdint>
#include <string>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

// Canonical sort key for shared journal entries on every replica.
// Compared as full (lamport, origin_uid, origin_sequence) — never packed.
struct SharedEventOrder {
  std::uint64_t lamport{0};
  std::string origin_uid;
  std::uint64_t origin_sequence{0};

  bool operator==(SharedEventOrder const& other) const noexcept {
    return lamport == other.lamport && origin_uid == other.origin_uid &&
           origin_sequence == other.origin_sequence;
  }

  bool operator!=(SharedEventOrder const& other) const noexcept {
    return !(*this == other);
  }

  AE_REFLECT_MEMBERS(lamport, origin_uid, origin_sequence)
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

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_EVENT_ORDER_H_
