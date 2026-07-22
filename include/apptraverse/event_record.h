#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>
#include <utility>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"

namespace apptraverse {

enum class EventDeliveryState : std::uint8_t {
  kPending,
  kSent,
  kConfirmed,
};

/**
 * \brief One local persistent journal entry.
 *
 * Holds the event reference plus local commit metadata. Delivery-state
 * transitions and distributed ordering are intentionally out of scope here.
 */
class EventRecord {
 public:
  EventRecord() = default;

  EventRecord(Event::ptr event, std::uint64_t sequence,
              EventDeliveryState delivery_state)
      : event_{std::move(event)},
        sequence_{sequence},
        delivery_state_{delivery_state} {}

  Event::ptr const& event() const { return event_; }
  std::uint64_t sequence() const { return sequence_; }
  EventDeliveryState delivery_state() const { return delivery_state_; }

  AE_REFLECT_MEMBERS(event_, sequence_, delivery_state_)

 private:
  friend class Node;

  Event::ptr event_;
  std::uint64_t sequence_{0};
  EventDeliveryState delivery_state_{EventDeliveryState::kPending};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
