#ifndef APPTRAVERSE_EXAMPLES_WINDOW_BOUNDS_CHANGED_EVENT_H_
#define APPTRAVERSE_EXAMPLES_WINDOW_BOUNDS_CHANGED_EVENT_H_

#include <cstdint>

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"

#include "windows_window.h"

namespace apptraverse {

// Local immediate event: updates the stored normal outer rectangle after a
// finished user move/resize. Not shared. Not journaled. No timestamp.
class WindowBoundsChangedEvent
    : public EventFor<WindowsWindow, WindowBoundsChangedEvent> {
  APPTRAVERSE_OBJECT(WindowBoundsChangedEvent, Event, 0)

 protected:
  WindowBoundsChangedEvent() = default;

 public:
  explicit WindowBoundsChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height))

  std::int32_t x{0};
  std::int32_t y{0};
  std::int32_t width{0};
  std::int32_t height{0};
};

inline void WindowsWindow::Apply(WindowBoundsChangedEvent const& event) {
  if (x == event.x && y == event.y && width == event.width &&
      height == event.height) {
    return;
  }
  x = event.x;
  y = event.y;
  width = event.width;
  height = event.height;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WINDOW_BOUNDS_CHANGED_EVENT_H_
