#include "win_presenters.h"

#include <cstdio>

#include "apptraverse/model_object_proxy.h"
#include "surfaces_win32_messages.h"
#include "win32_fatal.h"

namespace apptraverse {

bool DispatchChildCommand(WPARAM wparam, LPARAM lparam);

namespace {

APPTRAVERSE_REGISTER(Win32SurfaceWindowPresenter);
APPTRAVERSE_REGISTER(Win32SurfaceButtonPresenter);

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
  if (msg == WM_COMMAND && DispatchChildCommand(wparam, lparam)) {
    return 0;
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
    if (LOWORD(wparam) != WA_INACTIVE) {
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

bool DispatchChildCommand(WPARAM wparam, LPARAM lparam) {
  HWND const child = reinterpret_cast<HWND>(lparam);
  if (child == nullptr) {
    return false;
  }
  auto* owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(child, GWLP_USERDATA));
  if (owner == nullptr) {
    return false;
  }
  return owner->OnCommand(static_cast<std::uint32_t>(LOWORD(wparam)),
                          static_cast<std::uint16_t>(HIWORD(wparam)));
}

Win32SurfaceWindowPresenter::ptr ResolveWin32WindowPresenter(
    Surface const& surface) {
  if (surface.chrome.window) {
    return Win32SurfaceWindowPresenter::ptr{surface.chrome.window};
  }
  return {};
}

void EnsureWin32SurfacePresenterRegistration() {
  EnsureDesktopSurfacePresenterRegistration();
  EnsureSurfaceWindowPresenterRegistration();
  EnsureSurfaceButtonPresenterRegistration();
  (void)&g_apptraverse_registrar_Win32SurfaceWindowPresenter;
  (void)&g_apptraverse_registrar_Win32SurfaceButtonPresenter;
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

void Win32SurfaceWindowPresenter::OnLoad() {
  wchar_t title[64];
  std::swprintf(title, 64, L"Surface %u", surface->number);
  hwnd = CreateWindowExW(
      0, kSurfacesWindowClass, title, WS_OVERLAPPEDWINDOW, surface->desktop_x,
      surface->desktop_y, surface->desktop_width, surface->desktop_height,
      nullptr, nullptr, GetModuleHandleW(nullptr), this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Surface", err);
  }
  SetHwndUserData(hwnd, this);
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32SurfaceWindowPresenter::OnUnload() {
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

void Win32SurfaceButtonPresenter::ApplyLayout() {
  if (hwnd == nullptr || !window_presenter || !window_presenter->surface) {
    return;
  }
  constexpr int kMargin = 12;
  constexpr int kGap = 8;
  constexpr int kAddW = 80;
  constexpr int kAddH = 28;
  constexpr int kCloseW = 160;
  constexpr int kCloseH = 28;

  int x = kMargin;
  int y = kMargin;
  int w = kAddW;
  int h = kAddH;
  if (role == SurfaceButtonRole::Close) {
    w = kCloseW;
    h = kCloseH;
    if (window_presenter->IsWide()) {
      x = kMargin + kAddW + kGap;
    } else {
      y = kMargin + kAddH + kGap;
    }
  }

  if (SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE) ==
      0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos Surface button", err);
  }
}

void Win32SurfaceButtonPresenter::OnLoad() {
  auto* window = AsWin32Window(&*window_presenter);
  if (window == nullptr || window->hwnd == nullptr) {
    return;
  }
  wchar_t const* label = role == SurfaceButtonRole::Add ? L"Add"
                                                        : L"Close this window";
  hwnd = CreateWindowExW(0, L"BUTTON", label,
                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0,
                         window->hwnd,
                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
                         GetModuleHandleW(nullptr), nullptr);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Surface button", err);
  }
  SetHwndUserData(hwnd, this);
  ApplyLayout();
}

void Win32SurfaceButtonPresenter::OnModelChanged() { ApplyLayout(); }

void Win32SurfaceButtonPresenter::OnUnload() {
  SetHwndUserData(hwnd, nullptr);
  if (hwnd != nullptr) {
    if (DestroyWindow(hwnd) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("DestroyWindow Surface button", err);
    }
    hwnd = nullptr;
  }
}

bool Win32SurfaceButtonPresenter::OnCommand(std::uint32_t command_id,
                                            std::uint16_t notification_code) {
  if (notification_code != BN_CLICKED || command_id != control_id) {
    return false;
  }
  if (!model_proxy || !surface) {
    return false;
  }
  if (role == SurfaceButtonRole::Add) {
    model_proxy->Invoke<Surface>(surface->obj_id, &Surface::AddSurface);
    return true;
  }
  if (surface->surfaces->surfaces.size() == 1) {
    PostApplicationStop(presentation_host);
  } else {
    model_proxy->Invoke<Surface>(surface->obj_id, &Surface::Remove);
  }
  return true;
}

LRESULT CALLBACK Win32SurfaceWindowPresenter::WndProc(HWND hwnd, UINT msg,
                                                      WPARAM wparam,
                                                      LPARAM lparam) {
  return SurfaceHostWndProc(hwnd, msg, wparam, lparam);
}

}  // namespace apptraverse
