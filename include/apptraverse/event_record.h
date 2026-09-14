#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"

namespace apptraverse {

// Single authoritative journal entry. Three timestamps-and-identities live
// here and they are deliberately separate:
//
// - order.timestamp_us is the only thing that decides journal position.
// - identity is the logical Event identity, used for deduplication and for
//   recognizing the same Event arriving twice. It never affects position.
// - retained_since_us is local replica bookkeeping for age retention. It is
//   neither order nor identity.
//
// Shared Events always carry an identity before insert; a Node's own local
// Events use an empty identity.
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

// Journal position, and only that. Identity and retention bookkeeping are not
// consulted, so records with equal timestamps compare equal in both
// directions and whatever a container algorithm does with them is local
// implementation behavior, not a distributed ordering rule.
inline bool EventRecordOrderLess(EventRecord const& a,
                                 EventRecord const& b) noexcept {
  return SharedEventOrderLess(a.order, b.order);
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
