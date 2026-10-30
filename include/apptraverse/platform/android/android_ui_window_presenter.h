#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_

#include <jni.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::android {

struct AndroidDesktopWindowViewRefs;

class AndroidDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::android::AndroidDesktopWindowPresenter",
      AndroidDesktopWindowPresenter, UiDesktopWindowPresenter, 0)

 protected:
  AndroidDesktopWindowPresenter() = default;

 public:
  explicit AndroidDesktopWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  jobject ClientMount() const;

 private:
  void SyncTitleFromMirror();

  AndroidDesktopWindowViewRefs* view_refs_{nullptr};
};

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_WINDOW_PRESENTER_H_
