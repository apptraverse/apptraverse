#ifndef APPTRAVERSE_WIN_PRESENTERS_H_
#define APPTRAVERSE_WIN_PRESENTERS_H_

#include <functional>

#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "demo_ids.h"
#include "demo_layout.h"
#include "demo_log.h"
#include "demo_model.h"
#include "win_util.h"

namespace apptraverse {

using BoundsCommandFn = std::function<void(WindowBoundsCommand)>;

class WinTextToolbarPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinTextToolbarPresenter, Presenter, 0)

 protected:
  WinTextToolbarPresenter() = default;

 public:
  explicit WinTextToolbarPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(toolbar))

  TextToolbar::ptr toolbar;
  std::function<void()> on_activate;
  HWND parent_hwnd{nullptr};
  HWND hwnd{nullptr};

  void OnLoad() override {
    hwnd = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY, 0, 0, 0, 0,
        parent_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlId)),
        GetModuleHandleW(nullptr), nullptr);
    ApplyText();
  }

  void Present() {
    auto const id = toolbar->text.id();
    if (id.id() != last_string_obj_id_.id()) {
      ApplyText();
    }
  }

  void Destroy() {
    hwnd = nullptr;
  }

  static constexpr int kControlId = 401;

 private:
  void ApplyText() {
    last_string_obj_id_ = toolbar->text.id();
    SetWindowTextW(hwnd, Utf8ToWide(toolbar->text->bytes).c_str());
  }

  ae::ObjId last_string_obj_id_{};
};

class WinColorToolbarPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinColorToolbarPresenter, Presenter, 0)

 protected:
  WinColorToolbarPresenter() = default;

 public:
  explicit WinColorToolbarPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(toolbar))

  ColorToolbar::ptr toolbar;
  HWND parent_hwnd{nullptr};
  HWND hwnd{nullptr};

  void OnLoad() override {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinColorToolbarPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0,
                           0, parent_hwnd, nullptr, GetModuleHandleW(nullptr),
                           this);
    last_generation_ = toolbar->Generation();
    last_color_ = toolbar->color;
  }

  void Present() {
    if (toolbar->Generation() == last_generation_) {
      return;
    }
    last_generation_ = toolbar->Generation();
    if (toolbar->color != last_color_) {
      last_color_ = toolbar->color;
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }

  void Destroy() { hwnd = nullptr; }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseColorToolbar";

  static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self =
          static_cast<WinColorToolbarPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd = wnd;
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinColorToolbarPresenter*>(
        GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(wnd, &ps);
      HBRUSH brush = CreateSolidBrush(self->toolbar->color);
      FillRect(hdc, &ps.rcPaint, brush);
      DeleteObject(brush);
      EndPaint(wnd, &ps);
      return 0;
    }
    return DefWindowProcW(wnd, msg, wparam, lparam);
  }

  std::uint64_t last_generation_{0};
  std::uint32_t last_color_{0};
};

class WinCenterStripPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinCenterStripPresenter, Presenter, 0)

 protected:
  WinCenterStripPresenter() = default;

 public:
  explicit WinCenterStripPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(strip))

  CenterStrip::ptr strip;
  HWND parent_hwnd{nullptr};
  HWND hwnd{nullptr};

  void OnLoad() override {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinCenterStripPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0,
                           0, parent_hwnd, nullptr, GetModuleHandleW(nullptr),
                           this);
    last_generation_ = strip->Generation();
  }

  void Present() { last_generation_ = strip->Generation(); }

  void Destroy() { hwnd = nullptr; }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseCenterStrip";

  static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self =
          static_cast<WinCenterStripPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd = wnd;
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinCenterStripPresenter*>(
        GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    if (msg == WM_PAINT) {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(wnd, &ps);
      HBRUSH brush = CreateSolidBrush(self->strip->fill_color);
      FillRect(hdc, &ps.rcPaint, brush);
      DeleteObject(brush);
      EndPaint(wnd, &ps);
      return 0;
    }
    if (msg == WM_ERASEBKGND) {
      return 1;
    }
    return DefWindowProcW(wnd, msg, wparam, lparam);
  }

  std::uint64_t last_generation_{0};
};

class WinPaintWindowPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinPaintWindowPresenter, Presenter, 0)

 protected:
  WinPaintWindowPresenter() = default;

 public:
  explicit WinPaintWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  PaintWindow::ptr window;
  BoundsCommandFn commands;
  std::function<void()> on_close;
  HWND hwnd{nullptr};
  HWND paint_child{nullptr};

  void OnLoad() override {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinPaintWindowPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    creating_ = true;
    hwnd = CreateWindowExW(0, kClassName, L"Window A", WS_OVERLAPPEDWINDOW,
                           window->left, window->top,
                           window->right - window->left,
                           window->bottom - window->top, nullptr, nullptr,
                           GetModuleHandleW(nullptr), this);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    creating_ = false;
    last_generation_ = window->Generation();
    commands(MakeBoundsCommand(hwnd, window->obj_id.id()));
  }

  void Present() {
    if (window->Generation() != last_generation_) {
      last_generation_ = window->Generation();
      ApplyModelBounds();
    }
  }

  void Destroy() {
    HWND h = hwnd;
    hwnd = nullptr;
    if (h == nullptr) {
      return;
    }
    if (paint_child != nullptr) {
      KillTimer(h, kPaintTimer);
      paint_child = nullptr;
    }
    DestroyWindow(h);
  }

  std::uint64_t paint_count() const { return paint_count_; }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraversePaintWindow";
  static constexpr UINT_PTR kPaintTimer = 1;

  static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self = static_cast<WinPaintWindowPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd = wnd;
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinPaintWindowPresenter*>(
        GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    return self->Handle(wnd, msg, wparam, lparam);
  }

  static LRESULT CALLBACK PaintChildProc(HWND wnd, UINT msg, WPARAM wparam,
                                         LPARAM lparam) {
    auto* self = reinterpret_cast<WinPaintWindowPresenter*>(
        GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (msg == WM_PAINT && self != nullptr) {
      self->PaintChild(wnd);
      return 0;
    }
    return DefWindowProcW(wnd, msg, wparam, lparam);
  }

  LRESULT Handle(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_CREATE:
        OnCreate(wnd);
        return 0;
      case WM_TIMER:
        if (wparam == kPaintTimer && paint_child != nullptr) {
          InvalidateRect(paint_child, nullptr, FALSE);
        }
        return 0;
      case WM_SIZE:
        if (paint_child != nullptr && wparam != SIZE_MINIMIZED) {
          MoveWindow(paint_child, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE);
        }
        return 0;
      case WM_WINDOWPOSCHANGED: {
        auto const* wp = reinterpret_cast<WINDOWPOS*>(lparam);
        bool const geometry_changed =
            wp != nullptr &&
            (wp->flags & (SWP_NOMOVE | SWP_NOSIZE)) !=
                (SWP_NOMOVE | SWP_NOSIZE);
        if (!creating_ && !applying_model_bounds_ && geometry_changed) {
          auto command = MakeBoundsCommand(wnd, window->obj_id.id());
          if (!BoundsMatchWindow(*window, command)) {
            commands(std::move(command));
          }
        }
        return DefWindowProcW(wnd, msg, wparam, lparam);
      }
      case WM_CLOSE:
        on_close();
        return 0;
      case WM_DESTROY:
        hwnd = nullptr;
        return 0;
      default:
        return DefWindowProcW(wnd, msg, wparam, lparam);
    }
  }

  void OnCreate(HWND wnd) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinPaintWindowPresenter::PaintChildProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AppTraversePaintChild";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    RECT client{};
    GetClientRect(wnd, &client);
    paint_child = CreateWindowExW(
        0, L"AppTraversePaintChild", L"", WS_CHILD | WS_VISIBLE, 0, 0,
        client.right, client.bottom, wnd, nullptr, GetModuleHandleW(nullptr),
        nullptr);
    SetWindowLongPtrW(paint_child, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    SetTimer(wnd, kPaintTimer, 1000, nullptr);
  }

  void PaintChild(HWND wnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(wnd, &ps);
    ++paint_count_;
    COLORREF const fill = RGB(static_cast<int>(paint_count_ * 40 % 180) + 40,
                              80, 160);
    HBRUSH brush = CreateSolidBrush(fill);
    FillRect(hdc, &ps.rcPaint, brush);
    DeleteObject(brush);
    auto const gen = window->Generation();
    std::wstring line = L"WM_PAINT count=" + std::to_wstring(paint_count_) +
                        L" WindowA gen=" + std::to_wstring(gen) +
                        L" (repaint != model update)";
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, 12, 12, line.c_str(), static_cast<int>(line.size()));
    demo::DemoLog("paint WindowA count=" + std::to_string(paint_count_) +
                  " gen=" + std::to_string(gen));
    EndPaint(wnd, &ps);
  }

  void ApplyModelBounds() {
    RECT outer{};
    GetWindowRect(hwnd, &outer);
    int const w = window->right - window->left;
    int const h = window->bottom - window->top;
    if (outer.left == window->left && outer.top == window->top &&
        outer.right == window->right && outer.bottom == window->bottom) {
      return;
    }
    applying_model_bounds_ = true;
    SetWindowPos(hwnd, nullptr, window->left, window->top, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applying_model_bounds_ = false;
  }

  std::uint64_t last_generation_{0};
  std::uint64_t paint_count_{0};
  bool applying_model_bounds_{false};
  bool creating_{false};
};

class WinLayoutWindowPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinLayoutWindowPresenter, Presenter, 0)

 protected:
  WinLayoutWindowPresenter() = default;

 public:
  explicit WinLayoutWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(text_toolbar),
                    AE_MMBR(color_toolbar), AE_MMBR(center_strip))

  LayoutWindow::ptr window;
  WinTextToolbarPresenter::ptr text_toolbar;
  WinColorToolbarPresenter::ptr color_toolbar;
  WinCenterStripPresenter::ptr center_strip;
  BoundsCommandFn commands;
  std::function<void()> on_close;
  HWND hwnd{nullptr};

  void OnLoad() override {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinLayoutWindowPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    creating_ = true;
    hwnd = CreateWindowExW(0, kClassName, L"Window B", WS_OVERLAPPEDWINDOW,
                           window->left, window->top,
                           window->right - window->left,
                           window->bottom - window->top, nullptr, nullptr,
                           GetModuleHandleW(nullptr), this);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    creating_ = false;
    last_generation_ = window->Generation();

    text_toolbar->parent_hwnd = hwnd;
    color_toolbar->parent_hwnd = hwnd;
    center_strip->parent_hwnd = hwnd;
    text_toolbar->OnLoad();
    color_toolbar->OnLoad();
    center_strip->OnLoad();
    LayoutChildren();
    commands(MakeBoundsCommand(hwnd, window->obj_id.id()));
  }

  void Present() {
    if (window->Generation() != last_generation_) {
      last_generation_ = window->Generation();
      ApplyModelBounds();
    }
    LayoutChildren();
    text_toolbar->Present();
    color_toolbar->Present();
    center_strip->Present();
  }

  void Destroy() {
    HWND h = hwnd;
    hwnd = nullptr;
    if (h == nullptr) {
      return;
    }
    DestroyWindow(h);
  }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseLayoutWindow";

  static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self =
          static_cast<WinLayoutWindowPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd = wnd;
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinLayoutWindowPresenter*>(
        GetWindowLongPtrW(wnd, GWLP_USERDATA));
    if (self == nullptr) {
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    return self->Handle(wnd, msg, wparam, lparam);
  }

  LRESULT Handle(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_COMMAND: {
        HWND const source = reinterpret_cast<HWND>(lparam);
        if (text_toolbar && text_toolbar->hwnd == source &&
            HIWORD(wparam) == STN_CLICKED) {
          if (text_toolbar->on_activate) {
            text_toolbar->on_activate();
          }
          return 0;
        }
        return DefWindowProcW(wnd, msg, wparam, lparam);
      }
      case WM_WINDOWPOSCHANGED: {
        auto const* wp = reinterpret_cast<WINDOWPOS*>(lparam);
        bool const geometry_changed =
            wp != nullptr &&
            (wp->flags & (SWP_NOMOVE | SWP_NOSIZE)) !=
                (SWP_NOMOVE | SWP_NOSIZE);
        if (!creating_ && !applying_model_bounds_ && geometry_changed) {
          auto command = MakeBoundsCommand(wnd, window->obj_id.id());
          if (!BoundsMatchWindow(*window, command)) {
            commands(std::move(command));
          }
        }
        return DefWindowProcW(wnd, msg, wparam, lparam);
      }
      case WM_CLOSE:
        on_close();
        return 0;
      case WM_DESTROY:
        hwnd = nullptr;
        return 0;
      default:
        return DefWindowProcW(wnd, msg, wparam, lparam);
    }
  }

  void LayoutChildren() {
    MoveIfChanged(text_toolbar->hwnd, TextToolbarNativeRect(*window),
                  last_text_rect_);
    MoveIfChanged(color_toolbar->hwnd, ColorToolbarNativeRect(*window),
                  last_color_rect_);
    MoveIfChanged(center_strip->hwnd, CenterStripNativeRect(*window),
                  last_strip_rect_);
  }

  void ApplyModelBounds() {
    RECT outer{};
    GetWindowRect(hwnd, &outer);
    int const w = window->right - window->left;
    int const h = window->bottom - window->top;
    if (outer.left == window->left && outer.top == window->top &&
        outer.right == window->right && outer.bottom == window->bottom) {
      return;
    }
    applying_model_bounds_ = true;
    SetWindowPos(hwnd, nullptr, window->left, window->top, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    applying_model_bounds_ = false;
  }

  NativeRect last_text_rect_{};
  NativeRect last_color_rect_{};
  NativeRect last_strip_rect_{};
  std::uint64_t last_generation_{0};
  bool applying_model_bounds_{false};
  bool creating_{false};
};

class WinPresentationApplication : public Presenter {
  APPTRAVERSE_OBJECT(WinPresentationApplication, Presenter, 0)

 protected:
  WinPresentationApplication() = default;

 public:
  explicit WinPresentationApplication(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(paint_window), AE_MMBR(layout_window))

  WinPaintWindowPresenter::ptr paint_window;
  WinLayoutWindowPresenter::ptr layout_window;
  BoundsCommandFn commands;
  std::function<void()> on_close;
  std::function<void()> on_text_toolbar_activate;

  void OnLoad() override {
    paint_window->commands = commands;
    paint_window->on_close = on_close;
    layout_window->commands = commands;
    layout_window->on_close = on_close;
    layout_window->text_toolbar->on_activate = on_text_toolbar_activate;
    paint_window->OnLoad();
    layout_window->OnLoad();
  }

  void PresentPaintWindow() { paint_window->Present(); }

  void PresentLayoutWindow() { layout_window->Present(); }

  void Destroy() {
    paint_window->Destroy();
    layout_window->Destroy();
  }
};

WinPresentationApplication::ptr BuildPresentationGraph(
    ae::Domain& domain, Application& application);

void EnsureWindowsPresenterRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WIN_PRESENTERS_H_
