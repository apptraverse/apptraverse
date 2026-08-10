#ifndef APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_H_

#include <cassert>
#include <cstdint>

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/window.h"
#include "apptraverse/window_changed_event.h"

namespace apptraverse {

// Android Window Node. Geometry is the Activity content viewport; there is no
// desktop-style window position.
class AndroidWindow : public NodeFor<AndroidWindow, Window> {
  APPTRAVERSE_OBJECT(AndroidWindow, Window, 0)

 protected:
  AndroidWindow() = default;

 public:
  explicit AndroidWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(viewport_width), AE_MMBR(viewport_height),
                    AE_MMBR(density_dpi))

  std::int32_t viewport_width{0};
  std::int32_t viewport_height{0};
  std::int32_t density_dpi{160};

  void Apply(WindowChangedEvent const& event) override {
    std::int32_t const width = event.available_right - event.available_left;
    std::int32_t const height = event.available_bottom - event.available_top;
    assert(width > 0);
    assert(height > 0);
    viewport_width = width;
    viewport_height = height;
    density_dpi = event.density_dpi;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_H_
