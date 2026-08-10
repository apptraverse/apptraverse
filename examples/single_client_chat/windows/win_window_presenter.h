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
#include "apptraverse/window_presenter.h"

#include "display_environment_changed_event.h"
#include "win_chat_presenter.h"
#include "window_bounds_changed_event.h"
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

    if (ApplyDisplayEnvironmentToModel(model, model.x, model.y, model.width,
                                       model.height, CurrentSystemDpi())) {
      window.Save();
    }

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
          PersistNormalBoundsIfNeeded(hwnd);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
      }
      case WM_DISPLAYCHANGE: {
        HandleDisplayEnvironmentChange(hwnd);
        return 0;
      }
      case WM_SETTINGCHANGE: {
        if (wparam == SPI_SETWORKAREA) {
          HandleDisplayEnvironmentChange(hwnd);
        }
        return 0;
      }
      case WM_DPICHANGED: {
        auto const* suggested = reinterpret_cast<RECT const*>(lparam);
        if (suggested != nullptr) {
          HandleDpiChanged(hwnd, *suggested, HIWORD(wparam));
        }
        return 0;
      }
      case WM_COMMAND: {
        if (LOWORD(wparam) == 3 && HIWORD(wparam) == BN_CLICKED) {
          if (chat_presenter.is_loaded()) {
            static_cast<WinChatPresenter&>(*chat_presenter).OnSendClicked();
            window.Save();
            chat_presenter->chat.Save();
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

  bool ApplyDisplayEnvironmentToModel(WindowsWindow& model,
                                      std::int32_t candidate_x,
                                      std::int32_t candidate_y,
                                      std::int32_t candidate_width,
                                      std::int32_t candidate_height,
                                      std::int32_t dpi) {
    RECT candidate{candidate_x, candidate_y, candidate_x + candidate_width,
                   candidate_y + candidate_height};
    RECT const work = WorkAreaForRect(candidate);

    std::int32_t const before_x = model.x;
    std::int32_t const before_y = model.y;
    std::int32_t const before_w = model.width;
    std::int32_t const before_h = model.height;

    auto event = DisplayEnvironmentChangedEvent::ptr::Create(
        ae::CreateWith{*window.domain()});
    event->work_left = work.left;
    event->work_top = work.top;
    event->work_right = work.right;
    event->work_bottom = work.bottom;
    event->candidate_x = candidate_x;
    event->candidate_y = candidate_y;
    event->candidate_width = candidate_width;
    event->candidate_height = candidate_height;
    event->dpi = dpi;
    event->ApplyTo(model);

    return model.x != before_x || model.y != before_y ||
           model.width != before_w || model.height != before_h;
  }

  void ApplyModelBoundsToHwnd(HWND hwnd, WindowsWindow const& model) {
    applying_model_bounds_ = true;
    SetWindowPos(hwnd, nullptr, model.x, model.y, model.width, model.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applying_model_bounds_ = false;
  }

  void PersistNormalBoundsIfNeeded(HWND hwnd) {
    if (IsIconic(hwnd)) {
      return;
    }

    assert(window.is_valid());
    window.Load();
    auto& model = static_cast<WindowsWindow&>(*window);

    RECT outer{};
    if (IsZoomed(hwnd)) {
      // Keep maximize; refresh the stored normal rectangle for a future restore.
      outer = QueryNormalOuterRect(hwnd);
    } else {
      GetWindowRect(hwnd, &outer);
    }

    std::int32_t const next_x = outer.left;
    std::int32_t const next_y = outer.top;
    std::int32_t const next_w = outer.right - outer.left;
    std::int32_t const next_h = outer.bottom - outer.top;
    if (next_w <= 0 || next_h <= 0) {
      return;
    }
    if (model.x == next_x && model.y == next_y && model.width == next_w &&
        model.height == next_h) {
      return;
    }

    auto event = WindowBoundsChangedEvent::ptr::Create(
        ae::CreateWith{*window.domain()});
    event->x = next_x;
    event->y = next_y;
    event->width = next_w;
    event->height = next_h;
    event->ApplyTo(model);
    window.Save();
  }

  void HandleDisplayEnvironmentChange(HWND hwnd) {
    assert(window.is_valid());
    window.Load();
    auto& model = static_cast<WindowsWindow&>(*window);

    RECT candidate{model.x, model.y, model.x + model.width,
                   model.y + model.height};
    if (!IsIconic(hwnd) && !IsZoomed(hwnd)) {
      GetWindowRect(hwnd, &candidate);
    }

    bool const changed = ApplyDisplayEnvironmentToModel(
        model, candidate.left, candidate.top, candidate.right - candidate.left,
        candidate.bottom - candidate.top, CurrentSystemDpi());
    if (changed) {
      window.Save();
    }
    if (!IsZoomed(hwnd) && !IsIconic(hwnd)) {
      ApplyModelBoundsToHwnd(hwnd, model);
    }
  }

  void HandleDpiChanged(HWND hwnd, RECT const& suggested, UINT dpi) {
    assert(window.is_valid());
    window.Load();
    auto& model = static_cast<WindowsWindow&>(*window);

    bool const changed = ApplyDisplayEnvironmentToModel(
        model, suggested.left, suggested.top, suggested.right - suggested.left,
        suggested.bottom - suggested.top, static_cast<std::int32_t>(dpi));
    if (changed) {
      window.Save();
    }
    ApplyModelBoundsToHwnd(hwnd, model);
  }

  HWND hwnd_{nullptr};
  bool applying_model_bounds_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_WINDOW_PRESENTER_H_
