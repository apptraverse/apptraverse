#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"
#include "apptraverse/event_order.h"

namespace apptraverse {

// Single authoritative journal entry.
//
// - order.timestamp_us is the only thing that decides journal position.
// - retained_since_us is local replica bookkeeping for age retention. It is
//   neither order nor a logical Event identity.
struct EventRecord {
  Event::ptr event;
  EventOrder order{};
  std::uint64_t retained_since_us{0};

  AE_REFLECT_MEMBERS(event, order, retained_since_us)
};

// Journal position, and only that. Retention bookkeeping is not consulted, so
// records with equal timestamps compare equal in both directions and whatever
// a container algorithm does with them is local implementation behavior.
inline bool EventRecordOrderLess(EventRecord const& a,
                                 EventRecord const& b) noexcept {
  return EventOrderLess(a.order, b.order);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
