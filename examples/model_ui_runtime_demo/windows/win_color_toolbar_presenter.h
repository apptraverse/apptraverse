#ifndef APPTRAVERSE_WIN_COLOR_TOOLBAR_PRESENTER_H_
#define APPTRAVERSE_WIN_COLOR_TOOLBAR_PRESENTER_H_

#include "win_util.h"
#include "ui_runtime_registry.h"

namespace apptraverse {

class WinColorToolbarPresenter {
 public:
  void Create(HWND parent, RuntimeColorToolbar const& runtime) {
    runtime_ = &runtime;
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinColorToolbarPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&wc);
    hwnd_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0,
                            0, parent, nullptr, GetModuleHandleW(nullptr),
                            this);
    ApplyBounds();
  }

  void Present(RuntimeColorToolbar const& runtime) {
    runtime_ = &runtime;
    if (runtime.generation == last_generation_) {
      return;
    }
    bool const color_changed = runtime.color != last_color_;
    last_generation_ = runtime.generation;
    ApplyBounds();
    if (color_changed) {
      last_color_ = runtime.color;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }

  HWND hwnd() const { return hwnd_; }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseColorToolbar";

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<WinColorToolbarPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hwnd;
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinColorToolbarPresenter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      auto const color = self->runtime_ != nullptr ? self->runtime_->color
                                                   : 0x00C04040u;
      HBRUSH brush = CreateSolidBrush(color);
      FillRect(hdc, &ps.rcPaint, brush);
      DeleteObject(brush);
      EndPaint(hwnd, &ps);
      return 0;
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
  RuntimeColorToolbar const* runtime_{nullptr};
  std::uint64_t last_generation_{0};
  std::uint32_t last_color_{0};
  std::int32_t last_x_{0};
  std::int32_t last_y_{0};
  std::int32_t last_w_{0};
  std::int32_t last_h_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_COLOR_TOOLBAR_PRESENTER_H_
