#ifndef APPTRAVERSE_UI_UI_PRESENTED_WINDOW_H_
#define APPTRAVERSE_UI_UI_PRESENTED_WINDOW_H_

#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"

#include "apptraverse/ui/ui_window.h"
#include "apptraverse/ui/ui_window_presenter.h"

namespace apptraverse::ui {

class UiPresentedWindow : public NodeFor<UiPresentedWindow, UiWindow> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::UiPresentedWindow",
                           UiPresentedWindow, UiWindow, 0)

 protected:
  UiPresentedWindow() = default;

 public:
  explicit UiPresentedWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presenter);
  }

  ae::ObjPtr<UiWindowPresenter> presenter;
};

}  // namespace apptraverse::ui

#endif  // APPTRAVERSE_UI_UI_PRESENTED_WINDOW_H_
