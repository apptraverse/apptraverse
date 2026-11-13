#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN

#endif

#include <windows.h>


#include "apptraverse/ui/ui_native_presenter_schema.h"
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

  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kWindow,

                           WinDesktopWindowPresenter,

                           UiDesktopWindowPresenter, 0)

 protected:

  WinDesktopWindowPresenter() : hwnd_{nullptr}, applying_native_frame_{false}, interactive_move_active_{false} {}

 public:

  explicit WinDesktopWindowPresenter(ae::ObjProp prop)

      : UiDesktopWindowPresenter{prop}, hwnd_{nullptr}, applying_native_frame_{false}, interactive_move_active_{false} {}

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

  void HandleNativeFrameChanged(bool updates_restore_geometry = false);

  std::wstring ResolveTitleText() const;

  ClientFrame ReadMirrorClientFrame() const;

  ClientFrame ReadNativeClientFrame() const;

  void TrackInteractiveNativeFrame();

  static bool FramesEqual(ClientFrame a, ClientFrame b);

  HWND hwnd_;

  bool applying_native_frame_;

  bool interactive_move_active_;

  std::int32_t last_native_client_width_{-1};

  std::int32_t last_native_client_height_{-1};

};

class WinListContainerPresenter : public UiListContainerPresenter {

  APPTRAVERSE_NAMED_OBJECT(::apptraverse::ui::native_schema::kList,

                           WinListContainerPresenter, UiListContainerPresenter,

                           0)

 protected:

  WinListContainerPresenter() : hwnd_{nullptr} {}

 public:

  explicit WinListContainerPresenter(ae::ObjProp prop)

      : UiListContainerPresenter{prop}, hwnd_{nullptr} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) { dnv(base_); }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const { dnv(base_); }

  void OnLoad() override;

  void OnModelChanged() override;

  void OnUnload() override;

  HWND hwnd() const { return hwnd_; }

  bool OnCommand(std::uint32_t command_id,
                 std::uint16_t notification_code) override;

  bool HandleListNotification(std::uint16_t notification_code);

 private:

  void SyncFromMirror();

  WinDesktopWindowPresenter* Desktop() const;

  HWND hwnd_;

};

// The desktop window, push button and list presenters used by Windows apps.
void EnsureWinControlPresenterRegistration();
void EnsureWinUiPresenterRegistration();

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

