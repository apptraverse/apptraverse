#ifndef APPTRAVERSE_EXAMPLES_APPLE_WINDOW_H_
#define APPTRAVERSE_EXAMPLES_APPLE_WINDOW_H_

#include <cstdint>

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "model/window.h"
#include "model/window_changed_event.h"

namespace apptraverse {

class AppleWindow : public NodeFor<AppleWindow, Window> {
  APPTRAVERSE_OBJECT(AppleWindow, Window, 0)

 protected:
  AppleWindow() = default;

 public:
  explicit AppleWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height), AE_MMBR(density_dpi))

  std::int32_t width{0};
  std::int32_t height{0};
  std::int32_t density_dpi{72};

  void Apply(WindowChangedEvent const& event) override {
    std::int32_t const next_width = event.window_right - event.window_left;
    std::int32_t const next_height = event.window_bottom - event.window_top;
    if (next_width > 0) {
      width = next_width;
    }
    if (next_height > 0) {
      height = next_height;
    }
    density_dpi = event.density_dpi;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_APPLE_WINDOW_H_
