#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN

#endif

#include <windows.h>

#include "apptraverse/ui/ui_bitmap_image.h"

#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"

namespace apptraverse::ui::windows {

class WinWindowPresenter : public UiWindowPresenter {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinWindowPresenter",

                           WinWindowPresenter, UiWindowPresenter, 0)

 protected:

  WinWindowPresenter() = default;

 public:

  explicit WinWindowPresenter(ae::ObjProp prop) : UiWindowPresenter{prop} {}

};

class WinDesktopWindowPresenter : public UiDesktopWindowPresenter {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinDesktopWindowPresenter",

                           WinDesktopWindowPresenter,

                           UiDesktopWindowPresenter, 0)

 protected:

  WinDesktopWindowPresenter() : hwnd_{nullptr}, applying_native_frame_{false}, interactive_move_active_{false} {
#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
    test_model_frame_submit_count_ = 0;
#endif
  }

 public:

  explicit WinDesktopWindowPresenter(ae::ObjProp prop)

      : UiDesktopWindowPresenter{prop}, hwnd_{nullptr}, applying_native_frame_{false}, interactive_move_active_{false} {
#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
    test_model_frame_submit_count_ = 0;
#endif
  }

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

  void ApplyRequestedFrameFromMirror() override;

  bool PresentationCreateSucceeded() const override { return hwnd_ != nullptr; }

  void* NativeChildParentHandle() const override {
    return reinterpret_cast<void*>(hwnd_);
  }

  HWND hwnd() const { return hwnd_; }

  bool RouteChildCommand(std::uint32_t command_id,

                         std::uint16_t notification_code, HWND child);

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,

                                  LPARAM lparam);

 private:

  struct ClientFrame {

    std::int32_t x;

    std::int32_t y;

    std::int32_t client_width;

    std::int32_t client_height;

  };

  void HandleNativeFrameChanged();

  std::wstring ResolveTitleText() const;

  ClientFrame ReadMirrorClientFrame() const;

  ClientFrame ReadNativeClientFrame() const;

  void TrackInteractiveNativeFrame();

  static bool FramesEqual(ClientFrame a, ClientFrame b);

  HWND hwnd_;

  bool applying_native_frame_;

  bool interactive_move_active_;

#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)

 public:

  static int TestSetWindowPosCount();

  static void TestResetSetWindowPosCount();

  bool TestInteractiveMoveActive() const { return interactive_move_active_; }

  void TestSetInteractiveMoveActive(bool active) {

    interactive_move_active_ = active;

  }

  void TestReportNativeFrame(std::int32_t x, std::int32_t y,

                             std::int32_t client_width,

                             std::int32_t client_height);

  void TestMoveNativeOuter(std::int32_t x, std::int32_t y,

                           std::int32_t client_width,

                           std::int32_t client_height);

  void TestSimulateInteractiveNativeMove(std::int32_t x, std::int32_t y,

                                         std::int32_t client_width,

                                         std::int32_t client_height);

  void TestFinishInteractiveNativeMove();

  int TestModelFrameSubmitCount() const { return test_model_frame_submit_count_; }

  void TestResetModelFrameSubmitCount() { test_model_frame_submit_count_ = 0; }

 private:

  static int test_set_window_pos_count_;

  int test_model_frame_submit_count_;

#endif

};

class WinEditBoxPresenter : public UiEditBoxPresenter {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinEditBoxPresenter",

                           WinEditBoxPresenter, UiEditBoxPresenter, 0)

 protected:

  WinEditBoxPresenter() : hwnd_{nullptr}, applying_mirror_text_{false}, last_applied_generation_{0}, mirror_text_applied_{false} {}

 public:

  explicit WinEditBoxPresenter(ae::ObjProp prop) : UiEditBoxPresenter{prop}, hwnd_{nullptr}, applying_mirror_text_{false}, last_applied_generation_{0}, mirror_text_applied_{false} {}

  void OnLoad() override;

  void OnModelChanged() override;

  void OnUnload() override;

  bool PresentationCreateSucceeded() const override { return hwnd_ != nullptr; }

  HWND hwnd() const { return hwnd_; }

  void ReadFromNativeAndSubmit();
  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

 private:

  void SyncFromMirror();

  WinDesktopWindowPresenter* Desktop() const;

  HWND hwnd_;

  bool applying_mirror_text_;

  std::uint64_t last_applied_generation_;

  bool mirror_text_applied_;

};

class WinListContainerPresenter : public UiListContainerPresenter {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinListContainerPresenter",

                           WinListContainerPresenter, UiListContainerPresenter,

                           0)

 protected:

  WinListContainerPresenter() : hwnd_{nullptr} {}

 public:

  explicit WinListContainerPresenter(ae::ObjProp prop)

      : UiListContainerPresenter{prop}, hwnd_{nullptr} {}

  void OnLoad() override;

  void OnModelChanged() override;

  void OnUnload() override;

  bool PresentationCreateSucceeded() const override { return hwnd_ != nullptr; }

  HWND hwnd() const { return hwnd_; }

  bool HandleListNotification(std::uint16_t notification_code);

 private:

  void SyncFromMirror();

  WinDesktopWindowPresenter* Desktop() const;

  HWND hwnd_;

};

class WinLabelPresenter : public UiLabelPresenter {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinLabelPresenter",

                           WinLabelPresenter, UiLabelPresenter, 0)

 protected:

  WinLabelPresenter() : hwnd_{nullptr} {}

 public:

  explicit WinLabelPresenter(ae::ObjProp prop) : UiLabelPresenter{prop}, hwnd_{nullptr} {}

  void OnLoad() override;

  void OnModelChanged() override;

  void OnUnload() override;

  bool PresentationCreateSucceeded() const override { return hwnd_ != nullptr; }

 private:

  void SyncFromMirror();

  WinDesktopWindowPresenter* Desktop() const;

  HWND hwnd_;

};

class WinBitmapImagePresenter : public UiBitmapImagePresenter {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::ui::windows::WinBitmapImagePresenter",

                           WinBitmapImagePresenter, UiBitmapImagePresenter, 0)

 protected:

  WinBitmapImagePresenter() : hwnd_{nullptr} {}

 public:

  explicit WinBitmapImagePresenter(ae::ObjProp prop)

      : UiBitmapImagePresenter{prop}, hwnd_{nullptr} {}

  void OnLoad() override;

  void OnModelChanged() override;

  void OnUnload() override;

  bool PresentationCreateSucceeded() const override { return hwnd_ != nullptr; }

  void PaintToHdc(HDC hdc) const;

 private:

  WinDesktopWindowPresenter* Desktop() const;

  HWND hwnd_;

};

void EnsureWinUiPresenterRegistration();
void EnsureExtendedWinUiPresenterRegistration();

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

