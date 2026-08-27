#ifndef APPTRAVERSE_WIN_PRESENTERS_H_
#define APPTRAVERSE_WIN_PRESENTERS_H_

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

// Runtime-only child presenter. Not an ae::Obj; created by WinLayoutWindowPresenter.
class WinRuntimeCenterStripPresenter {
 public:
  CenterStrip::ptr strip;
  std::function<void()> on_activate;
  std::function<void(std::uint32_t)> on_remove;
  HWND parent_hwnd{nullptr};
  HWND hwnd{nullptr};

  void Create() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &WinRuntimeCenterStripPresenter::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = nullptr;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, 0,
                           0, parent_hwnd, nullptr, GetModuleHandleW(nullptr),
                           this);
    last_generation_ = strip->Generation();
    last_color_ = strip->fill_color;
  }

  void Present() {
    if (strip->Generation() == last_generation_) {
      return;
    }
    last_generation_ = strip->Generation();
    if (strip->fill_color != last_color_) {
      last_color_ = strip->fill_color;
      InvalidateRect(hwnd, nullptr, FALSE);
    }
  }

  void Destroy() {
    if (hwnd != nullptr) {
      DestroyWindow(hwnd);
      hwnd = nullptr;
    }
  }

  ~WinRuntimeCenterStripPresenter() { Destroy(); }

 private:
  static constexpr wchar_t const* kClassName = L"AppTraverseCenterStrip";

  static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam) {
    if (msg == WM_NCCREATE) {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* self =
          static_cast<WinRuntimeCenterStripPresenter*>(cs->lpCreateParams);
      SetWindowLongPtrW(wnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      self->hwnd = wnd;
      return DefWindowProcW(wnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<WinRuntimeCenterStripPresenter*>(
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
    if (msg == WM_LBUTTONUP) {
      if (self->on_activate) {
        self->on_activate();
      }
      return 0;
    }
    if (msg == WM_RBUTTONUP) {
      if (self->on_remove) {
        self->on_remove(self->strip->obj_id.id());
      }
      return 0;
    }
    return DefWindowProcW(wnd, msg, wparam, lparam);
  }

  std::uint64_t last_generation_{0};
  std::uint32_t last_color_{0};
};

// Reconcile runtime strip presenters to match LayoutWindow.center_strips.
// Creates missing presenters and Destroys entries whose ObjId left the vector.
inline void ReconcileRuntimeCenterStripPresenters(
    LayoutWindow& window, HWND parent_hwnd,
    std::function<void()> const& on_add,
    std::function<void(std::uint32_t)> const& on_remove,
    std::unordered_map<std::uint32_t,
                       std::unique_ptr<WinRuntimeCenterStripPresenter>>&
        presenters) {
  std::unordered_set<std::uint32_t> current;
  current.reserve(window.center_strips.size());
  for (auto& strip_ptr : window.center_strips) {
    assert(strip_ptr.is_valid());
    strip_ptr.Load();
    auto const id = strip_ptr.id().id();
    current.insert(id);
    if (presenters.count(id) > 0) {
      continue;
    }
    auto presenter = std::make_unique<WinRuntimeCenterStripPresenter>();
    presenter->strip = strip_ptr;
    presenter->parent_hwnd = parent_hwnd;
    presenter->on_activate = on_add;
    presenter->on_remove = on_remove;
    presenter->Create();
    presenters.emplace(id, std::move(presenter));
  }
  for (auto it = presenters.begin(); it != presenters.end();) {
    if (current.count(it->first) == 0) {
      it->second->Destroy();
      it = presenters.erase(it);
    } else {
      ++it;
    }
  }
}

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
    hwnd = CreateWindowExW(0, kClassName, L"Window A",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
      // Do not fight the live drag: model bounds catch up on EXITSIZEMOVE.
      if (!interactive_size_move_) {
        ApplyModelBounds();
      }
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

  void MaybePostBoundsCommand(HWND wnd) {
    auto command = MakeBoundsCommand(wnd, window->obj_id.id());
    if (!BoundsMatchWindow(*window, command)) {
      commands(std::move(command));
    }
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
          SetWindowPos(paint_child, nullptr, 0, 0, LOWORD(lparam),
                       HIWORD(lparam),
                       SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
        return 0;
      case WM_ENTERSIZEMOVE:
        interactive_size_move_ = true;
        return 0;
      case WM_EXITSIZEMOVE:
        interactive_size_move_ = false;
        if (!creating_ && !applying_model_bounds_) {
          MaybePostBoundsCommand(wnd);
        }
        return 0;
      case WM_WINDOWPOSCHANGED: {
        auto const* wp = reinterpret_cast<WINDOWPOS*>(lparam);
        bool const geometry_changed =
            wp != nullptr &&
            (wp->flags & (SWP_NOMOVE | SWP_NOSIZE)) !=
                (SWP_NOMOVE | SWP_NOSIZE);
        // Interactive drag: keep layout local. Commit to Model only when the
        // size-move loop ends (or for non-interactive geometry changes).
        if (!creating_ && !applying_model_bounds_ && geometry_changed &&
            !interactive_size_move_) {
          MaybePostBoundsCommand(wnd);
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
  bool interactive_size_move_{false};
  bool creating_{false};
};

class WinLayoutWindowPresenter : public Presenter {
  APPTRAVERSE_OBJECT(WinLayoutWindowPresenter, Presenter, 0)

 protected:
  WinLayoutWindowPresenter() = default;

 public:
  explicit WinLayoutWindowPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(text_toolbar),
                    AE_MMBR(color_toolbar))

  LayoutWindow::ptr window;
  WinTextToolbarPresenter::ptr text_toolbar;
  WinColorToolbarPresenter::ptr color_toolbar;
  BoundsCommandFn commands;
  std::function<void()> on_close;
  std::function<void()> on_add_center_strip;
  std::function<void(std::uint32_t)> on_remove_center_strip;
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
    hwnd = CreateWindowExW(0, kClassName, L"Window B",
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
    text_toolbar->OnLoad();
    color_toolbar->OnLoad();
    ReconcileCenterStrips();
    LayoutChildren();
    commands(MakeBoundsCommand(hwnd, window->obj_id.id()));
  }

  void Present() {
    if (window->Generation() != last_generation_) {
      last_generation_ = window->Generation();
      if (!interactive_size_move_) {
        ApplyModelBounds();
      }
      ReconcileCenterStrips();
    }
    // During live drag, native client size is ahead of the Model publication.
    if (interactive_size_move_) {
      LayoutChildrenFromNativeClient();
    } else {
      LayoutChildren();
    }
    text_toolbar->Present();
    color_toolbar->Present();
    for (auto& [id, presenter] : center_strips_) {
      (void)id;
      presenter->Present();
    }
  }

  void Destroy() {
    center_strips_.clear();
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

  void MaybePostBoundsCommand(HWND wnd) {
    auto command = MakeBoundsCommand(wnd, window->obj_id.id());
    if (!BoundsMatchWindow(*window, command)) {
      commands(std::move(command));
    }
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
      case WM_ENTERSIZEMOVE:
        interactive_size_move_ = true;
        return 0;
      case WM_EXITSIZEMOVE:
        interactive_size_move_ = false;
        LayoutChildrenFromNativeClient();
        if (!creating_ && !applying_model_bounds_) {
          MaybePostBoundsCommand(wnd);
        }
        return 0;
      case WM_WINDOWPOSCHANGED: {
        auto const* wp = reinterpret_cast<WINDOWPOS*>(lparam);
        bool const geometry_changed =
            wp != nullptr &&
            (wp->flags & (SWP_NOMOVE | SWP_NOSIZE)) !=
                (SWP_NOMOVE | SWP_NOSIZE);
        if (!creating_ && geometry_changed) {
          // Relayout immediately from the native client size so children track
          // live resize without waiting for the Model→UI publication round-trip.
          LayoutChildrenFromNativeClient();
          if (!applying_model_bounds_ && !interactive_size_move_) {
            MaybePostBoundsCommand(wnd);
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

  void ReconcileCenterStrips() {
    ReconcileRuntimeCenterStripPresenters(
        *window, hwnd, on_add_center_strip, on_remove_center_strip,
        center_strips_);
  }

  void LayoutChildrenFromClient(std::int32_t client_width,
                                std::int32_t client_height) {
    MoveIfChanged(text_toolbar->hwnd,
                  TextToolbarNativeRect(*window, client_width), last_text_rect_);
    MoveIfChanged(color_toolbar->hwnd,
                  ColorToolbarNativeRect(*window, client_width),
                  last_color_rect_);
    last_strip_rects_.resize(window->center_strips.size());
    for (std::size_t i = 0; i < window->center_strips.size(); ++i) {
      auto const id = window->center_strips[i].id().id();
      auto it = center_strips_.find(id);
      assert(it != center_strips_.end());
      MoveIfChanged(it->second->hwnd,
                    CenterStripNativeRect(*window, i, client_width,
                                          client_height),
                    last_strip_rects_[i]);
    }
  }

  void LayoutChildrenFromNativeClient() {
    RECT client{};
    GetClientRect(hwnd, &client);
    LayoutChildrenFromClient(client.right - client.left,
                             client.bottom - client.top);
  }

  void LayoutChildren() {
    LayoutChildrenFromClient(window->client_width, window->client_height);
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

  std::unordered_map<std::uint32_t,
                     std::unique_ptr<WinRuntimeCenterStripPresenter>>
      center_strips_;
  NativeRect last_text_rect_{};
  NativeRect last_color_rect_{};
  std::vector<NativeRect> last_strip_rects_;
  std::uint64_t last_generation_{0};
  bool applying_model_bounds_{false};
  bool interactive_size_move_{false};
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
  std::function<void()> on_add_center_strip;
  std::function<void(std::uint32_t)> on_remove_center_strip;

  void OnLoad() override {
    paint_window->commands = commands;
    paint_window->on_close = on_close;
    layout_window->commands = commands;
    layout_window->on_close = on_close;
    layout_window->on_add_center_strip = on_add_center_strip;
    layout_window->on_remove_center_strip = on_remove_center_strip;
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
