#ifndef APPTRAVERSE_EXAMPLES_LINUX_WINDOW_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_LINUX_WINDOW_PRESENTER_H_

#include "apptraverse/object_macros.h"
#include "model/window_presenter.h"

#include "linux_chat_presenter.h"
#include "linux_window.h"

namespace apptraverse {

class LinuxWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(LinuxWindowPresenter, WindowPresenter, 0)

 protected:
  LinuxWindowPresenter() = default;

 public:
  explicit LinuxWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  LinuxChatPresenter* LoadLinuxChatPresenter() {
    if (!window.is_valid() || !chat_presenter.is_valid()) {
      return nullptr;
    }
    window.Load();
    if (!window.is_loaded() || window->GetClassId() != LinuxWindow::kClassId) {
      return nullptr;
    }
    chat_presenter.Load();
    if (!chat_presenter.is_loaded() ||
        chat_presenter->GetClassId() != LinuxChatPresenter::kClassId) {
      return nullptr;
    }
    return &static_cast<LinuxChatPresenter&>(*chat_presenter);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_LINUX_WINDOW_PRESENTER_H_
