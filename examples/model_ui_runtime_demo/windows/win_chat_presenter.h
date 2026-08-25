#ifndef APPTRAVERSE_WIN_CHAT_PRESENTER_H_
#define APPTRAVERSE_WIN_CHAT_PRESENTER_H_

#include "win_util.h"
#include "ui_runtime_registry.h"

namespace apptraverse {

class WinChatPresenter {
 public:
  void Create(HWND parent, RuntimeChat const& runtime) {
    runtime_ = &runtime;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinChatPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0,
                            0, parent, nullptr, GetModuleHandleW(nullptr),
                            this);
    ApplyBounds();
  }

  void Present(RuntimeChat const& runtime) {
    runtime_ = &runtime;
    if (runtime.generation == last_generation_) {
      return;
    }
    last_generation_ = runtime.generation;
    ApplyBounds();
  }

  HWND hwnd() const { return hwnd_; }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseChatCanvas";
  static constexpr COLORREF kCanvasColor = RGB(196, 168, 112);

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<WinChatPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hwnd;
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinChatPresenter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      HBRUSH brush = CreateSolidBrush(kCanvasColor);
      FillRect(hdc, &ps.rcPaint, brush);
      DeleteObject(brush);
      EndPaint(hwnd, &ps);
      return 0;
    }
    if (msg == WM_ERASEBKGND) {
      return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  void ApplyBounds() {
    if (runtime_ == nullptr || hwnd_ == nullptr) {
      return;
    }
    if (runtime_->x == last_x_ && runtime_->y == last_y_ &&
        runtime_->width == last_w_ && runtime_->height == last_h_) {
      return;
    }
    last_x_ = runtime_->x;
    last_y_ = runtime_->y;
    last_w_ = runtime_->width;
    last_h_ = runtime_->height;
    MoveWindow(hwnd_, last_x_, last_y_, last_w_, last_h_, TRUE);
  }

  HWND hwnd_{nullptr};
  RuntimeChat const* runtime_{nullptr};
  std::uint64_t last_generation_{0};
  std::int32_t last_x_{0};
  std::int32_t last_y_{0};
  std::int32_t last_w_{0};
  std::int32_t last_h_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_CHAT_PRESENTER_H_
