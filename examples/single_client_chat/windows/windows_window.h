#ifndef APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_
#define APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_

#include <cassert>
#include <cstdint>

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "model/window.h"
#include "model/window_changed_event.h"

namespace apptraverse {

// Win32 Window Node. Fields describe the outer top-level normal/restored
// rectangle in screen coordinates plus the last known density.
class WindowsWindow : public NodeFor<WindowsWindow, Window> {
  APPTRAVERSE_OBJECT(WindowsWindow, Window, 0)

 protected:
  WindowsWindow() = default;

 public:
  explicit WindowsWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(density_dpi))

  std::int32_t x{120};
  std::int32_t y{80};
  std::int32_t width{720};
  std::int32_t height{520};
  std::int32_t density_dpi{96};

  void Apply(WindowChangedEvent const& event) override {
    std::int32_t const available_width =
        event.available_right - event.available_left;
    std::int32_t const available_height =
        event.available_bottom - event.available_top;
    assert(available_width > 0);
    assert(available_height > 0);

    std::int32_t next_width = event.window_right - event.window_left;
    std::int32_t next_height = event.window_bottom - event.window_top;
    std::int32_t next_x = event.window_left;
    std::int32_t next_y = event.window_top;

    if (next_width < 1) {
      next_width = 1;
    }
    if (next_height < 1) {
      next_height = 1;
    }
    if (next_width > available_width) {
      next_width = available_width;
    }
    if (next_height > available_height) {
      next_height = available_height;
    }

    if (next_x < event.available_left) {
      next_x = event.available_left;
    }
    if (next_y < event.available_top) {
      next_y = event.available_top;
    }
    if (next_x + next_width > event.available_right) {
      next_x = event.available_right - next_width;
    }
    if (next_y + next_height > event.available_bottom) {
      next_y = event.available_bottom - next_height;
    }

    x = next_x;
    y = next_y;
    width = next_width;
    height = next_height;
    density_dpi = event.density_dpi;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_
