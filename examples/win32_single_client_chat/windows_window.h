#ifndef APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_
#define APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_

#include <cstdint>

#include "apptraverse/window.h"

namespace apptraverse {

class ResizeWindowEvent;

class WindowsWindow : public Window {
  AE_OBJECT(WindowsWindow, Window, 0)

 protected:
  WindowsWindow() = default;

 public:
  explicit WindowsWindow(ae::ObjProp prop) : Window{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height))

  std::int32_t width{640};
  std::int32_t height{480};

  void Apply(ResizeWindowEvent const& event);
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WINDOWS_WINDOW_H_
