#include "apptraverse/platform/windows/win_ui_environment.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <string>
#include <windows.ui.viewmanagement.h>
#include <wrl.h>
#include "apptraverse/platform/windows/win32_fatal.h"

namespace apptraverse::ui::windows {
namespace {
HMODULE LoadSystemModule(wchar_t const* name) {
  wchar_t directory[MAX_PATH]{};
  UINT const length = GetSystemDirectoryW(directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return nullptr;
  return LoadLibraryW((std::wstring(directory) + L"\\" + name).c_str());
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
void WinUiEnvironment::Refresh() {
  UINT const dpi = WindowDpi(window_);
  scale_ = dpi / 96.0;
  text_scale_ = 1.0;
  if (modern_->text) {
    double value = 1.0;
    if (SUCCEEDED(modern_->text->get_TextScaleFactor(&value)) && std::isfinite(value) && value > 0) text_scale_ = value;
  }
  HIGHCONTRASTW contrast{sizeof(contrast)};
  high_contrast_ = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                   (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
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
  NONCLIENTMETRICSW metrics{sizeof(metrics)};
  if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
    FatalWin32("SystemParametersInfoW system fonts", GetLastError());
  }
  HDC const screen = GetDC(nullptr);
  if (!screen) FatalWin32("GetDC system fonts", GetLastError());
  int const system_dpi = GetDeviceCaps(screen, LOGPIXELSY);
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
  for (HWND child = GetWindow(window_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
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
    if (set) set(window_, 20, &dark, sizeof(dark));
    FreeLibrary(dwm);
  }
}
}  // namespace apptraverse::ui::windows
