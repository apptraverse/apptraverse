#ifndef APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_PRESENTER_H_

#include <cassert>
#include <cstdint>

#include "apptraverse/object_macros.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

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

  void CommitViewport(std::int32_t width, std::int32_t height,
                      std::int32_t density_dpi) {
    assert(width > 0);
    assert(height > 0);
    assert(window.is_valid());
    window.Load();
    assert(window.is_loaded());
    assert(window->GetClassId() == AndroidWindow::kClassId);

    auto event =
        WindowChangedEvent::ptr::Create(ae::CreateWith{*window.domain()});
    event->available_left = 0;
    event->available_top = 0;
    event->available_right = width;
    event->available_bottom = height;
    event->window_left = 0;
    event->window_top = 0;
    event->window_right = width;
    event->window_bottom = height;
    event->density_dpi = density_dpi;

    window->Commit(event);
    window.Save();
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_ANDROID_WINDOW_PRESENTER_H_
