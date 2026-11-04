#ifndef APPTRAVERSE_UI_PRESENTED_WINDOW_LIFECYCLE_H_
#define APPTRAVERSE_UI_PRESENTED_WINDOW_LIFECYCLE_H_

#include <type_traits>

#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse::ui {

template <typename WindowT>
void SavePresentedWindow(ae::ObjPtr<WindowT>& window) {
  static_assert(std::is_base_of_v<UiPresentedWindow, WindowT>);
  window.Save();
}

template <typename WindowT>
void UnloadPresentedWindow(ae::ObjPtr<WindowT>& window) {
  static_assert(std::is_base_of_v<UiPresentedWindow, WindowT>);
  SavePresentedWindow(window);
  window->presenter.Reset();
  window.Save();
  window.Reset();
}

template <typename WindowT>
void LoadPresentedWindow(ae::ObjPtr<WindowT>& window) {
  static_assert(std::is_base_of_v<UiPresentedWindow, WindowT>);
  window.Load();
  window->presenter.Load();
}

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_PRESENTED_WINDOW_LIFECYCLE_H_
