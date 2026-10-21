#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {

class WinDesktopWindowPresenter;

class WinPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinPushButtonPresenter",
                           WinPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  WinPushButtonPresenter() = default;

 public:
  explicit WinPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop} {}

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  bool PresentationCreateSucceeded() const override { return hwnd_ != nullptr; }

  HWND hwnd() const { return hwnd_; }

 private:
  void SyncFromMirror();
  WinDesktopWindowPresenter* Desktop() const;

  HWND hwnd_{nullptr};
};

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_
