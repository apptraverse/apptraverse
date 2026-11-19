#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

#ifndef WIN32_LEAN_AND_MEAN

#define WIN32_LEAN_AND_MEAN

#endif

#include <windows.h>
#include <memory>
#include <optional>
#include "apptraverse/ui/ui_device_state.h"
#include <vector>
#include "apptraverse/ui/ui_constraint_layout.h"


#include "apptraverse/ui/ui_native_presenter_schema.h"
#include "apptraverse/ui/ui_presenters.h"

#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"

namespace apptraverse::ui::windows {

class WinUiEnvironment;

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

  // Fullscreen is a published application choice; all native placement/style
  // bookkeeping remains runtime-only. The standard system caption is retained
  // in windowed mode, including its DWM and accessibility behavior.
  static constexpr std::uint32_t kFullscreenActionId = 5300;
  virtual bool HasFullscreenAction() const { return false; }
  virtual std::wstring ResolveFullscreenActionText() const { return {}; }
  virtual UiLayoutNode const* FullscreenActionLayout() const { return nullptr; }
  virtual void ToggleFullscreenFromUi() {}
  virtual void ExitFullscreenFromUi() {}
  void SetPresentationFullscreen(bool fullscreen);
  bool IsPresentationFullscreen() const { return fullscreen_; }
  void RefreshFullscreenBounds(bool frame_changed = false);
  bool HandlePresentationShortcut(MSG const& message);

  // Runtime presentation only: constraints come from the UI mirror, while
  // measurements, DPI, focus and actual rectangles stay local to this window.
  bool NativeCaptionProvidesClose() const override { return true; }
  virtual bool ShowPresentationContextMenu(int, int) { return false; }
  // Optional native menu composition. Windows owns the returned menu after
  // successful window creation; command actions still go through the proxy.
  virtual HMENU CreatePresentationMenu() { return nullptr; }
  static WinDesktopWindowPresenter* FromNativeWindow(HWND window);
  UiControlLayout ResolveChildLayout(UiControlLayout requested) const override;
  void RegisterChildControl(HWND child, std::uint32_t control_id);
  void UnregisterChildControl(HWND child);
  void RefreshLayout();
  void RefreshPresentationSettings();
  double LayoutScale() const;
  double TextScale() const;
  bool HighContrast() const;
  bool DarkTheme() const;
  COLORREF BackgroundColor() const;


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
  void ApplyRequestedNativeFrame(ClientFrame frame);

  static bool FramesEqual(ClientFrame a, ClientFrame b);

  struct NativeChild {
    HWND hwnd;
    std::uint32_t control_id;
    std::optional<bool> applied_dark_theme;
  };
  void ReportEnvironmentFromNative(UiEnvironmentReason reason = UiEnvironmentReason::kSystemNotification);
  void ApplyLocalPlacement();
  void KeepInCurrentWorkArea();
  bool NativeMeasurementsReady() const;
  UiConstraintLayoutResult ComputeLocalLayout() const;
  void ApplyChildAppearance(HWND child);
  void SyncFullscreenAction();
  void ScrollContent(int bar, WPARAM command);
  void EnsureFocusedControlVisible();
  bool PaintOwnerButton(DRAWITEMSTRUCT const& item);

  std::vector<NativeChild> native_children_;
  std::shared_ptr<WinUiEnvironment> environment_;
  std::optional<UiDeviceEnvironment> last_reported_environment_;
  std::optional<UiWindowEnvironment> last_reported_window_;
  std::optional<UiWindowEnvironment> pending_user_placement_;
  std::optional<UiWindowEnvironment> interactive_start_window_;
  UiAppearancePreferences applied_appearance_;
  std::uint64_t applied_placement_revision_{};
  bool applying_local_placement_{};
  bool applying_layout_{false};
  bool applying_settings_{false};
  int scroll_x_{0};
  int scroll_y_{0};
  int wheel_delta_{0};
  HWND hwnd_;

  bool applying_native_frame_;

  bool interactive_move_active_;
  bool fullscreen_{false};
  bool changing_fullscreen_{false};
  WINDOWPLACEMENT windowed_placement_{sizeof(WINDOWPLACEMENT)};
  LONG_PTR windowed_style_{};
  LONG_PTR windowed_ex_style_{};
  HWND fullscreen_action_{};
  std::optional<ClientFrame> pending_requested_frame_;

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
void EnsureWinWindowControlPresenterRegistration();
// All native controls, including standalone text controls.
void EnsureWinControlPresenterRegistration();
void EnsureWinUiPresenterRegistration();

}  // namespace apptraverse::ui::windows

#endif  // APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_PRESENTERS_H_

