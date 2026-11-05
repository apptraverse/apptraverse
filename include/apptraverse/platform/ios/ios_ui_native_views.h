#ifndef APPTRAVERSE_PLATFORM_IOS_IOS_UI_NATIVE_VIEWS_H_
#define APPTRAVERSE_PLATFORM_IOS_IOS_UI_NATIVE_VIEWS_H_

namespace apptraverse::ui::ios {

class IosDesktopWindowPresenter;
class IosPushButtonPresenter;
class IosListContainerPresenter;

class IosUiNativeViews {
 public:
  virtual void LoadDesktopWindow(IosDesktopWindowPresenter& presenter) = 0;
  virtual void SyncDesktopWindow(IosDesktopWindowPresenter& presenter) = 0;
  virtual void UnloadDesktopWindow(IosDesktopWindowPresenter& presenter) = 0;

  virtual void LoadPushButton(IosPushButtonPresenter& presenter) = 0;
  virtual void SyncPushButton(IosPushButtonPresenter& presenter) = 0;
  virtual void UnloadPushButton(IosPushButtonPresenter& presenter) = 0;

  virtual void LoadListContainer(IosListContainerPresenter& presenter) = 0;
  virtual void SyncListContainer(IosListContainerPresenter& presenter) = 0;
  virtual void UnloadListContainer(IosListContainerPresenter& presenter) = 0;

 protected:
  ~IosUiNativeViews() = default;
};

}  // namespace apptraverse::ui::ios

#endif  // APPTRAVERSE_PLATFORM_IOS_IOS_UI_NATIVE_VIEWS_H_
