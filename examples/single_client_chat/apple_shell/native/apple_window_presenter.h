#ifndef APPTRAVERSE_EXAMPLES_APPLE_WINDOW_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_APPLE_WINDOW_PRESENTER_H_

#include "apptraverse/object_macros.h"
#include "model/window_presenter.h"

#include "apple_chat_presenter.h"
#include "apple_window.h"

namespace apptraverse {

class AppleWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(AppleWindowPresenter, WindowPresenter, 0)

 protected:
  AppleWindowPresenter() = default;

 public:
  explicit AppleWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  AppleChatPresenter* LoadAppleChatPresenter() {
    if (!window.is_valid() || !chat_presenter.is_valid()) {
      return nullptr;
    }
    window.Load();
    if (!window.is_loaded() || window->GetClassId() != AppleWindow::kClassId) {
      return nullptr;
    }
    chat_presenter.Load();
    if (!chat_presenter.is_loaded() ||
        chat_presenter->GetClassId() != AppleChatPresenter::kClassId) {
      return nullptr;
    }
    return &static_cast<AppleChatPresenter&>(*chat_presenter);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_APPLE_WINDOW_PRESENTER_H_
