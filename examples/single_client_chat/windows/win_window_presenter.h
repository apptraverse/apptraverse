#ifndef APPTRAVERSE_EXAMPLES_WIN_WINDOW_PRESENTER_H_
#define APPTRAVERSE_EXAMPLES_WIN_WINDOW_PRESENTER_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cassert>
#include <cstdint>

#include "apptraverse/object_macros.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

#include "win_chat_presenter.h"
#include "windows_window.h"

namespace apptraverse {

class WinWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(WinWindowPresenter, WindowPresenter, 0)

 protected:
  WinWindowPresenter() = default;

 public:
  explicit WinWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}

  AE_OBJECT_REFLECT()

  void CreateNativeWindow() {
    assert(window.is_valid());
    window.Load();
    assert(window->GetClassId() == WindowsWindow::kClassId);
    auto& model = static_cast<WindowsWindow&>(*window);

    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinWindowPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    CommitWindowChanged(model.x, model.y, model.x + model.width,
                        model.y + model.height, CurrentSystemDpi());

    hwnd_ = CreateWindowExW(0, kClassName, L"AppTraverse Chat",
                            WS_OVERLAPPEDWINDOW, model.x, model.y, model.width,
                            model.height, nullptr, nullptr,
                            GetModuleHandleW(nullptr), this);
    assert(hwnd_ != nullptr);

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
  }

  HWND hwnd() const { return hwnd_; }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseWinChatWindow";

  static WinWindowPresenter* FromHwnd(HWND hwnd) {
    return reinterpret_cast<WinWindowPresenter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<WinWindowPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hwnd;
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    auto* self = FromHwnd(hwnd);
    if (self == nullptr) {
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }

  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_CREATE: {
        assert(chat_presenter.is_valid());
        chat_presenter.Load();
        assert(chat_presenter->GetClassId() == WinChatPresenter::kClassId);
        auto& chat_ui = static_cast<WinChatPresenter&>(*chat_presenter);
        chat_ui.CreateControls(hwnd);
        return 0;
      }
      case WM_SIZE: {
        int const width = LOWORD(lparam);
        int const height = HIWORD(lparam);
        if (wparam != SIZE_MINIMIZED && width > 0 && height > 0 &&
            chat_presenter.is_loaded()) {
          static_cast<WinChatPresenter&>(*chat_presenter)
              .Layout(width, height);
        }
        return 0;
      }
      case WM_WINDOWPOSCHANGED: {
        if (!applying_model_bounds_) {
          CommitFromHwnd(hwnd, CurrentSystemDpi());
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
      }
      case WM_DISPLAYCHANGE: {
        CommitFromHwnd(hwnd, CurrentSystemDpi());
        ApplyModelBoundsToHwnd(hwnd);
        return 0;
      }
      case WM_SETTINGCHANGE: {
        if (wparam == SPI_SETWORKAREA) {
          CommitFromHwnd(hwnd, CurrentSystemDpi());
          ApplyModelBoundsToHwnd(hwnd);
        }
        return 0;
      }
      case WM_DPICHANGED: {
        auto const* suggested = reinterpret_cast<RECT const*>(lparam);
        if (suggested != nullptr) {
          CommitWindowChanged(suggested->left, suggested->top, suggested->right,
                              suggested->bottom,
                              static_cast<std::int32_t>(HIWORD(wparam)));
          ApplyModelBoundsToHwnd(hwnd);
        }
        return 0;
      }
      case WM_COMMAND: {
        int const id = LOWORD(wparam);
        if (HIWORD(wparam) == BN_CLICKED && chat_presenter.is_loaded()) {
          auto& chat_ui =
              static_cast<WinChatPresenter&>(*chat_presenter);
          if (id == 3) {
            chat_ui.OnSendClicked();
            window.Save();
            chat_presenter->chat.Save();
          } else if (id == 4) {
            chat_ui.OnAddClicked();
          }
        }
        return 0;
      }
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
  }

  static std::int32_t CurrentSystemDpi() {
    HDC hdc = GetDC(nullptr);
    if (hdc == nullptr) {
      return 96;
    }
    int const dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);
    return dpi > 0 ? dpi : 96;
  }

  static RECT WorkAreaForRect(RECT const& candidate) {
    HMONITOR const monitor =
        MonitorFromRect(&candidate, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
    }
    return info.rcWork;
  }

  static RECT PrimaryWorkArea() {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    HMONITOR const primary =
        MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(primary, &info)) {
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &info.rcWork, 0);
    }
    return info.rcWork;
  }

  static RECT WorkspaceToScreen(RECT workspace_rect) {
    RECT const primary_work = PrimaryWorkArea();
    OffsetRect(&workspace_rect, primary_work.left, primary_work.top);
    return workspace_rect;
  }

  static RECT QueryNormalOuterRect(HWND hwnd) {
    if (IsIconic(hwnd) || IsZoomed(hwnd)) {
      WINDOWPLACEMENT placement{};
      placement.length = sizeof(placement);
      if (GetWindowPlacement(hwnd, &placement)) {
        return WorkspaceToScreen(placement.rcNormalPosition);
      }
    }
    RECT outer{};
    GetWindowRect(hwnd, &outer);
    return outer;
  }

  void CommitWindowChanged(std::int32_t window_left, std::int32_t window_top,
                           std::int32_t window_right, std::int32_t window_bottom,
                           std::int32_t dpi) {
    assert(window.is_valid());
    window.Load();
    auto& model = static_cast<WindowsWindow&>(*window);

    RECT candidate{window_left, window_top, window_right, window_bottom};
    RECT const work = WorkAreaForRect(candidate);

    auto event =
        WindowChangedEvent::ptr::Create(ae::CreateWith{*window.domain()});
    event->available_left = work.left;
    event->available_top = work.top;
    event->available_right = work.right;
    event->available_bottom = work.bottom;
    event->window_left = window_left;
    event->window_top = window_top;
    event->window_right = window_right;
    event->window_bottom = window_bottom;
    event->density_dpi = dpi;

    window->Commit(event);
    window.Save();
  }

  void CommitFromHwnd(HWND hwnd, std::int32_t dpi) {
    RECT outer = QueryNormalOuterRect(hwnd);
    if (!IsIconic(hwnd) && !IsZoomed(hwnd)) {
      GetWindowRect(hwnd, &outer);
    }
    CommitWindowChanged(outer.left, outer.top, outer.right, outer.bottom, dpi);
  }

  void ApplyModelBoundsToHwnd(HWND hwnd) {
    assert(window.is_valid());
    window.Load();
    auto& model = static_cast<WindowsWindow&>(*window);
    if (IsZoomed(hwnd) || IsIconic(hwnd)) {
      return;
    }
    applying_model_bounds_ = true;
    SetWindowPos(hwnd, nullptr, model.x, model.y, model.width, model.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applying_model_bounds_ = false;
  }

  HWND hwnd_{nullptr};
  bool applying_model_bounds_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_WINDOW_PRESENTER_H_
