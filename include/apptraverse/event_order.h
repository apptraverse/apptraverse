#ifndef APPTRAVERSE_EVENT_ORDER_H_
#define APPTRAVERSE_EVENT_ORDER_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

// Journal position for a local Event record: timestamp in microseconds only.
// Two different Events may share the same timestamp; tie-breaking by insertion
// index is local implementation behavior, not a global rule.
struct EventOrder {
  std::uint64_t timestamp_us{0};

  bool operator==(EventOrder const& other) const noexcept {
    return timestamp_us == other.timestamp_us;
  }

  bool operator!=(EventOrder const& other) const noexcept {
    return !(*this == other);
  }

  AE_REFLECT_MEMBERS(timestamp_us)
};

inline bool EventOrderLess(EventOrder const& a, EventOrder const& b) noexcept {
  return a.timestamp_us < b.timestamp_us;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_ORDER_H_
