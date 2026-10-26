#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {

class WinPushButtonPresenter : public UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinPushButtonPresenter",
                           WinPushButtonPresenter, UiPushButtonPresenter, 0)

 protected:
  WinPushButtonPresenter() : hwnd_{nullptr} {}

 public:
  explicit WinPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop}, hwnd_{nullptr} {}

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  bool PresentationCreateSucceeded() const override { return hwnd_ != nullptr; }

  HWND hwnd() const { return hwnd_; }

#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
  static int TestSetWindowPosCount();
  static void TestResetSetWindowPosCount();
#endif

 private:
  void SyncFromMirror();
  void ApplyLayout();

  HWND hwnd_;

#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
  static int test_set_window_pos_count_;
#endif
};

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PUSH_BUTTON_PRESENTER_H_
