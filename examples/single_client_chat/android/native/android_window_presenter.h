#ifndef APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_PRESENTER_H_

#include "apptraverse/object_macros.h"
#include "apptraverse/window_presenter.h"

#include "android_chat_presenter.h"
#include "android_window.h"

namespace apptraverse {

class AndroidWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(AndroidWindowPresenter, WindowPresenter, 0)

 protected:
  AndroidWindowPresenter() = default;

 public:
  explicit AndroidWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  // Loads the Android presenter layer. Returns nullptr when the stored graph
  // does not belong to this platform.
  AndroidChatPresenter* LoadAndroidChatPresenter() {
    if (!window.is_valid() || !chat_presenter.is_valid()) {
      return nullptr;
    }
    window.Load();
    if (!window.is_loaded() ||
        window->GetClassId() != AndroidWindow::kClassId) {
      return nullptr;
    }
    chat_presenter.Load();
    if (!chat_presenter.is_loaded() ||
        chat_presenter->GetClassId() != AndroidChatPresenter::kClassId) {
      return nullptr;
    }
    return &static_cast<AndroidChatPresenter&>(*chat_presenter);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_PRESENTER_H_
