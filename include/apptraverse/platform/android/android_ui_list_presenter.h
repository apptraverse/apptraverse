#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_LIST_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_LIST_PRESENTER_H_

#include <jni.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::android {

struct AndroidListContainerViewRefs;

class AndroidListContainerPresenter : public UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::ui::android::AndroidListContainerPresenter",
      AndroidListContainerPresenter, UiListContainerPresenter, 0)

 protected:
  AndroidListContainerPresenter() = default;

 public:
  explicit AndroidListContainerPresenter(ae::ObjProp prop)
      : UiListContainerPresenter{prop} {}

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

  void HandleRowClickFromJava(int row_index);

 private:
  void SyncFromMirror();

  AndroidListContainerViewRefs* view_refs_{nullptr};
  bool applying_mirror_sync_{false};
};

void NotifyAndroidListSelectionFromJava(std::int64_t native_presenter,
                                        jint row_index);

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_LIST_PRESENTER_H_
