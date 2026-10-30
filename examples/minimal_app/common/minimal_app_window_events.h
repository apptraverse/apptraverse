#ifndef APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_WINDOW_EVENTS_H_
#define APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_WINDOW_EVENTS_H_

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::example::minimal {

class MinimalApplication;

class MinimalOpenAuxiliaryWindowEvent
    : public EventFor<MinimalApplication, MinimalOpenAuxiliaryWindowEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::minimal::MinimalOpenAuxiliaryWindowEvent",
      MinimalOpenAuxiliaryWindowEvent, Event, 0)

 protected:
  MinimalOpenAuxiliaryWindowEvent() = default;

 public:
  explicit MinimalOpenAuxiliaryWindowEvent(ae::ObjProp prop) : EventFor{prop} {}
};

class MinimalCloseAuxiliaryWindowEvent
    : public EventFor<MinimalApplication, MinimalCloseAuxiliaryWindowEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::minimal::MinimalCloseAuxiliaryWindowEvent",
      MinimalCloseAuxiliaryWindowEvent, Event, 0)

 protected:
  MinimalCloseAuxiliaryWindowEvent() = default;

 public:
  explicit MinimalCloseAuxiliaryWindowEvent(ae::ObjProp prop)
      : EventFor{prop} {}
};

void EnsureMinimalAppWindowEventsRegistration();

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_WINDOW_EVENTS_H_
