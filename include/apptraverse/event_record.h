#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"

namespace apptraverse {

// Single authoritative journal entry. Shared ChatRoom Events always carry
// identity + SharedEventOrder before insert. Non-shared Nodes may use a
// local-only order (empty identity.origin_uid) with monotonic lamport.
//
// retained_since_us is local replica bookkeeping for age retention. It is not
// part of canonical order, SharedEventId, or EventRecordOrderLess.
struct EventRecord {
  Event::ptr event;
  SharedEventId identity{};
  SharedEventOrder order{};
  std::uint64_t retained_since_us{0};

  bool HasSharedIdentity() const noexcept {
    return !identity.origin_uid.empty();
  }

  AE_REFLECT_MEMBERS(event, identity, order, retained_since_us)
};

// Wire layout for Node journal v1 (before retained_since_us).
struct EventRecordWireV1 {
  Event::ptr event;
  SharedEventId identity{};
  SharedEventOrder order{};

  AE_REFLECT_MEMBERS(event, identity, order)
};

inline bool EventRecordOrderLess(EventRecord const& a,
                                 EventRecord const& b) noexcept {
  return SharedEventOrderLess(a.order, b.order);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
