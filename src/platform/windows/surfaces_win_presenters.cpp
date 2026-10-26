#include "apptraverse/platform/windows/surfaces_win_presenters.h"

#include "apptraverse/platform/windows/surfaces_win32_z_order.h"

#include <string>

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/presenter.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/surfaces/surfaces_localization.h"
#include "apptraverse/surfaces/surfaces_model.h"
#include "apptraverse/surfaces/surfaces_win32_messages.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_native_push_button.h"

namespace apptraverse {

namespace {

constexpr int kMargin = kSurfaceChromeMargin;
constexpr int kGap = 8;
constexpr int kAddW = kSurfaceAddButtonLayoutWidth;
constexpr int kAddH = kSurfaceAddButtonLayoutHeight;
constexpr int kLangW = 48;
constexpr int kLangH = 28;
constexpr int kCloseW = 200;
constexpr int kCloseH = 28;
constexpr int kHorizontalChromeMinWidth =
    kMargin + kAddW + kGap + kLangW + kGap + kCloseW + kMargin;

bool ChromeButtonsUseHorizontalRow(SurfaceWindowPresenter const& window) {
  if (!window.surface) {
    return true;
  }
  Surface const& surface = *window.surface;
  if (surface.presentation_width < kHorizontalChromeMinWidth) {
    return false;
  }
  return surface.presentation_width >= surface.presentation_height;
}

void SetHwndUserData(HWND hwnd, void* value) {
  SetLastError(0);
  LONG_PTR const previous =
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
  if (previous == 0 && GetLastError() != 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowLongPtrW GWLP_USERDATA", err);
  }
}

void PostApplicationStop(void* presentation_host) {
  HWND const notify = reinterpret_cast<HWND>(presentation_host);
  if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("PostMessageW WM_APPTRAVERSE_STOP", err);
  }
}

Win32SurfaceWindowPresenter* AsWin32Window(SurfaceWindowPresenter* base) {
  if (base == nullptr) {
    return nullptr;
  }
  return static_cast<Win32SurfaceWindowPresenter*>(base);
}

void SurfaceButtonLayout(SurfaceWindowPresenter const& window,
                         std::uint32_t control_id, int* x, int* y, int* w,
                         int* h) {
  *x = kMargin;
  *y = kMargin;
  if (control_id == static_cast<std::uint32_t>(kSurfaceAddButtonId)) {
    *w = kAddW;
    *h = kAddH;
    return;
  }
  if (control_id == static_cast<std::uint32_t>(kSurfaceLanguageButtonId)) {
    *w = kLangW;
    *h = kLangH;
    if (ChromeButtonsUseHorizontalRow(window)) {
      *x = kMargin + kAddW + kGap;
    } else {
      *y = kMargin + kAddH + kGap;
    }
    return;
  }
  *w = kCloseW;
  *h = kCloseH;
  if (ChromeButtonsUseHorizontalRow(window)) {
    *x = kMargin + kAddW + kGap + kLangW + kGap;
  } else {
    *y = kMargin + kAddH + kGap + kLangH + kGap;
  }
}

LRESULT CALLBACK SurfaceHostWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* presenter = reinterpret_cast<SurfacePresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (presenter == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_COMMAND) {
    auto* window = static_cast<Win32SurfaceWindowPresenter*>(presenter);
    if (window->child_command_handler != nullptr &&
        window->child_command_handler(window, wparam, lparam)) {
      return 0;
    }
    if (DispatchChildCommand(window->hwnd, wparam, lparam)) {
      return 0;
    }
  }
  if (msg == WM_SIZE) {
    RECT client{};
    if (GetClientRect(hwnd, &client) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("GetClientRect Surface WM_SIZE", err);
    }
    presenter->PresentationSizeChanged(client.right - client.left,
                                       client.bottom - client.top);
    return 0;
  }
  if (msg == WM_ACTIVATE) {
    if (LOWORD(wparam) != WA_INACTIVE &&
        !SurfacesWin32ZOrderRestoreInProgress()) {
      presenter->PageShown();
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_CLOSE) {
    PostApplicationStop(presenter->presentation_host);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

void ComputeSurfaceChromeButtonLayout(SurfaceWindowPresenter const& window,
                                      std::uint32_t control_id, int* x,
                                      int* y, int* w, int* h) {
  SurfaceButtonLayout(window, control_id, x, y, w, h);
}

bool TryParseSurfaceChildBnClicked(HWND parent_hwnd, WPARAM wparam,
                                   LPARAM lparam,
                                   SurfaceChildBnClickedTarget* out) {
  if (out == nullptr || parent_hwnd == nullptr) {
    return false;
  }
  *out = {};
  if (static_cast<std::uint16_t>(HIWORD(wparam)) != BN_CLICKED) {
    return false;
  }
  HWND const child = reinterpret_cast<HWND>(lparam);
  if (child == nullptr || !IsWindow(child)) {
    return false;
  }
  if (GetParent(child) != parent_hwnd) {
    return false;
  }
  std::uint32_t const command_id = static_cast<std::uint32_t>(LOWORD(wparam));
  if (static_cast<std::uint32_t>(GetDlgCtrlID(child)) != command_id) {
    return false;
  }
  auto* owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(child, GWLP_USERDATA));
  if (owner == nullptr) {
    return false;
  }
  out->child_hwnd = child;
  out->presenter = owner;
  out->control_id = command_id;
  return true;
}

bool DispatchChildCommand(HWND parent_hwnd, WPARAM wparam, LPARAM lparam) {
  SurfaceChildBnClickedTarget target{};
  if (!TryParseSurfaceChildBnClicked(parent_hwnd, wparam, lparam, &target)) {
    return false;
  }
  target.presenter->NotifyClickedFromUi();
  return true;
}

Win32SurfaceWindowPresenter::ptr ResolveWin32WindowPresenter(
    Surface const& surface) {
  if (surface.chrome.window) {
    return Win32SurfaceWindowPresenter::ptr{surface.chrome.window};
  }
  return {};
}

void RegisterSurfacesWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &SurfaceHostWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kSurfacesWindowClass;
  if (RegisterClassW(&wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW Surfaces", err);
  }
}

void UnregisterSurfacesWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  if (UnregisterClassW(kSurfacesWindowClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW Surfaces", err);
  }
}

void Win32SurfaceWindowPresenter::SyncWindowTitle() {
  if (hwnd == nullptr || !locale || !window_title_prefix || !surface) {
    return;
  }
#if APPTRAVERSE_SURFACE_DEMO_REVISION == 1
  std::string const title = FormatSurfaceWindowTitleWithSuffix(
      *window_title_prefix, locale->language_index(), *surface);
#else
  std::string const title = FormatSurfaceWindowTitle(
      *window_title_prefix, locale->language_index(), surface->number);
#endif
  SetWindowTextW(hwnd, ui::windows::Utf8ToWide(title).c_str());
}

void Win32SurfaceWindowPresenter::OnLoad() {
  hwnd = CreateWindowExW(
      0, kSurfacesWindowClass, L"", WS_OVERLAPPEDWINDOW, surface->desktop_x,
      surface->desktop_y, surface->desktop_width, surface->desktop_height,
      nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Surface", err);
  }
  SetHwndUserData(hwnd, this);
  SyncWindowTitle();
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32SurfaceWindowPresenter::OnModelChanged() { SyncWindowTitle(); }

void Win32SurfaceWindowPresenter::OnUnload() {
  child_command_handler = nullptr;
  SetHwndUserData(hwnd, nullptr);
  if (hwnd != nullptr) {
    if (DestroyWindow(hwnd) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("DestroyWindow Surface", err);
    }
    hwnd = nullptr;
  }
}

void Win32SurfaceWindowPresenter::QueueCurrentBounds() {
  RECT rect{};
  if (GetWindowRect(hwnd, &rect) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowRect Surface", err);
  }
  UpdateModelBounds(rect.left, rect.top, rect.right - rect.left,
                    rect.bottom - rect.top);
}

void Win32SurfaceButtonPresenter::SyncCaption() {
  if (hwnd == nullptr || !locale || !label) {
    return;
  }
  SetWindowTextW(
      hwnd, ui::windows::Utf8ToWide(label->Resolve(locale->language_index()))
                .c_str());
}

void Win32SurfaceButtonPresenter::ApplyLayout() {
  if (hwnd == nullptr || !window_presenter || !window_presenter->surface) {
    return;
  }
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  SurfaceButtonLayout(*window_presenter, control_id, &x, &y, &w, &h);

  if (SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE) ==
      0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos Surface button", err);
  }
}

void Win32SurfaceButtonPresenter::OnLoad() {
  if (!window_presenter) {
    FatalWin32("Win32SurfaceButtonPresenter OnLoad window_presenter",
               ERROR_INVALID_PARAMETER);
  }
  auto* window = AsWin32Window(window_presenter.operator->());
  if (window == nullptr) {
    FatalWin32("Win32SurfaceButtonPresenter OnLoad Win32 window",
               ERROR_INVALID_PARAMETER);
  }
  if (window->hwnd == nullptr) {
    FatalWin32("Win32SurfaceButtonPresenter OnLoad parent hwnd",
               ERROR_INVALID_PARAMETER);
  }
  hwnd = ui::windows::CreateChildPushButton({
      .parent_hwnd = window->hwnd,
      .control_id = control_id,
      .presenter_user_data = this,
      .create_fatal_operation = "CreateWindowExW Surface button",
  });
  SyncCaption();
  ApplyLayout();
}

void Win32SurfaceButtonPresenter::OnModelChanged() {
  SyncCaption();
  ApplyLayout();
}

void Win32SurfaceButtonPresenter::OnUnload() {
  ui::windows::DestroyChildPushButton(hwnd, "DestroyWindow Surface button");
}

void Win32SurfaceButtonPresenter::NotifyClickedFromUi() {
  if (role == SurfaceButtonRole::Add) {
    model_proxy->Invoke<Surface>(surface->obj_id, &Surface::AddSurface);
    return;
  }
  if (surface->surfaces->surfaces.size() == 1) {
    PostApplicationStop(presentation_host);
    return;
  }
  model_proxy->Invoke<Surface>(surface->obj_id, &Surface::Remove);
}

void Win32SurfaceLanguageButtonPresenter::SyncCaption() {
  if (hwnd == nullptr || !locale || !language_switch_label) {
    return;
  }
  SetWindowTextW(hwnd,
                 ui::windows::Utf8ToWide(language_switch_label->Resolve(
                                           locale->language_index()))
                     .c_str());
}

void Win32SurfaceLanguageButtonPresenter::ApplyLayout() {
  if (hwnd == nullptr || !window_presenter || !window_presenter->surface) {
    return;
  }
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  SurfaceButtonLayout(*window_presenter, control_id, &x, &y, &w, &h);

  if (SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE) ==
      0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos Surface language button", err);
  }
}

void Win32SurfaceLanguageButtonPresenter::OnLoad() {
  if (!window_presenter) {
    FatalWin32("Win32SurfaceLanguageButtonPresenter OnLoad window_presenter",
               ERROR_INVALID_PARAMETER);
  }
  auto* window = AsWin32Window(window_presenter.operator->());
  if (window == nullptr) {
    FatalWin32("Win32SurfaceLanguageButtonPresenter OnLoad Win32 window",
               ERROR_INVALID_PARAMETER);
  }
  if (window->hwnd == nullptr) {
    FatalWin32("Win32SurfaceLanguageButtonPresenter OnLoad parent hwnd",
               ERROR_INVALID_PARAMETER);
  }
  hwnd = ui::windows::CreateChildPushButton({
      .parent_hwnd = window->hwnd,
      .control_id = control_id,
      .presenter_user_data = this,
      .create_fatal_operation = "CreateWindowExW Surface language button",
  });
  SyncCaption();
  ApplyLayout();
}

void Win32SurfaceLanguageButtonPresenter::OnModelChanged() {
  SyncCaption();
  ApplyLayout();
}

void Win32SurfaceLanguageButtonPresenter::OnUnload() {
  ui::windows::DestroyChildPushButton(hwnd,
                                      "DestroyWindow Surface language button");
}

LRESULT CALLBACK Win32SurfaceWindowPresenter::WndProc(HWND hwnd, UINT msg,
                                                      WPARAM wparam,
                                                      LPARAM lparam) {
  return SurfaceHostWndProc(hwnd, msg, wparam, lparam);
}

}  // namespace apptraverse
