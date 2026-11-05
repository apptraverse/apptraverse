#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_

#include <jni.h>

#include "apptraverse/platform/android/android_ui_native_views.h"

namespace apptraverse::ui::android {

// JNI View implementation of the Android presenters. Passed as
// Presenter::presentation_host through its AndroidUiNativeViews base (see
// presenter_host()). Not serialized.
struct AndroidPresentationHost final : AndroidUiNativeViews {
  JavaVM* vm{nullptr};
  jobject activity{nullptr};
  jobject content_root{nullptr};

  void* presenter_host() { return static_cast<AndroidUiNativeViews*>(this); }

  void LoadDesktopWindow(AndroidDesktopWindowPresenter& presenter) override;
  void SyncDesktopWindow(AndroidDesktopWindowPresenter& presenter) override;
  void UnloadDesktopWindow(AndroidDesktopWindowPresenter& presenter) override;

  void LoadPushButton(AndroidPushButtonPresenter& presenter) override;
  void SyncPushButton(AndroidPushButtonPresenter& presenter) override;
  void UnloadPushButton(AndroidPushButtonPresenter& presenter) override;

  void LoadListContainer(AndroidListContainerPresenter& presenter) override;
  void SyncListContainer(AndroidListContainerPresenter& presenter) override;
  void UnloadListContainer(AndroidListContainerPresenter& presenter) override;

 private:
  static jobject ClientMount(AndroidDesktopWindowPresenter const& desktop);
};

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_PRESENTATION_HOST_H_
