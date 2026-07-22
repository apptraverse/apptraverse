#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>
#include <utility>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"
#include "apptraverse/event_identity.h"

namespace apptraverse {

enum class EventDeliveryState : std::uint8_t {
  kPending,
  kSent,
  kConfirmed,
};

/**
 * \brief One local persistent journal entry.
 *
 * Delivery state is modeled for exactly two replicas of the same logical
 * Node. Multi-peer acknowledgement maps are intentionally out of scope.
 */
class EventRecord {
 public:
  EventRecord() = default;

  EventRecord(Event::ptr event, EventIdentity identity,
              std::uint64_t logical_time, EventDeliveryState delivery_state)
      : event_{std::move(event)},
        identity_{identity},
        logical_time_{logical_time},
        delivery_state_{delivery_state} {}

  Event::ptr const& event() const { return event_; }
  EventIdentity const& identity() const { return identity_; }
  ReplicaId origin() const { return identity_.origin; }
  std::uint64_t origin_sequence() const { return identity_.sequence; }
  std::uint64_t logical_time() const { return logical_time_; }
  EventDeliveryState delivery_state() const { return delivery_state_; }

  static bool OrderBefore(EventRecord const& lhs, EventRecord const& rhs) {
    if (lhs.logical_time_ < rhs.logical_time_) {
      return true;
    }
    if (rhs.logical_time_ < lhs.logical_time_) {
      return false;
    }
    return lhs.identity_ < rhs.identity_;
  }

  AE_REFLECT_MEMBERS(event_, identity_, logical_time_, delivery_state_)

 private:
  friend class Node;

  Event::ptr event_;
  EventIdentity identity_{};
  std::uint64_t logical_time_{0};
  EventDeliveryState delivery_state_{EventDeliveryState::kPending};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
