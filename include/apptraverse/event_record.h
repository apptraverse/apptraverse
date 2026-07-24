#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include <cstdint>

#include "aether/clock.h"
#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"

namespace apptraverse {

enum class DeliveryStatus : std::uint8_t {
  kPending,
  kDelivered,
};

struct EventRecord {
  Event::ptr event;
  ae::TimePoint time{};
  DeliveryStatus delivery_status{DeliveryStatus::kPending};
  AE_REFLECT_MEMBERS(event, time, delivery_status)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
