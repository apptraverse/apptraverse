#ifndef APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_H_

#include "apptraverse/object_macros.h"
#include "apptraverse/window.h"

namespace apptraverse {

// Android platform layer of Window. The Android framework owns the window
// geometry and the Activity lifecycle, so nothing platform specific is
// reflected into the persisted graph.
class AndroidWindow : public Window {
  APPTRAVERSE_OBJECT(AndroidWindow, Window, 0)

 protected:
  AndroidWindow() = default;

 public:
  explicit AndroidWindow(ae::ObjProp prop) : Window{prop} {}

  AE_OBJECT_REFLECT()
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_H_
