#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>
#include <vector>

#include "aether/clock.h"
#include "aether/obj/obj_id.h"
#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"

namespace apptraverse {

enum class DeliveryStatus : std::uint8_t {
  kPending,
  kDelivered,
};

enum class EventRecordOrigin : std::uint8_t {
  kLocal,
  kRemote,
};

struct EventRecipientState {
  ae::ObjId recipient;
  DeliveryStatus delivery_status{DeliveryStatus::kPending};

  AE_REFLECT_MEMBERS(recipient, delivery_status)
};

struct EventRecord {
  Event::ptr event;
  ae::TimePoint time{};
  EventRecordOrigin origin{EventRecordOrigin::kLocal};
  std::vector<EventRecipientState> recipients;

  EventRecipientState* FindRecipient(ae::ObjId recipient) {
    for (auto& state : recipients) {
      if (state.recipient == recipient) {
        return &state;
      }
    }
    return nullptr;
  }

  EventRecipientState const* FindRecipient(ae::ObjId recipient) const {
    for (auto const& state : recipients) {
      if (state.recipient == recipient) {
        return &state;
      }
    }
    return nullptr;
  }

  AE_REFLECT_MEMBERS(event, time, origin, recipients)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
