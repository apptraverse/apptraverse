#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_PUSH_BUTTON_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_PUSH_BUTTON_PRESENTER_H_

#include <jni.h>

#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::ui::android {

struct AndroidPushButtonViewRefs;

class AndroidPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::android::AndroidPushButtonPresenter",
      AndroidPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  AndroidPushButtonPresenter() = default;

 public:
  explicit AndroidPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, button, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, button, window_presenter, layout_x, layout_y, layout_width,
        layout_height, control_id);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

 private:
  void SyncFromMirror();

  AndroidPushButtonViewRefs* view_refs_{nullptr};
};

void NotifyAndroidPushButtonClickedFromJava(std::int64_t native_presenter);

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_PUSH_BUTTON_PRESENTER_H_
