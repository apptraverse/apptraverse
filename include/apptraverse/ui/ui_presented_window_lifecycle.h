#ifndef APPTRAVERSE_UI_PRESENTED_WINDOW_LIFECYCLE_H_
#define APPTRAVERSE_UI_PRESENTED_WINDOW_LIFECYCLE_H_

#include <cstdlib>
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
  // Retain the presenter's private leaves while saving the window with a lazy
  // presenter link. One graph walk visits shared layers only once.
  auto presenter = window->presenter;
  if (presenter.domain() != window.domain()) {
    // A shared presenter in an ancestor retains its original storage owner.
    presenter.Save();
    window->presenter.Reset();
    window.Save();
    window.Reset();
    return;
  }
  window->presenter.Reset();
  ae::DomainGraph graph{window.domain()};
  auto const window_saved = graph.SaveRoot(window.cached(), window.id());
  if (window_saved.IsErr()) {
    std::abort();
  }
  if (presenter.cached()) {
    auto const presenter_saved =
        graph.SaveRoot(presenter.cached(), presenter.id());
    if (presenter_saved.IsErr()) {
      std::abort();
    }
  }
  presenter.Reset();
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
