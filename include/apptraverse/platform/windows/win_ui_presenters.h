#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::ui::windows {

class WinDesktopWindowPresenter : public UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinDesktopWindowPresenter",
                           WinDesktopWindowPresenter,
                           UiDesktopWindowPresenter, 0)

 protected:
  WinDesktopWindowPresenter() = default;

 public:
  explicit WinDesktopWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop} {}

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  HWND hwnd() const { return hwnd_; }

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);

 private:
  void SyncNativeFrameFromMirror();
  void HandleNativeFrameChanged(RECT const& frame);

  HWND hwnd_{nullptr};
  bool applying_native_frame_{false};
  std::uint64_t suppress_native_echo_until_{0};
};

class WinEditBoxPresenter : public UiEditBoxPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinEditBoxPresenter",
                           WinEditBoxPresenter, UiEditBoxPresenter, 0)

 protected:
  WinEditBoxPresenter() = default;

 public:
  explicit WinEditBoxPresenter(ae::ObjProp prop) : UiEditBoxPresenter{prop} {}

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

  static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                           LPARAM lparam, UINT_PTR id,
                                           DWORD_PTR ref_data);

 private:
  void SyncFromMirror();
  void ReadFromNativeAndSubmit();

  HWND hwnd_{nullptr};
  bool applying_mirror_text_{false};
};

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
};

class WinLabelPresenter : public UiLabelPresenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinLabelPresenter",
                           WinLabelPresenter, UiLabelPresenter, 0)

 protected:
  WinLabelPresenter() = default;

 public:
  explicit WinLabelPresenter(ae::ObjProp prop) : UiLabelPresenter{prop} {}

  void OnLoad() override;
  void OnModelChanged() override;
  void OnUnload() override;

 private:
  HWND hwnd_{nullptr};
};

void EnsureWinUiPresenterRegistration();

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_
