#ifndef APPTRAVERSE_WINDOW_CHANGED_EVENT_H_
#define APPTRAVERSE_WINDOW_CHANGED_EVENT_H_

#include <cstdint>

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/window.h"

namespace apptraverse {

// Normalized display/window environment change. available_* is the usable
// rectangle (monitor work area / Android Activity content). window_* is the
// candidate outer window (Win32) or the current viewport (Android).
class WindowChangedEvent
    : public EventFor<Window, WindowChangedEvent> {
  APPTRAVERSE_OBJECT(WindowChangedEvent, Event, 0)

 protected:
  WindowChangedEvent() = default;

 public:
  explicit WindowChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(available_left), AE_MMBR(available_top),
                    AE_MMBR(available_right), AE_MMBR(available_bottom),
                    AE_MMBR(window_left), AE_MMBR(window_top),
                    AE_MMBR(window_right), AE_MMBR(window_bottom),
                    AE_MMBR(density_dpi))

  std::int32_t available_left{0};
  std::int32_t available_top{0};
  std::int32_t available_right{0};
  std::int32_t available_bottom{0};
  std::int32_t window_left{0};
  std::int32_t window_top{0};
  std::int32_t window_right{0};
  std::int32_t window_bottom{0};
  std::int32_t density_dpi{96};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CHANGED_EVENT_H_
