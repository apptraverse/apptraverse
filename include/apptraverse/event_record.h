#ifndef APPTRAVERSE_EVENT_RECORD_H_
#define APPTRAVERSE_EVENT_RECORD_H_

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event.h"
#include "apptraverse/event_identity.h"
#include "apptraverse/event_order.h"

namespace apptraverse {

struct EventRecord {
  EventIdentity identity;
  EventOrder order;
  Event::ptr event;

  AE_REFLECT_MEMBERS(identity, order, event)
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_RECORD_H_
