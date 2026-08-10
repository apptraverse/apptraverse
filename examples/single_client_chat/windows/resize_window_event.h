#ifndef APPTRAVERSE_EXAMPLES_RESIZE_WINDOW_EVENT_H_
#define APPTRAVERSE_EXAMPLES_RESIZE_WINDOW_EVENT_H_

#include <cstdint>

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"

#include "windows_window.h"

namespace apptraverse {

class ResizeWindowEvent
    : public EventFor<WindowsWindow, ResizeWindowEvent> {
  APPTRAVERSE_OBJECT(ResizeWindowEvent, Event, 0)

 protected:
  ResizeWindowEvent() = default;

 public:
  explicit ResizeWindowEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height))

  std::int32_t width{0};
  std::int32_t height{0};
};

inline void WindowsWindow::Apply(ResizeWindowEvent const& event) {
  width = event.width;
  height = event.height;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_RESIZE_WINDOW_EVENT_H_
