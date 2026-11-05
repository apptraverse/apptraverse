#ifndef APPTRAVERSE_PLATFORM_MACOS_MACOS_UI_NATIVE_VIEWS_H_
#define APPTRAVERSE_PLATFORM_MACOS_MACOS_UI_NATIVE_VIEWS_H_

namespace apptraverse::ui::macos {

class MacDesktopWindowPresenter;
class MacPushButtonPresenter;
class MacListContainerPresenter;

class MacUiNativeViews {
 public:
  virtual void LoadDesktopWindow(MacDesktopWindowPresenter& presenter) = 0;
  virtual void SyncDesktopWindow(MacDesktopWindowPresenter& presenter) = 0;
  virtual void UnloadDesktopWindow(MacDesktopWindowPresenter& presenter) = 0;

  virtual void LoadPushButton(MacPushButtonPresenter& presenter) = 0;
  virtual void SyncPushButton(MacPushButtonPresenter& presenter) = 0;
  virtual void UnloadPushButton(MacPushButtonPresenter& presenter) = 0;

  virtual void LoadListContainer(MacListContainerPresenter& presenter) = 0;
  virtual void SyncListContainer(MacListContainerPresenter& presenter) = 0;
  virtual void UnloadListContainer(MacListContainerPresenter& presenter) = 0;

 protected:
  ~MacUiNativeViews() = default;
};

}  // namespace apptraverse::ui::macos

#endif  // APPTRAVERSE_PLATFORM_MACOS_MACOS_UI_NATIVE_VIEWS_H_
