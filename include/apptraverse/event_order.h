#ifndef APPTRAVERSE_EVENT_ORDER_H_
#define APPTRAVERSE_EVENT_ORDER_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

namespace apptraverse {

struct EventOrder {
  // Unix UTC time in microseconds since epoch. Zero is invalid.
  std::uint64_t timestamp_us{0};

  bool IsValid() const { return timestamp_us != 0; }

  bool operator==(EventOrder const&) const = default;

  bool operator<(EventOrder const& other) const {
    return timestamp_us < other.timestamp_us;
  }

  AE_REFLECT_MEMBERS(timestamp_us)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_ORDER_H_
