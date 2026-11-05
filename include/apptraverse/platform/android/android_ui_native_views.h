#ifndef APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_NATIVE_VIEWS_H_
#define APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_NATIVE_VIEWS_H_

namespace apptraverse::ui::android {

class AndroidDesktopWindowPresenter;
class AndroidPushButtonPresenter;
class AndroidListContainerPresenter;

// Native View side of the Android presenters. The presenter classes carry the
// serializable description (ClassId, fields, links) and build on any host; the
// JNI implementation is supplied at runtime as Presenter::presentation_host.
class AndroidUiNativeViews {
 public:
  virtual void LoadDesktopWindow(AndroidDesktopWindowPresenter& presenter) = 0;
  virtual void SyncDesktopWindow(AndroidDesktopWindowPresenter& presenter) = 0;
  virtual void UnloadDesktopWindow(AndroidDesktopWindowPresenter& presenter) = 0;

  virtual void LoadPushButton(AndroidPushButtonPresenter& presenter) = 0;
  virtual void SyncPushButton(AndroidPushButtonPresenter& presenter) = 0;
  virtual void UnloadPushButton(AndroidPushButtonPresenter& presenter) = 0;

  virtual void LoadListContainer(AndroidListContainerPresenter& presenter) = 0;
  virtual void SyncListContainer(AndroidListContainerPresenter& presenter) = 0;
  virtual void UnloadListContainer(AndroidListContainerPresenter& presenter) = 0;

 protected:
  ~AndroidUiNativeViews() = default;
};

}  // namespace apptraverse::ui::android

#endif  // APPTRAVERSE_PLATFORM_ANDROID_ANDROID_UI_NATIVE_VIEWS_H_
