#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"
#include "apptraverse/event_order.h"

namespace apptraverse {

// Single authoritative journal entry.
//
// - order.timestamp_us is the primary journal position. Equal timestamps use
//   the serialized Event ID as an explicit deterministic tie-breaker.
// - retained_since_us is local replica bookkeeping for age retention. It is
//   neither order nor a logical Event identity.
struct EventRecord {
  Event::ptr event;
  EventOrder order{};
  std::uint64_t retained_since_us{0};

  AE_REFLECT_MEMBERS(event, order, retained_since_us)
};

// Journal position only; retention bookkeeping is never consulted. Shared
// replicas receive the same Event ID, so equal-time arrival order cannot
// change replay order. Per-author indexes remain delivery cursors, not order.
inline bool EventRecordOrderLess(EventRecord const& a,
                                 EventRecord const& b) noexcept {
  if (a.order.timestamp_us != b.order.timestamp_us) {
    return EventOrderLess(a.order, b.order);
  }
  return (a.event.is_valid() ? a.event.id() : ae::ObjId{}) <
         (b.event.is_valid() ? b.event.id() : ae::ObjId{});
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
