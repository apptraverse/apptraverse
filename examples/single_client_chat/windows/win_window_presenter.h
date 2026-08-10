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

#include "apptraverse/object_macros.h"
#include "apptraverse/window_presenter.h"

#include "resize_window_event.h"
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

    RECT window_rect{0, 0, model.width, model.height};
    AdjustWindowRectEx(&window_rect, WS_OVERLAPPEDWINDOW, FALSE, 0);

    hwnd_ = CreateWindowExW(
        0, kClassName, L"AppTraverse Chat", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top, nullptr, nullptr,
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
        if (wparam != SIZE_MINIMIZED && width > 0 && height > 0) {
          ApplyResize(width, height);
          if (chat_presenter.is_loaded()) {
            static_cast<WinChatPresenter&>(*chat_presenter)
                .Layout(width, height);
          }
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

  void ApplyResize(int width, int height) {
    if (width <= 0 || height <= 0) {
      return;
    }
    assert(window.is_valid());
    window.Load();
    auto& model = static_cast<WindowsWindow&>(*window);
    if (model.width == width && model.height == height) {
      return;
    }
    auto event =
        ResizeWindowEvent::ptr::Create(ae::CreateWith{*window.domain()});
    event->width = width;
    event->height = height;
    event->ApplyTo(model);
    window.Save();
  }

  HWND hwnd_{nullptr};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EXAMPLES_WIN_WINDOW_PRESENTER_H_
