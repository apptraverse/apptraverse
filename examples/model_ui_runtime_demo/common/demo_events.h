#ifndef APPTRAVERSE_DEMO_EVENTS_H_
#define APPTRAVERSE_DEMO_EVENTS_H_

#include <cstdint>

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"
#include "demo_model.h"

namespace apptraverse {

class WindowBoundsChangedEvent
    : public EventFor<Window, WindowBoundsChangedEvent> {
  APPTRAVERSE_OBJECT(WindowBoundsChangedEvent, Event, 0)

 protected:
  WindowBoundsChangedEvent() = default;

 public:
  explicit WindowBoundsChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(left), AE_MMBR(top), AE_MMBR(right),
                    AE_MMBR(bottom), AE_MMBR(client_width),
                    AE_MMBR(client_height))

  std::int32_t left{0};
  std::int32_t top{0};
  std::int32_t right{0};
  std::int32_t bottom{0};
  std::int32_t client_width{0};
  std::int32_t client_height{0};
};

class ColorChangedEvent : public EventFor<ColorToolbar, ColorChangedEvent> {
  APPTRAVERSE_OBJECT(ColorChangedEvent, Event, 0)

 protected:
  ColorChangedEvent() = default;

 public:
  explicit ColorChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(color))

  std::uint32_t color{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_DEMO_EVENTS_H_
