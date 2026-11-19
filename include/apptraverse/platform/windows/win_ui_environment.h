#ifndef APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_ENVIRONMENT_H_
#define APPTRAVERSE_PLATFORM_WINDOWS_WIN_UI_ENVIRONMENT_H_

#include <windows.h>
#include <memory>
#include <map>
#include "apptraverse/ui/ui_device_state.h"

namespace apptraverse::ui::windows {

// Called before creating native host windows. Existing manifest/host policy wins.
void EnsureWinProcessDpiAwareness();
UINT GetWinWindowDpi(HWND window);
std::string GetWinWindowMonitorKey(HWND window);
// Load optional Windows components from System32, never the working directory.
HMODULE LoadWinSystemModule(wchar_t const* name);

inline constexpr UINT kWinPresentationSettingsChanged = WM_APP + 0x310;
struct WinUiPalette {
  COLORREF background{}, foreground{}, control_background{}, disabled_foreground{}, border{};
  bool dark{}, high_contrast{};
  bool operator==(WinUiPalette const&) const = default;
};
WinUiPalette ResolveWinUiPalette(WinUiPalette system, WinUiPalette light,
                                  UiAppearancePreferences preferences);

// Native resources and subscriptions stay runtime-only. snapshot() contains
// serializable OS facts; the owning model decides how to compare/persist them.
// The Win7 path needs only User32/GDI; newer DPI and WinRT APIs are optional.
class WinUiEnvironment {
 public:
  explicit WinUiEnvironment(HWND window);
  ~WinUiEnvironment();
  WinUiEnvironment(WinUiEnvironment const&) = delete;
  WinUiEnvironment& operator=(WinUiEnvironment const&) = delete;
  void Refresh(UiAppearancePreferences preferences = {});
  UiDeviceEnvironment const& snapshot() const { return snapshot_; }
  UiWindowEnvironment WindowSnapshot(ae::ObjId window_id) const;
  bool AcceptsNotification(WPARAM token) const;
  HFONT font() const { return font_; }
  HBRUSH background_brush() const { return background_brush_; }
  double scale() const { return scale_; }
  double text_scale() const { return text_scale_; }
  bool high_contrast() const { return high_contrast_; }
  bool dark() const { return dark_; }
  COLORREF background() const { return background_; }
  COLORREF foreground() const { return foreground_; }
  COLORREF control_background() const { return control_background_; }
  COLORREF disabled_foreground() const { return disabled_foreground_; }
  COLORREF border() const { return border_; }
 private:
  struct ModernSettings;
  std::unique_ptr<ModernSettings> modern_;
  HWND window_;
  UiDeviceEnvironment snapshot_;
  std::map<std::wstring, std::string> monitor_keys_;
  HFONT font_{nullptr};
  HBRUSH background_brush_{nullptr};
  double scale_{1.0};
  double text_scale_{1.0};
  bool high_contrast_{false};
  bool dark_{false};
  COLORREF background_{0};
  COLORREF foreground_{0};
  COLORREF control_background_{0};
  COLORREF disabled_foreground_{0};
  COLORREF border_{0};
};

}  // namespace apptraverse::ui::windows
#endif
