#include "apptraverse/platform/windows/win_ui_environment.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"

namespace apptraverse::ui::windows {
namespace {
HMODULE LoadSystemModule(wchar_t const* name) {
  wchar_t directory[MAX_PATH]{};
  UINT const length = GetSystemDirectoryW(directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return nullptr;
  return LoadLibraryW((std::wstring(directory) + L"\\" + name).c_str());
}
std::string MonitorKey(wchar_t const* display) {
  DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
  // DeviceID is a monitor device-interface path on Vista and later. Unlike
  // DISPLAY1/2 it identifies the local PnP endpoint across display reordering.
  if (EnumDisplayDevicesW(display, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME) && device.DeviceID[0])
    return WideToUtf8(device.DeviceID);
  // Remote/virtual displays need not expose a PnP monitor interface.
  return "gdi:" + WideToUtf8(display);
}
UINT WindowDpi(HWND window) {
  using GetDpi = UINT(WINAPI*)(HWND);
  auto function = reinterpret_cast<GetDpi>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  if (function != nullptr) {
    UINT const dpi = function(window);
    if (dpi != 0) return dpi;
  }
  HDC const dc = GetDC(window);
  if (dc == nullptr) FatalWin32("GetDC system DPI", GetLastError());
  int const dpi = GetDeviceCaps(dc, LOGPIXELSX);
  ReleaseDC(window, dc);
  return dpi > 0 ? static_cast<UINT>(dpi) : 96;
}
}  // namespace

UINT GetWinWindowDpi(HWND window) { return WindowDpi(window); }
std::string GetWinWindowMonitorKey(HWND window) {
  MONITORINFOEXW info{}; info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info))
    FatalWin32("GetMonitorInfoW monitor identity", GetLastError());
  return MonitorKey(info.szDevice);
}

void EnsureWinProcessDpiAwareness() {
  static bool const configured = [] {
    using SetContext = BOOL(WINAPI*)(HANDLE);
    auto context = reinterpret_cast<SetContext>(GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (context != nullptr) {
      if (context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))) ||
          GetLastError() == ERROR_ACCESS_DENIED) return true;
    }
    HMODULE shcore = LoadSystemModule(L"shcore.dll");
    if (shcore != nullptr) {
      using SetAwareness = HRESULT(WINAPI*)(int);
      auto awareness = reinterpret_cast<SetAwareness>(GetProcAddress(shcore, "SetProcessDpiAwareness"));
      HRESULT const result = awareness ? awareness(2) : E_NOTIMPL;
      FreeLibrary(shcore);
      if (SUCCEEDED(result) || result == E_ACCESSDENIED) return true;
    }
    // Vista/Win7 provide system-DPI awareness, without per-monitor scaling.
    SetProcessDPIAware();
    return true;
  }();
  (void)configured;
}

struct WinUiEnvironment::ModernSettings {
  using CreateString = HRESULT(WINAPI*)(PCNZWCH, UINT32, HSTRING*);
  using DeleteString = HRESULT(WINAPI*)(HSTRING);
  using Activate = HRESULT(WINAPI*)(HSTRING, IInspectable**);
  using Initialize = HRESULT(WINAPI*)(int);
  using Uninitialize = void(WINAPI*)();
  using SettingsHandler = __FITypedEventHandler_2_Windows__CUI__CViewManagement__CUISettings_IInspectable;
  HMODULE module{nullptr};
  Uninitialize uninitialize{nullptr};
  bool initialized{false};
  Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IUISettings2> text;
  Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IUISettings3> colors;
  Microsoft::WRL::ComPtr<SettingsHandler> handler;
  EventRegistrationToken text_token{};
  EventRegistrationToken color_token{};
  bool text_subscribed{false};
  bool color_subscribed{false};
  struct NotificationTarget {
    std::atomic<HWND> window;
    UINT_PTR token;
    explicit NotificationTarget(HWND hwnd) : window{hwnd} {
      static std::atomic<UINT_PTR> next{1};
      token = next.fetch_add(1);
    }
  };
  std::shared_ptr<NotificationTarget> notification;

  explicit ModernSettings(HWND window)
      : notification{std::make_shared<NotificationTarget>(window)} {
    module = LoadSystemModule(L"combase.dll");
    if (module == nullptr) return;  // Windows 7: native metrics remain authoritative.
    auto create = reinterpret_cast<CreateString>(GetProcAddress(module, "WindowsCreateString"));
    auto remove = reinterpret_cast<DeleteString>(GetProcAddress(module, "WindowsDeleteString"));
    auto activate = reinterpret_cast<Activate>(GetProcAddress(module, "RoActivateInstance"));
    auto initialize = reinterpret_cast<Initialize>(GetProcAddress(module, "RoInitialize"));
    uninitialize = reinterpret_cast<Uninitialize>(GetProcAddress(module, "RoUninitialize"));
    if (!create || !remove || !activate || !initialize || !uninitialize) return;
    HRESULT const init = initialize(0);  // RO_INIT_SINGLETHREADED
    initialized = SUCCEEDED(init);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return;
    HSTRING name{};
    constexpr wchar_t class_name[] = L"Windows.UI.ViewManagement.UISettings";
    if (FAILED(create(class_name, static_cast<UINT32>(std::size(class_name) - 1), &name))) return;
    Microsoft::WRL::ComPtr<IInspectable> object;
    HRESULT const result = activate(name, object.GetAddressOf());
    remove(name);
    if (FAILED(result)) return;
    object.As(&text);
    object.As(&colors);
    std::weak_ptr<NotificationTarget> weak = notification;
    handler = Microsoft::WRL::Callback<SettingsHandler>(
        [weak](ABI::Windows::UI::ViewManagement::IUISettings*, IInspectable*) -> HRESULT {
          if (auto target = weak.lock()) {
            HWND const hwnd = target->window.load();
            if (hwnd != nullptr) PostMessageW(hwnd, kWinPresentationSettingsChanged, target->token, 0);
          }
          return S_OK;
        });
    if (text) text_subscribed = SUCCEEDED(text->add_TextScaleFactorChanged(handler.Get(), &text_token));
    if (colors) color_subscribed = SUCCEEDED(colors->add_ColorValuesChanged(handler.Get(), &color_token));
  }
  ~ModernSettings() {
    notification->window.store(nullptr);
    if (text_subscribed) text->remove_TextScaleFactorChanged(text_token);
    if (color_subscribed) colors->remove_ColorValuesChanged(color_token);
    handler.Reset(); colors.Reset(); text.Reset();
    if (initialized) uninitialize();
    if (module != nullptr) FreeLibrary(module);
  }
};

WinUiEnvironment::WinUiEnvironment(HWND window)
    : modern_{std::make_unique<ModernSettings>(window)}, window_{window} {
  Refresh();
}
WinUiEnvironment::~WinUiEnvironment() {
  modern_.reset();
  if (font_) DeleteObject(font_);
  if (background_brush_) DeleteObject(background_brush_);
}
bool WinUiEnvironment::AcceptsNotification(WPARAM token) const {
  return modern_ && token == modern_->notification->token;
}
void WinUiEnvironment::Refresh(UiAppearancePreferences preferences) {
  snapshot_.supported_settings = 0;
  UINT const dpi = WindowDpi(window_);
  scale_ = dpi / 96.0;
  text_scale_ = 1.0;
  if (modern_->text) {
    double value = 1.0;
    if (SUCCEEDED(modern_->text->get_TextScaleFactor(&value)) && std::isfinite(value) && value > 0) {
      text_scale_ = value;
      snapshot_.supported_settings |= kSystemTextScale;
    }
  }
  HIGHCONTRASTW contrast{sizeof(contrast)};
  bool const has_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) != FALSE;
  high_contrast_ = has_contrast && (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
  if (has_contrast) snapshot_.supported_settings |= kSystemHighContrast;
  if (modern_->colors) snapshot_.supported_settings |= kSystemDarkMode;
  background_ = GetSysColor(COLOR_WINDOW);
  foreground_ = GetSysColor(COLOR_WINDOWTEXT);
  control_background_ = GetSysColor(COLOR_BTNFACE);
  disabled_foreground_ = GetSysColor(COLOR_GRAYTEXT);
  border_ = GetSysColor(COLOR_WINDOWFRAME);
  dark_ = false;
  if (!high_contrast_ && modern_->colors) {
    ABI::Windows::UI::Color background{};
    ABI::Windows::UI::Color foreground{};
    using namespace ABI::Windows::UI::ViewManagement;
    if (SUCCEEDED(modern_->colors->GetColorValue(UIColorType_Background, &background)) &&
        SUCCEEDED(modern_->colors->GetColorValue(UIColorType_Foreground, &foreground))) {
      dark_ = background.R + background.G + background.B < foreground.R + foreground.G + foreground.B;
      if (dark_) {
        background_ = RGB(background.R, background.G, background.B);
        foreground_ = RGB(foreground.R, foreground.G, foreground.B);
        control_background_ = RGB(40, 40, 40);
        disabled_foreground_ = RGB(160, 160, 160);
        border_ = RGB(150, 150, 150);
      }
    }
  }
  // Snapshot raw OS facts before applying application-local preferences.
  snapshot_.text_scale_milli = static_cast<std::uint32_t>(std::lround(text_scale_ * 1000));
  snapshot_.dark = dark_;
  snapshot_.high_contrast = high_contrast_;
  snapshot_.colors = {background_, foreground_, control_background_, disabled_foreground_, border_};
  BOOL enabled = TRUE;
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0))
    snapshot_.supported_settings |= kSystemReducedMotion;
  snapshot_.reduced_motion = enabled == FALSE;
  enabled = FALSE;
  if (SystemParametersInfoW(SPI_GETKEYBOARDCUES, 0, &enabled, 0))
    snapshot_.supported_settings |= kSystemKeyboardCues;
  snapshot_.keyboard_cues = enabled != FALSE;
  enabled = FALSE;
  if (SystemParametersInfoW(SPI_GETSCREENREADER, 0, &enabled, 0))
    snapshot_.supported_settings |= kSystemScreenReaderHint;
  snapshot_.screen_reader_hint = enabled != FALSE;
  snapshot_.monitors.clear();
  if (!EnumDisplayMonitors(nullptr, nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
        auto& monitors = *reinterpret_cast<std::vector<UiMonitorEnvironment>*>(context);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) FatalWin32("GetMonitorInfoW environment", GetLastError());
        auto rect = [](RECT const& value) -> UiDisplayRect {
          return {value.left, value.top, value.right - value.left, value.bottom - value.top};
        };
        UiMonitorEnvironment item;
        item.key = MonitorKey(info.szDevice);
        item.bounds = rect(info.rcMonitor);
        item.work_area = rect(info.rcWork);
        item.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        DEVMODEW mode{}; mode.dmSize = sizeof(mode);
        if (EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
          item.orientation = mode.dmDisplayOrientation;
          item.refresh_hz = mode.dmDisplayFrequency;
        }
        monitors.push_back(std::move(item));
        return TRUE;
      }, reinterpret_cast<LPARAM>(&snapshot_.monitors)))
    FatalWin32("EnumDisplayMonitors environment", GetLastError());
  NormalizeUiDeviceEnvironment(snapshot_);
  // High Contrast always wins over a local appearance preference.
  if (!high_contrast_) {
    if (preferences.theme == UiThemeMode::kLight) {
      dark_ = false;
      background_ = GetSysColor(COLOR_WINDOW);
      foreground_ = GetSysColor(COLOR_WINDOWTEXT);
      control_background_ = GetSysColor(COLOR_BTNFACE);
      disabled_foreground_ = GetSysColor(COLOR_GRAYTEXT);
      border_ = GetSysColor(COLOR_WINDOWFRAME);
    } else if (preferences.theme == UiThemeMode::kDark && !dark_) {
      dark_ = true;
      background_ = RGB(0, 0, 0);
      foreground_ = RGB(255, 255, 255);
      control_background_ = RGB(40, 40, 40);
      disabled_foreground_ = RGB(160, 160, 160);
      border_ = RGB(150, 150, 150);
    }
    if (dark_ && preferences.soften_dark) {
      background_ = RGB(32, 32, 32);
      control_background_ = RGB(43, 43, 43);
    }
  }
  NONCLIENTMETRICSW metrics{sizeof(metrics)};
  if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
    FatalWin32("SystemParametersInfoW system fonts", GetLastError());
  }
  HDC const screen = GetDC(nullptr);
  if (!screen) FatalWin32("GetDC system fonts", GetLastError());
  int const system_dpi = GetDeviceCaps(screen, LOGPIXELSY);
  snapshot_.system_dpi_x = static_cast<std::uint32_t>(GetDeviceCaps(screen, LOGPIXELSX));
  snapshot_.system_dpi_y = static_cast<std::uint32_t>(system_dpi);
  snapshot_.message_font = {WideToUtf8(metrics.lfMessageFont.lfFaceName),
      metrics.lfMessageFont.lfHeight, metrics.lfMessageFont.lfWeight,
      metrics.lfMessageFont.lfItalic != 0};
  ReleaseDC(nullptr, screen);
  LOGFONTW font = metrics.lfMessageFont;
  // NONCLIENTMETRICS already includes the user's text-size preference. Scale
  // only from system DPI to this window's DPI; multiplying TextScaleFactor here
  // would apply accessibility text zoom twice. Keep TextScaleFactor separately
  // for preference reporting and its change subscription for local remeasurement.
  // Chromium documents this NCM behavior in ui/gfx/system_fonts_win.cc:
  // https://chromium.googlesource.com/chromium/src/+/9381754a05f6cd30d9dbacaa2a4199faae3e4cda/ui/gfx/system_fonts_win.cc
  font.lfHeight = static_cast<LONG>(std::lround(font.lfHeight *
      static_cast<double>(dpi) / std::max(1, system_dpi)));
  if (font.lfHeight == 0) font.lfHeight = -1;
  HFONT const next_font = CreateFontIndirectW(&font);
  if (!next_font) FatalWin32("CreateFontIndirectW system font", GetLastError());
  // Rebind children before deleting the old HFONT: no dangling control font.
  for (HWND child = window_ ? GetWindow(window_, GW_CHILD) : nullptr;
       child; child = GetWindow(child, GW_HWNDNEXT)) {
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(next_font), TRUE);
  }
  if (font_) DeleteObject(font_);
  font_ = next_font;
  HBRUSH const brush = CreateSolidBrush(background_);
  if (!brush) FatalWin32("CreateSolidBrush system palette", GetLastError());
  if (background_brush_) DeleteObject(background_brush_);
  background_brush_ = brush;
  // The titlebar enhancement is optional on newer Windows; Win7 rejects it.
  HMODULE dwm = LoadSystemModule(L"dwmapi.dll");
  if (dwm) {
    using SetAttribute = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto set = reinterpret_cast<SetAttribute>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    BOOL const dark = dark_ ? TRUE : FALSE;
    if (set && window_) set(window_, 20, &dark, sizeof(dark));
    FreeLibrary(dwm);
  }
}
UiWindowEnvironment WinUiEnvironment::WindowSnapshot(ae::ObjId window_id) const {
  MONITORINFOEXW info{}; info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &info))
    FatalWin32("GetMonitorInfoW window environment", GetLastError());
  auto const dpi = WindowDpi(window_);
  RECT frame{};
  if (!GetWindowRect(window_, &frame)) FatalWin32("GetWindowRect environment", GetLastError());
  return {window_id, MonitorKey(info.szDevice), dpi, dpi,
      {MulDiv(frame.left - info.rcWork.left, 96, dpi),
       MulDiv(frame.top - info.rcWork.top, 96, dpi),
       MulDiv(frame.right - frame.left, 96, dpi), MulDiv(frame.bottom - frame.top, 96, dpi)}};
}
}  // namespace apptraverse::ui::windows
