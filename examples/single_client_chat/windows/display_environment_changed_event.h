#ifndef APPTRAVERSE_EXAMPLES_DISPLAY_ENVIRONMENT_CHANGED_EVENT_H_
#define APPTRAVERSE_EXAMPLES_DISPLAY_ENVIRONMENT_CHANGED_EVENT_H_

#include <cassert>
#include <cstdint>

#include "apptraverse/event_for.h"
#include "apptraverse/object_macros.h"

#include "windows_window.h"

namespace apptraverse {

// Local immediate event: clamps a candidate outer rectangle into a work area.
// Not shared. Not journaled. No timestamp. Resolution/DPI are not stored on
// WindowsWindow; dpi is informational for the presenter only.
class DisplayEnvironmentChangedEvent
    : public EventFor<WindowsWindow, DisplayEnvironmentChangedEvent> {
  APPTRAVERSE_OBJECT(DisplayEnvironmentChangedEvent, Event, 0)

 protected:
  DisplayEnvironmentChangedEvent() = default;

 public:
  explicit DisplayEnvironmentChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(work_left), AE_MMBR(work_top), AE_MMBR(work_right),
                    AE_MMBR(work_bottom), AE_MMBR(candidate_x),
                    AE_MMBR(candidate_y), AE_MMBR(candidate_width),
                    AE_MMBR(candidate_height), AE_MMBR(dpi))

  std::int32_t work_left{0};
  std::int32_t work_top{0};
  std::int32_t work_right{0};
  std::int32_t work_bottom{0};
  std::int32_t candidate_x{0};
  std::int32_t candidate_y{0};
  std::int32_t candidate_width{0};
  std::int32_t candidate_height{0};
  std::int32_t dpi{96};
};

inline void WindowsWindow::Apply(DisplayEnvironmentChangedEvent const& event) {
  std::int32_t const work_width = event.work_right - event.work_left;
  std::int32_t const work_height = event.work_bottom - event.work_top;
  assert(work_width > 0);
  assert(work_height > 0);

  std::int32_t next_width = event.candidate_width;
  std::int32_t next_height = event.candidate_height;
  std::int32_t next_x = event.candidate_x;
  std::int32_t next_y = event.candidate_y;

  if (next_width < 1) {
    next_width = 1;
  }
  if (next_height < 1) {
    next_height = 1;
  }
  if (next_width > work_width) {
    next_width = work_width;
  }
  if (next_height > work_height) {
    next_height = work_height;
  }

  if (next_x < event.work_left) {
    next_x = event.work_left;
  }
  if (next_y < event.work_top) {
    next_y = event.work_top;
  }
  if (next_x + next_width > event.work_right) {
    next_x = event.work_right - next_width;
  }
  if (next_y + next_height > event.work_bottom) {
    next_y = event.work_bottom - next_height;
  }

  if (x == next_x && y == next_y && width == next_width &&
      height == next_height) {
    return;
  }

  x = next_x;
  y = next_y;
  width = next_width;
  height = next_height;
}

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_DISPLAY_ENVIRONMENT_CHANGED_EVENT_H_
