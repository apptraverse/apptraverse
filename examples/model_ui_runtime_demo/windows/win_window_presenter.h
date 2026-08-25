#ifndef APPTRAVERSE_WIN_WINDOW_PRESENTER_H_
#define APPTRAVERSE_WIN_WINDOW_PRESENTER_H_

#include <cassert>
#include <functional>
#include <memory>
#include <string>

#include "demo_log.h"
#include "win_chat_presenter.h"
#include "win_color_toolbar_presenter.h"
#include "win_toolbar_presenter.h"
#include "win_util.h"

namespace apptraverse {

class WinWindowPresenter {
 public:
  using CommandFn = std::function<void(ModelCommand)>;

  void Create(RuntimeWindow const& runtime, wchar_t const* title, bool window_b,
              std::uint32_t window_id, CommandFn commands,
              ImmutableObjectStore const* store, UiRuntimeRegistry* registry,
              std::uint32_t text_id, std::uint32_t color_id,
              std::uint32_t chat_id) {
    runtime_ = &runtime;
    window_b_ = window_b;
    window_id_ = window_id;
    commands_ = std::move(commands);
    store_ = store;
    registry_ = registry;
    text_id_ = text_id;
    color_id_ = color_id;
    chat_id_ = chat_id;

    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinWindowPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        0, kClassName, title, WS_OVERLAPPEDWINDOW, runtime.left, runtime.top,
        runtime.right - runtime.left, runtime.bottom - runtime.top, nullptr,
        nullptr, GetModuleHandleW(nullptr), this);
    assert(hwnd_ != nullptr);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
  }

  void Present(RuntimeWindow const& runtime) {
    runtime_ = &runtime;
    if (runtime.generation != last_generation_) {
      last_generation_ = runtime.generation;
      ApplyModelBounds();
    }
    if (!window_b_ || registry_ == nullptr) {
      return;
    }
    if (text_) {
      auto* bar = registry_->Must<RuntimeTextToolbar>(text_id_);
      text_->Present(*bar);
    }
    if (color_) {
      auto* bar = registry_->Must<RuntimeColorToolbar>(color_id_);
      color_->Present(*bar);
    }
    if (chat_) {
      auto* chat = registry_->Must<RuntimeChat>(chat_id_);
      chat_->Present(*chat);
    }
  }

  HWND hwnd() const { return hwnd_; }
  std::uint64_t paint_count() const { return paint_count_; }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseModelUiWindow";
  static constexpr UINT_PTR kPaintTimer = 1;

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<WinWindowPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd_ = hwnd;
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinWindowPresenter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return self->Handle(hwnd, msg, wparam, lparam);
  }

  static LRESULT CALLBACK PaintChildProc(HWND hwnd, UINT msg, WPARAM wparam,
                                         LPARAM lparam) {
    auto* self = reinterpret_cast<WinWindowPresenter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_PAINT && self != nullptr) {
      self->PaintChild(hwnd);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_CREATE:
        OnCreate(hwnd);
        return 0;
      case WM_TIMER:
        if (wparam == kPaintTimer && paint_child_ != nullptr) {
          InvalidateRect(paint_child_, nullptr, FALSE);
        }
        return 0;
      case WM_SIZE:
        if (!window_b_ && paint_child_ != nullptr && wparam != SIZE_MINIMIZED) {
          MoveWindow(paint_child_, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        }
        return 0;
      case WM_WINDOWPOSCHANGED: {
        auto const* wp = reinterpret_cast<WINDOWPOS*>(lparam);
        bool const geometry_changed =
            wp != nullptr &&
            (wp->flags & (SWP_NOMOVE | SWP_NOSIZE)) !=
                (SWP_NOMOVE | SWP_NOSIZE);
        if (!applying_model_bounds_ && commands_ && geometry_changed) {
          commands_(MakeBoundsCommand(hwnd, window_id_));
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
      }
      case WM_COMMAND:
        if (window_b_ && chat_ && LOWORD(wparam) == 3 &&
            HIWORD(wparam) == BN_CLICKED) {
          chat_->OnSendClicked();
        }
        return 0;
      case WM_DESTROY:
        if (hwnd == hwnd_) {
          PostQuitMessage(0);
        }
        return 0;
      default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
  }

  void OnCreate(HWND hwnd) {
    if (!window_b_) {
      WNDCLASSW wc{};
      wc.lpfnWndProc = &WinWindowPresenter::PaintChildProc;
      wc.hInstance = GetModuleHandleW(nullptr);
      wc.lpszClassName = L"AppTraversePaintChild";
      wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
      RegisterClassW(&wc);
      RECT client{};
      GetClientRect(hwnd, &client);
      paint_child_ = CreateWindowExW(
          0, L"AppTraversePaintChild", L"", WS_CHILD | WS_VISIBLE, 0, 0,
          client.right, client.bottom, hwnd, nullptr, GetModuleHandleW(nullptr),
          nullptr);
      SetWindowLongPtrW(paint_child_, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(this));
      SetTimer(hwnd, kPaintTimer, 1000, nullptr);
      return;
    }
    text_ = std::make_unique<WinTextToolbarPresenter>();
    color_ = std::make_unique<WinColorToolbarPresenter>();
    chat_ = std::make_unique<WinChatPresenter>();
    auto* bar = registry_->Must<RuntimeTextToolbar>(text_id_);
    auto* color = registry_->Must<RuntimeColorToolbar>(color_id_);
    auto* chat = registry_->Must<RuntimeChat>(chat_id_);
    text_->Create(hwnd, *bar, *store_);
    color_->Create(hwnd, *color);
    chat_->Create(hwnd, *chat, [this](std::string text) {
      if (!commands_) {
        return;
      }
      AddMessageCommand command;
      command.chat_id = chat_id_;
      command.text = std::move(text);
      commands_(std::move(command));
    });
  }

  void PaintChild(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    ++paint_count_;
    COLORREF const fill = RGB(static_cast<int>(paint_count_ * 40 % 180) + 40,
                              80, 160);
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(hdc, &ps.rcPaint, brush);
    DeleteObject(brush);
    auto const gen = runtime_ != nullptr ? runtime_->generation : 0;
    std::wstring line = L"WM_PAINT count=" + std::to_wstring(paint_count_) +
                        L" WindowA gen=" + std::to_wstring(gen) +
                        L" (repaint != model update)";
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, 12, 12, line.c_str(), static_cast<int>(line.size()));
    demo::DemoLog("paint WindowA count=" + std::to_string(paint_count_) +
                  " gen=" + std::to_string(gen));
    EndPaint(hwnd, &ps);
  }

  void ApplyModelBounds() {
    if (hwnd_ == nullptr || runtime_ == nullptr) {
      return;
    }
    RECT outer{};
    GetWindowRect(hwnd_, &outer);
    int const w = runtime_->right - runtime_->left;
    int const h = runtime_->bottom - runtime_->top;
    if (outer.left == runtime_->left && outer.top == runtime_->top &&
        outer.right == runtime_->right && outer.bottom == runtime_->bottom) {
      return;
    }
    applying_model_bounds_ = true;
    SetWindowPos(hwnd_, nullptr, runtime_->left, runtime_->top, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applying_model_bounds_ = false;
  }

  HWND hwnd_{nullptr};
  HWND paint_child_{nullptr};
  RuntimeWindow const* runtime_{nullptr};
  bool window_b_{false};
  std::uint32_t window_id_{0};
  CommandFn commands_;
  ImmutableObjectStore const* store_{nullptr};
  UiRuntimeRegistry* registry_{nullptr};
  std::uint32_t text_id_{0};
  std::uint32_t color_id_{0};
  std::uint32_t chat_id_{0};
  std::unique_ptr<WinTextToolbarPresenter> text_;
  std::unique_ptr<WinColorToolbarPresenter> color_;
  std::unique_ptr<WinChatPresenter> chat_;
  std::uint64_t last_generation_{0};
  std::uint64_t paint_count_{0};
  bool applying_model_bounds_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_WINDOW_PRESENTER_H_
