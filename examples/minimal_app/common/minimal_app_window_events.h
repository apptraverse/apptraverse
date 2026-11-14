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

class MinimalViewportEvent : public EventFor<MinimalApplication, MinimalViewportEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::minimal::MinimalViewportEvent",
      MinimalViewportEvent, Event, 0)
 protected:
  MinimalViewportEvent() = default;
 public:
  explicit MinimalViewportEvent(ae::ObjProp prop) : EventFor{prop} {}
  std::int32_t width, height, dpi;
  bool requires_single_page;
  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height), AE_MMBR(dpi), AE_MMBR(requires_single_page))
};
class MinimalDesktopEnvironmentEvent : public EventFor<MinimalApplication, MinimalDesktopEnvironmentEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::minimal::MinimalDesktopEnvironmentEvent",
      MinimalDesktopEnvironmentEvent, Event, 0)
 protected:
  MinimalDesktopEnvironmentEvent() = default;
 public:
  explicit MinimalDesktopEnvironmentEvent(ae::ObjProp prop) : EventFor{prop} {}
  std::int32_t dpi;
  AE_OBJECT_REFLECT(AE_MMBR(dpi))
};

}  // namespace apptraverse::example::minimal

#endif  // APPTRAVERSE_EXAMPLE_MINIMAL_MINIMAL_APP_WINDOW_EVENTS_H_
