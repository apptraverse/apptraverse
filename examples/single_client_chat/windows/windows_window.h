#ifndef APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_
#define APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_

#include <cstdint>

#include "apptraverse/object_macros.h"
#include "apptraverse/window.h"

namespace apptraverse {

class WindowBoundsChangedEvent;
class DisplayEnvironmentChangedEvent;

// Platform Window model for Win32. Fields describe the outer top-level
// normal/restored rectangle in screen coordinates. Minimized/maximized show
// state is not persisted.
class WindowsWindow : public Window {
  APPTRAVERSE_OBJECT(WindowsWindow, Window, 0)

 protected:
  WindowsWindow() = default;

 public:
  explicit WindowsWindow(ae::ObjProp prop) : Window{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(x), AE_MMBR(y), AE_MMBR(width), AE_MMBR(height))

  std::int32_t x{100};
  std::int32_t y{100};
  std::int32_t width{640};
  std::int32_t height{480};

  void Apply(WindowBoundsChangedEvent const& event);
  void Apply(DisplayEnvironmentChangedEvent const& event);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_
