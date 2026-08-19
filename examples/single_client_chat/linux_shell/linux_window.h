#ifndef APPTRAVERSE_EXAMPLES_LINUX_WINDOW_H_
#define APPTRAVERSE_EXAMPLES_LINUX_WINDOW_H_

#include <algorithm>
#include <cstdint>

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "model/window.h"
#include "model/window_changed_event.h"

namespace apptraverse {

// GTK3 Window Node. Fields describe the outer top-level window rectangle.
class LinuxWindow : public NodeFor<LinuxWindow, Window> {
  APPTRAVERSE_OBJECT(LinuxWindow, Window, 0)

 protected:
  LinuxWindow() = default;

 public:
  explicit LinuxWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height),
                    AE_MMBR(density_dpi))

  std::int32_t x{120};
  std::int32_t y{80};
  std::int32_t width{720};
  std::int32_t height{520};
  std::int32_t density_dpi{96};

  void Apply(WindowChangedEvent const& event) override {
    std::int32_t next_width = event.window_right - event.window_left;
    std::int32_t next_height = event.window_bottom - event.window_top;
    x = event.window_left;
    y = event.window_top;
    width = std::max<std::int32_t>(1, next_width);
    height = std::max<std::int32_t>(1, next_height);
    density_dpi = event.density_dpi;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_LINUX_WINDOW_H_
