#ifndef APPTRAVERSE_EVENT_ORDER_H_
#define APPTRAVERSE_EVENT_ORDER_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

// Primary journal position: timestamp in microseconds. EventRecordOrderLess
// explicitly resolves equal timestamps by serialized Event ID.
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
