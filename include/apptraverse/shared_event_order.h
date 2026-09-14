#ifndef APPTRAVERSE_SHARED_EVENT_ORDER_H_
#define APPTRAVERSE_SHARED_EVENT_ORDER_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

// Position of a shared journal entry: an event timestamp in microseconds, and
// nothing else. Order and identity are independent concepts — SharedEventId
// says which logical Event this is, and never influences where it sits.
//
// Two different Events may legitimately carry the same timestamp. What happens
// then is unresolved architecture, not a rule this type encodes: there is no
// tie-break by origin, sequence, ObjId, endpoint, or insertion index, and
// replicas are not claimed to converge for that case.
struct SharedEventOrder {
  std::uint64_t timestamp_us{0};

  bool operator==(SharedEventOrder const& other) const noexcept {
    return timestamp_us == other.timestamp_us;
  }

  bool operator!=(SharedEventOrder const& other) const noexcept {
    return !(*this == other);
  }

  AE_REFLECT_MEMBERS(timestamp_us)
};

inline bool SharedEventOrderLess(SharedEventOrder const& a,
                                 SharedEventOrder const& b) noexcept {
  return a.timestamp_us < b.timestamp_us;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_SHARED_EVENT_ORDER_H_
