#include "apptraverse/platform/windows/win_ui_presenters.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/windows/win_ui_environment.h"

#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {
namespace {

constexpr UINT kValidateLayoutChildren = WM_APP + 0x311;
constexpr wchar_t kDesktopWindowClass[] = L"AppTraverseUiDesktopWindow";
constexpr std::uint16_t kBnClicked = 0;
constexpr std::uint16_t kLbnSelchange = 1;
constexpr std::uint16_t kLbnDblclk = 2;

void SetUserData(HWND hwnd, void* value) {
  SetLastError(ERROR_SUCCESS);
  auto const previous =
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
  if (previous == 0 && GetLastError() != ERROR_SUCCESS) {
    FatalWin32("SetWindowLongPtrW Ui presenter", GetLastError());
  }
}

Presenter* PresenterFromChildHwnd(HWND child) {
  if (child == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<Presenter*>(GetWindowLongPtrW(child, GWLP_USERDATA));
}

void ApplyScrollInfoIfChanged(HWND hwnd, int bar, SCROLLINFO requested) {
  // SetScrollInfo normalizes these values. Compare that effective request,
  // otherwise a finite layout maximum below the viewport redraws forever.
  requested.nPage = std::min(requested.nPage,
      static_cast<UINT>(requested.nMax - requested.nMin + 1));
  int const last_position = requested.nMax -
      std::max(static_cast<int>(requested.nPage) - 1, 0);
  requested.nPos = std::clamp(requested.nPos, requested.nMin, last_position);
  SCROLLINFO current{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
  GetScrollInfo(hwnd, bar, &current);
  if (requested.nMin != current.nMin || requested.nMax != current.nMax ||
      requested.nPage != current.nPage || requested.nPos != current.nPos)
    SetScrollInfo(hwnd, bar, &requested, TRUE);
}

WinDesktopWindowPresenter* DesktopFromHwnd(HWND hwnd) {
  return reinterpret_cast<WinDesktopWindowPresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
void ApplyListLayoutFromMirror(HWND hwnd, std::int32_t layout_x,
                               std::int32_t layout_y,
                               std::int32_t layout_width,
                               std::int32_t layout_height) {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  ReadChildClientFrame(hwnd, x, y, width, height);
  if (x == layout_x && y == layout_y && width == layout_width &&
      height == layout_height) {
    return;
  }
  if (SetWindowPos(hwnd, nullptr, layout_x, layout_y, layout_width,
                   layout_height, SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos Ui list", err);
  }
}

}  // namespace

LRESULT CALLBACK WinDesktopWindowPresenter::WndProc(HWND hwnd, UINT msg,
                                                  WPARAM wparam,
                                                  LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* presenter = DesktopFromHwnd(hwnd);
  if (presenter == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == kValidateLayoutChildren) {
    if (!presenter->NativeMeasurementsReady()) FatalWin32("Missing required native layout control", ERROR_INVALID_DATA);
    presenter->RefreshLayout();
    return 0;
  }
  if (msg == WM_CONTEXTMENU && presenter->ShowPresentationContextMenu(
      static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam)))) return 0;
  if (msg == WM_SIZE) {
    presenter->RefreshLayout();
  }
  if (msg == kWinPresentationSettingsChanged) {
    // A queued notification may outlive a closed HWND. Its environment token
    // prevents a recycled window handle from consuming that stale callback.
    if (presenter->environment_ && presenter->environment_->AcceptsNotification(wparam))
      presenter->RefreshPresentationSettings();
    return 0;
  }
  if (msg == WM_SETTINGCHANGE || msg == WM_THEMECHANGED || msg == WM_SYSCOLORCHANGE ||
      msg == WM_DISPLAYCHANGE || msg == WM_DEVICECHANGE || msg == WM_POWERBROADCAST) {
    presenter->RefreshPresentationSettings();
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == 0x02E0) {  // WM_DPICHANGED, optional on Windows newer than Win7.
    auto const* rect = reinterpret_cast<RECT const*>(lparam);
    if (rect != nullptr && !presenter->applying_local_placement_) SetWindowPos(hwnd, nullptr, rect->left, rect->top,
        rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
    presenter->RefreshPresentationSettings();
    return 0;
  }
  if (msg == WM_VSCROLL || msg == WM_HSCROLL) {
    presenter->ScrollContent(msg == WM_VSCROLL ? SB_VERT : SB_HORZ, wparam);
    return 0;
  }
  if (msg == WM_MOUSEWHEEL && presenter->LayoutRoot() != nullptr) {
    presenter->wheel_delta_ += GET_WHEEL_DELTA_WPARAM(wparam);
    int const steps = presenter->wheel_delta_ / WHEEL_DELTA;
    presenter->wheel_delta_ %= WHEEL_DELTA;
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    UINT const amount = lines == WHEEL_PAGESCROLL ? 1 : lines;
    UINT const command = lines == WHEEL_PAGESCROLL
        ? (steps > 0 ? SB_PAGEUP : SB_PAGEDOWN)
        : (steps > 0 ? SB_LINEUP : SB_LINEDOWN);
    for (int step = 0; step < std::abs(steps); ++step)
      for (UINT line = 0; line < amount; ++line)
        presenter->ScrollContent(SB_VERT, MAKEWPARAM(command, 0));
    return 0;
  }
  if (msg == WM_ERASEBKGND && presenter->environment_) {
    RECT rect{}; GetClientRect(hwnd, &rect);
    FillRect(reinterpret_cast<HDC>(wparam), &rect, presenter->environment_->background_brush());
    return 1;
  }
  if ((msg == WM_CTLCOLORLISTBOX || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLOREDIT) && presenter->environment_) {
    HDC const dc = reinterpret_cast<HDC>(wparam);
    SetTextColor(dc, presenter->environment_->foreground());
    SetBkColor(dc, presenter->environment_->background());
    return reinterpret_cast<LRESULT>(presenter->environment_->background_brush());
  }
  if (msg == WM_DRAWITEM && presenter->PaintOwnerButton(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam))) return TRUE;
  if (msg == WM_COMMAND && (HIWORD(wparam) == BN_SETFOCUS || HIWORD(wparam) == LBN_SETFOCUS)) presenter->EnsureFocusedControlVisible();
  if (msg == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE) {
    presenter->NotifyActivatedFromUi();
  }
  if (msg == WM_COMMAND) {
    HWND const child = reinterpret_cast<HWND>(lparam);
    if (child == nullptr && presenter->OnCommand(
        static_cast<std::uint32_t>(LOWORD(wparam)),
        static_cast<std::uint16_t>(HIWORD(wparam)))) return 0;
    if (presenter->RouteChildCommand(static_cast<std::uint32_t>(LOWORD(wparam)),
                                     static_cast<std::uint16_t>(HIWORD(wparam)),
                                     child)) {
      return 0;
    }
  }
  if (msg == WM_ENTERSIZEMOVE) {
    presenter->interactive_move_active_ = true;
  }
  if (msg == WM_EXITSIZEMOVE) {
    presenter->interactive_move_active_ = false;
    if (presenter->pending_requested_frame_) {
      auto const requested = *presenter->pending_requested_frame_;
      presenter->pending_requested_frame_.reset();
      presenter->ApplyRequestedNativeFrame(requested);
    } else if (!presenter->applying_native_frame_) {
      presenter->HandleNativeFrameChanged(true);
    }
    presenter->ReportEnvironmentFromNative(UiEnvironmentReason::kUserWindowChange);
  }
  if (msg == WM_WINDOWPOSCHANGED && !presenter->applying_native_frame_ &&
      !presenter->applying_layout_ && !presenter->applying_settings_) {
    auto const* position = reinterpret_cast<WINDOWPOS const*>(lparam);
    // Windows emits FRAMECHANGED with both NOMOVE and NOSIZE when themes or
    // native scrollbars update. Recalculate its client layout via WM_SIZE, but
    // do not turn a nonclient appearance change into a model window-frame Event.
    bool const geometry_changed = position == nullptr ||
        (position->flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE);
    if (geometry_changed) {
      if (!presenter->applying_local_placement_)
        presenter->ReportEnvironmentFromNative();
      if (presenter->interactive_move_active_) {
        presenter->TrackInteractiveNativeFrame();
      } else {
        presenter->HandleNativeFrameChanged(false);
      }
    }
  }
  if (msg == WM_CLOSE) {
    if (!presenter->applying_native_frame_) {
      presenter->HandleNativeFrameChanged();
    }
    auto* host =
        static_cast<WinPresentationHost*>(presenter->presentation_host);
    host->on_desktop_window_user_close(host->opaque,
                                       presenter->window.operator->());
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::wstring WinDesktopWindowPresenter::ResolveTitleText() const {
  return Utf8ToWide(ResolvePresentedWindowTitle(locale->language_index()));
}

void WinDesktopWindowPresenter::OnLoad() {
  EnsureWinProcessDpiAwareness();
  auto const desktop = desktop_window();
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &WinDesktopWindowPresenter::WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kDesktopWindowClass;
  static ATOM const window_class = RegisterClassW(&wc);
  if (window_class == 0) {
    FatalWin32("RegisterClassW Ui desktop window", GetLastError());
  }

  int outer_w = 0;
  int outer_h = 0;
  HMENU const menu = CreatePresentationMenu();
  ClientSizeToOuter(desktop->restore_client_width(),
                    desktop->restore_client_height(), outer_w, outer_h, menu != nullptr);
  auto const title = ResolveTitleText();
  hwnd_ = CreateWindowExW(0, kDesktopWindowClass, title.c_str(),
                          WindowStyleForClientFrame() | WS_CLIPCHILDREN,
                          desktop->HasExplicitPosition() ? desktop->x() : CW_USEDEFAULT,
                          desktop->HasExplicitPosition() ? desktop->y() : CW_USEDEFAULT, outer_w, outer_h, nullptr, menu,
                          instance, nullptr);
  if (hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW Ui desktop window", GetLastError());
  }
  SetUserData(hwnd_, this);
  ApplyLocalPlacement();
  if (!LocalDeviceState()->PlacementFor(window.id())) KeepInCurrentWorkArea();
  RefreshPresentationSettings();
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  ReportEnvironmentFromNative();
}

WinDesktopWindowPresenter::ClientFrame
WinDesktopWindowPresenter::ReadMirrorClientFrame() const {
  ClientFrame frame{};
  auto const desktop = desktop_window();
  frame.x = desktop->x();
  frame.y = desktop->y();
  frame.client_width = desktop->restore_client_width();
  frame.client_height = desktop->restore_client_height();
  return frame;
}

bool WinDesktopWindowPresenter::FramesEqual(ClientFrame a, ClientFrame b) {
  return a.x == b.x && a.y == b.y && a.client_width == b.client_width &&
         a.client_height == b.client_height;
}

void WinDesktopWindowPresenter::TrackInteractiveNativeFrame() {
  HandleNativeFrameChanged(false);
}

WinDesktopWindowPresenter::ClientFrame
WinDesktopWindowPresenter::ReadNativeClientFrame() const {
  ClientFrame frame{};
  ReadModelFrameFromHwnd(hwnd_, frame.x, frame.y, frame.client_width,
                         frame.client_height);
  return frame;
}

void WinDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  if (hwnd_ == nullptr) return;
  ClientFrame const mirror = ReadMirrorClientFrame();
  if (interactive_move_active_) {
    // Publication intent is consumed once. Retain its target across the native
    // modal drag, including later unrelated mirror publications.
    pending_requested_frame_ = mirror;
    return;
  }
  ApplyRequestedNativeFrame(mirror);
}

void WinDesktopWindowPresenter::ApplyRequestedNativeFrame(ClientFrame mirror) {
  ClientFrame const native = ReadNativeClientFrame();
  if (FramesEqual(mirror, native)) {
    return;
  }
  applying_native_frame_ = true;
  int outer_w = 0;
  int outer_h = 0;
  ClientSizeToOuter(mirror.client_width, mirror.client_height, outer_w,
                    outer_h, GetMenu(hwnd_) != nullptr);
  if (!SetWindowPos(hwnd_, nullptr, mirror.x, mirror.y, outer_w, outer_h,
                    SWP_NOZORDER | SWP_NOACTIVATE)) {
    FatalWin32("SetWindowPos Ui desktop window", GetLastError());
  }
  applying_native_frame_ = false;
  ReportEnvironmentFromNative(UiEnvironmentReason::kUserWindowChange);
}

void WinDesktopWindowPresenter::HandleNativeFrameChanged(
    bool updates_restore_geometry) {
  if (applying_native_frame_ || applying_layout_ || applying_settings_) {
    return;
  }
  if (hwnd_ == nullptr) return;
  ReportEnvironmentFromNative(updates_restore_geometry
      ? UiEnvironmentReason::kUserWindowChange : UiEnvironmentReason::kSystemNotification);
}

bool WinDesktopWindowPresenter::RouteChildCommand(std::uint32_t command_id,
                                                  std::uint16_t notification_code,
                                                  HWND child) {
  if (child == nullptr ||
      static_cast<std::uint32_t>(GetDlgCtrlID(child)) != command_id) {
    return false;
  }
  auto* child_presenter = PresenterFromChildHwnd(child);
  if (child_presenter == nullptr) {
    return false;
  }
  return child_presenter->OnCommand(command_id, notification_code);
}

void WinDesktopWindowPresenter::OnModelChanged() {
  ApplyLocalPlacement();
  auto const* local = LocalDeviceState();
  if (local && local->appearance != applied_appearance_) RefreshPresentationSettings();
  RefreshLayout();
  SetNativeWindowTextIfChanged(hwnd_, ResolveTitleText());
}

void WinDesktopWindowPresenter::OnUnload() {
  SetLastError(0);
  LONG_PTR const previous = SetWindowLongPtrW(
      hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(nullptr));
  if (previous == 0 && GetLastError() != 0) {
    FatalWin32("SetWindowLongPtrW GWLP_USERDATA", GetLastError());
  }
  if (DestroyWindow(hwnd_) == 0) {
    FatalWin32("DestroyWindow Ui desktop window", GetLastError());
  }
  hwnd_ = nullptr;
  native_children_.clear();
  environment_.reset();
  last_reported_environment_.reset();
  last_reported_window_.reset();
  applied_placement_revision_ = 0;
  interactive_move_active_ = false;
  pending_requested_frame_.reset();
}

WinDesktopWindowPresenter* WinDesktopWindowPresenter::FromNativeWindow(HWND window) {
  wchar_t class_name[64]{};
  if (!GetClassNameW(window, class_name, 64) || wcscmp(class_name, kDesktopWindowClass) != 0) return nullptr;
  return DesktopFromHwnd(window);
}

double WinDesktopWindowPresenter::LayoutScale() const {
  return environment_ ? environment_->scale() : 1.0;
}
double WinDesktopWindowPresenter::TextScale() const {
  return environment_ ? environment_->text_scale() : 1.0;
}
bool WinDesktopWindowPresenter::HighContrast() const {
  return environment_ && environment_->high_contrast();
}
bool WinDesktopWindowPresenter::DarkTheme() const {
  return environment_ && environment_->dark();
}

COLORREF WinDesktopWindowPresenter::BackgroundColor() const {
  return environment_ ? environment_->background() : GetSysColor(COLOR_WINDOW);
}

void WinDesktopWindowPresenter::ApplyChildAppearance(HWND child) {
  if (!environment_) return;
  SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(environment_->font()), TRUE);
  wchar_t class_name[32]{};
  GetClassNameW(child, class_name, 32);
  if ((wcscmp(class_name, L"Edit") == 0 &&
       (GetWindowLongPtrW(child, GWL_STYLE) & ES_MULTILINE)) ||
      wcscmp(class_name, L"ListBox") == 0) {
    HMODULE theme = LoadLibraryW(L"uxtheme.dll");
    if (theme) {
      using SetTheme = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
      auto set_theme = reinterpret_cast<SetTheme>(
          GetProcAddress(theme, "SetWindowTheme"));
      if (set_theme)
        set_theme(child, environment_->dark() ? L"DarkMode_Explorer" : nullptr,
                  nullptr);
      FreeLibrary(theme);
    }
  }
  if (wcscmp(class_name, L"Button") == 0) {
    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    style = (style & ~BS_TYPEMASK) | (environment_->dark() ? BS_OWNERDRAW : BS_PUSHBUTTON);
    SetWindowLongPtrW(child, GWL_STYLE, style | BS_MULTILINE | BS_NOTIFY | WS_TABSTOP);
    SetWindowPos(child, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  InvalidateRect(child, nullptr, TRUE);
}

void WinDesktopWindowPresenter::RefreshPresentationSettings() {
  if (hwnd_ == nullptr || applying_settings_) return;
  // Font rebinding, native appearance and scrollbar updates may synchronously
  // emit nonclient/frame messages. They are local presentation effects, not a
  // user request to persist a new window frame or publish a model viewport.
  applying_settings_ = true;
  if (!environment_) environment_ = std::make_shared<WinUiEnvironment>(hwnd_);
  auto const* local = LocalDeviceState();
  applied_appearance_ = local->appearance;
  environment_->Refresh(applied_appearance_);
  for (auto const& child : native_children_) ApplyChildAppearance(child.hwnd);
  applying_settings_ = false;
  RefreshLayout();
  InvalidateRect(hwnd_, nullptr, TRUE);
  ReportEnvironmentFromNative();
}

void WinDesktopWindowPresenter::ReportEnvironmentFromNative(UiEnvironmentReason window_reason) {
  auto const* local = LocalDeviceState();
  if (!environment_ || applying_local_placement_) return;
  auto const& current = environment_->snapshot();
  if (!last_reported_environment_ || *last_reported_environment_ != current) {
    auto const reason = last_reported_environment_ ? UiEnvironmentReason::kSystemNotification
                                                  : UiEnvironmentReason::kStartup;
    model_proxy->Invoke(local->obj_id, &UiDeviceState::ObserveEnvironment, current, reason);
    last_reported_environment_ = current;
  }
  if (IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
  auto const window_environment = environment_->WindowSnapshot(window.id());
  if (!last_reported_window_ || *last_reported_window_ != window_environment ||
      window_reason == UiEnvironmentReason::kUserWindowChange) {
    auto const reason = last_reported_window_ ? window_reason : UiEnvironmentReason::kStartup;
    model_proxy->Invoke(local->obj_id, &UiDeviceState::ObserveWindow, window_environment, reason);
    last_reported_window_ = window_environment;
  }
}

void WinDesktopWindowPresenter::ApplyLocalPlacement() {
  auto const* local = LocalDeviceState();
  if (!hwnd_ || applying_local_placement_) return;
  auto const* placement = local->PlacementFor(window.id());
  if (!placement || placement->revision == applied_placement_revision_) return;
  if (applied_placement_revision_ == 0 && placement->revision == 1 && last_reported_window_ &&
      placement->resolved_monitor == last_reported_window_->monitor_key &&
      placement->frame_dip == last_reported_window_->frame_dip) {
    // The first preference for this newly created window was just observed
    // from native UI; do not replay its older queued geometry over a live drag.
    applied_placement_revision_ = placement->revision;
    return;
  }
  auto monitor = std::find_if(local->environment.monitors.begin(), local->environment.monitors.end(),
      [&](auto const& item) { return item.key == placement->resolved_monitor; });
  // A temporarily headless environment has no placement to apply yet.
  if (monitor == local->environment.monitors.end()) return;
  auto const& work = monitor->work_area;
  UINT const previous_dpi = GetWinWindowDpi(hwnd_);
  applying_local_placement_ = true;
  bool const prior_native = applying_native_frame_;
  applying_native_frame_ = true;
  if (GetWinWindowMonitorKey(hwnd_) != monitor->key) {
    // Fit the staging frame entirely inside the selected monitor before asking
    // the HWND for its DPI. A large source frame must not retain another monitor.
    if (!SetWindowPos(hwnd_, nullptr, work.x, work.y,
        std::max(1, std::min(64, work.width)), std::max(1, std::min(64, work.height)),
        SWP_NOZORDER | SWP_NOACTIVATE))
      FatalWin32("SetWindowPos resolved monitor", GetLastError());
  }
  auto const dpi = GetWinWindowDpi(hwnd_);
  auto const& dip = placement->frame_dip;
  int const width = std::clamp(MulDiv(dip.width, dpi, 96), 1, std::max(1, work.width));
  int const height = std::clamp(MulDiv(dip.height, dpi, 96), 1, std::max(1, work.height));
  int const x = work.x + std::clamp(MulDiv(dip.x, dpi, 96), 0, std::max(0, work.width - width));
  int const y = work.y + std::clamp(MulDiv(dip.y, dpi, 96), 0, std::max(0, work.height - height));
  RECT current{}; GetWindowRect(hwnd_, &current);
  if (current.left != x || current.top != y || current.right - current.left != width ||
      current.bottom - current.top != height) {
    if (!SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE))
      FatalWin32("SetWindowPos local placement", GetLastError());
  }
  applied_placement_revision_ = placement->revision;
  applying_native_frame_ = prior_native;
  applying_local_placement_ = false;
  if (environment_) {
    if (previous_dpi != GetWinWindowDpi(hwnd_)) RefreshPresentationSettings();
    else ReportEnvironmentFromNative();
  }
}

void WinDesktopWindowPresenter::KeepInCurrentWorkArea() {
  if (hwnd_ == nullptr || IsIconic(hwnd_) || IsZoomed(hwnd_)) return;
  MONITORINFO info{sizeof(info)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info))
    FatalWin32("GetMonitorInfoW work area", GetLastError());
  RECT bounds{};
  if (!GetWindowRect(hwnd_, &bounds)) FatalWin32("GetWindowRect work area", GetLastError());
  auto const& work = info.rcWork;
  LONG const width = std::min(bounds.right - bounds.left, work.right - work.left);
  LONG const height = std::min(bounds.bottom - bounds.top, work.bottom - work.top);
  LONG const x = std::clamp(bounds.left, work.left, work.right - width);
  LONG const y = std::clamp(bounds.top, work.top, work.bottom - height);
  if (x != bounds.left || y != bounds.top || width != bounds.right - bounds.left ||
      height != bounds.bottom - bounds.top) {
    if (!SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE))
      FatalWin32("SetWindowPos work area", GetLastError());
  }
}

void WinDesktopWindowPresenter::RegisterChildControl(HWND child, std::uint32_t control_id) {
  native_children_.push_back({child, control_id});
  ApplyChildAppearance(child);
  RefreshLayout();
}
void WinDesktopWindowPresenter::UnregisterChildControl(HWND child) {
  std::erase_if(native_children_, [child](auto const& item) { return item.hwnd == child; });
}

bool WinDesktopWindowPresenter::NativeMeasurementsReady() const {
  UiLayoutNode const* root = LayoutRoot();
  auto ready = [&](auto&& self, UiLayoutNode const& node) -> bool {
    if (node.flow == UiLayoutFlow::kLeaf) {
      return std::any_of(native_children_.begin(), native_children_.end(), [&](auto const& child) {
        return child.control_id == node.control_id;
      });
    }
    return std::all_of(node.children.begin(), node.children.end(), [&](auto const& child) {
      return self(self, child);
    });
  };
  return ready(ready, *root);
}

UiConstraintLayoutResult WinDesktopWindowPresenter::ComputeLocalLayout() const {
  UiLayoutNode const* const root = LayoutRoot();
  if (hwnd_ == nullptr) return {};
  double const scale = LayoutScale();
  RECT client{};
  if (!GetClientRect(hwnd_, &client)) FatalWin32("GetClientRect local layout", GetLastError());
  HDC const dc = GetDC(hwnd_);
  if (!dc) FatalWin32("GetDC control measurements", GetLastError());
  HGDIOBJ const previous = SelectObject(dc, environment_->font());
  std::vector<UiLayoutMeasurement> measurements;
  for (auto const& child : native_children_) {
    wchar_t class_name[32]{}; GetClassNameW(child.hwnd, class_name, 32);
    bool const button = wcscmp(class_name, L"Button") == 0;
    if (!button) {
      TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
      auto const line=(metrics.tmHeight+metrics.tmExternalLeading)/scale;
      bool const label=wcscmp(class_name,L"Static")==0;
      bool const edit=wcscmp(class_name,L"Edit")==0;
      bool const multiline=edit&&(GetWindowLongPtrW(child.hwnd,GWL_STYLE)&ES_MULTILINE);
      bool const list=wcscmp(class_name,L"ListBox")==0;
      double preferred_width=0;
      double minimum_width=0;
      if(label){
        int const length=GetWindowTextLengthW(child.hwnd);std::wstring text(length+1,L'\0');GetWindowTextW(child.hwnd,text.data(),length+1);
        RECT bounds{};DrawTextW(dc,text.c_str(),length,&bounds,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
        preferred_width=(bounds.right-bounds.left)/scale;
        std::size_t begin=0;
        while(begin<static_cast<std::size_t>(length)){
          auto end=text.find_first_of(L" \t\r\n",begin);if(end==std::wstring::npos||end>static_cast<std::size_t>(length))end=length;
          SIZE word{};GetTextExtentPoint32W(dc,text.data()+begin,static_cast<int>(end-begin),&word);minimum_width=std::max(minimum_width,word.cx/scale);begin=end+1;
        }
      }
      measurements.push_back({child.control_id,label?minimum_width:metrics.tmAveCharWidth/scale*4,
          line*(multiline||list?3:1)+8,preferred_width,line+8});
      continue;
    }
    int const length = GetWindowTextLengthW(child.hwnd);
    std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(child.hwnd, text.data(), length + 1); text.resize(length);
    RECT full{};
    DrawTextW(dc, text.c_str(), length, &full, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
    double longest_word = 0;
    std::size_t begin = 0;
    while (begin < text.size()) {
      auto end = text.find_first_of(L" \t\r\n", begin);
      if (end == std::wstring::npos) end = text.size();
      SIZE word{};
      GetTextExtentPoint32W(dc, text.data() + begin, static_cast<int>(end - begin), &word);
      longest_word = std::max(longest_word, static_cast<double>(word.cx));
      begin = end + 1;
    }
    measurements.push_back({child.control_id,
        longest_word / scale + 24, (full.bottom - full.top) / scale + 12,
        (full.right - full.left) / scale + 24, (full.bottom - full.top) / scale + 12});
  }
  auto result = ComputeUiConstraintLayout(*root, client.right / scale, client.bottom / scale, measurements);
  // Width-dependent text wrapping is measured with the actual native font.
  // Monotonic height refinement avoids oscillation between alternative layouts.
  for (int pass = 0; pass < 3; ++pass) {
    bool changed = false;
    for (auto& measured : measurements) {
      auto rect = std::find_if(result.controls.begin(), result.controls.end(), [&](auto const& item) {
        return item.control_id == measured.control_id;
      });
      auto child = std::find_if(native_children_.begin(), native_children_.end(), [&](auto const& item) {
        return item.control_id == measured.control_id;
      });
      if (rect == result.controls.end() || child == native_children_.end()) continue;
      wchar_t class_name[32]{}; GetClassNameW(child->hwnd, class_name, 32);
      bool const button=wcscmp(class_name,L"Button")==0;
      bool const label=wcscmp(class_name,L"Static")==0;
      if(!button&&!label)continue;
      double const horizontal_padding=button?24:0;
      double const vertical_padding=button?12:8;
      int const length = GetWindowTextLengthW(child->hwnd);
      std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
      GetWindowTextW(child->hwnd, text.data(), length + 1);
      RECT text_rect{0, 0, std::max(1, static_cast<int>(std::floor((rect->width - horizontal_padding) * scale))), 0};
      DrawTextW(dc, text.c_str(), length, &text_rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
      double const height = (text_rect.bottom - text_rect.top) / scale + vertical_padding;
      if (height > measured.min_height + 0.01) {
        measured.min_height = height;
        measured.preferred_height = std::max(measured.preferred_height, height);
        changed = true;
      }
    }
    if (!changed) break;
    result = ComputeUiConstraintLayout(*root, client.right / scale, client.bottom / scale, measurements);
  }
  SelectObject(dc, previous); ReleaseDC(hwnd_, dc);
  if (!result.valid) FatalWin32(result.error.c_str(), ERROR_INVALID_DATA);
  return result;
}

UiControlLayout WinDesktopWindowPresenter::ResolveChildLayout(UiControlLayout requested) const {
  if (environment_ == nullptr || !NativeMeasurementsReady()) {
    // Siblings are created synchronously before their first complete layout.
    // Use a provisional native box, never serialized measured rectangles.
    return {0, 0, 1, 1, requested.control_id};
  }
  auto const result = ComputeLocalLayout();
  auto const found = std::find_if(result.controls.begin(), result.controls.end(), [&](auto const& rect) {
    return rect.control_id == requested.control_id;
  });
  if (found == result.controls.end()) FatalWin32("Missing required child in layout", ERROR_INVALID_DATA);
  double const scale = LayoutScale();
  int const left = static_cast<int>(std::lround(found->x * scale));
  int const top = static_cast<int>(std::lround(found->y * scale));
  int const right = static_cast<int>(std::lround((found->x + found->width) * scale));
  int const bottom = static_cast<int>(std::lround((found->y + found->height) * scale));
  return {left - scroll_x_, top - scroll_y_, std::max(1, right - left),
          std::max(1, bottom - top), requested.control_id};
}

void WinDesktopWindowPresenter::RefreshLayout() {
  if (applying_layout_ || !environment_) return;
  if (!NativeMeasurementsReady()) {
    // InitializePresenters creates siblings synchronously. Validate at the next
    // UI dispatch boundary, after the complete publication has been presented.
    if (!PostMessageW(hwnd_, kValidateLayoutChildren, 0, 0)) FatalWin32("PostMessageW layout readiness", GetLastError());
    return;
  }
  applying_layout_ = true;
  UiConstraintLayoutResult result;
  for (int pass = 0; pass < 2; ++pass) {
    result = ComputeLocalLayout();
    RECT client{}; GetClientRect(hwnd_, &client);
    SCROLLINFO horizontal{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    horizontal.nMax = std::max(0, static_cast<int>(std::ceil(result.content_width * LayoutScale())) - 1);
    horizontal.nPage = static_cast<UINT>(client.right); horizontal.nPos = scroll_x_;
    ApplyScrollInfoIfChanged(hwnd_, SB_HORZ, horizontal);
    SCROLLINFO vertical{sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE | SIF_POS};
    vertical.nMax = std::max(0, static_cast<int>(std::ceil(result.content_height * LayoutScale())) - 1);
    vertical.nPage = static_cast<UINT>(client.bottom); vertical.nPos = scroll_y_;
    ApplyScrollInfoIfChanged(hwnd_, SB_VERT, vertical);
    scroll_x_ = GetScrollPos(hwnd_, SB_HORZ); scroll_y_ = GetScrollPos(hwnd_, SB_VERT);
  }
  // The final scrollbar update may itself alter the native client viewport.
  result = ComputeLocalLayout();
  for (auto const& child : native_children_) {
    if((GetWindowLongPtrW(child.hwnd,GWL_STYLE)&WS_VISIBLE)==0)continue;
    auto const found = std::find_if(result.controls.begin(), result.controls.end(), [&](auto const& rect) {
      return rect.control_id == child.control_id;
    });
    if (found == result.controls.end()) FatalWin32("Missing required child in layout", ERROR_INVALID_DATA);
    double const scale = LayoutScale();
    int const left = static_cast<int>(std::lround(found->x * scale));
    int const top = static_cast<int>(std::lround(found->y * scale));
    int const right = static_cast<int>(std::lround((found->x + found->width) * scale));
    int const bottom = static_cast<int>(std::lround((found->y + found->height) * scale));
    SetNativeChildFrameIfChanged(child.hwnd, left - scroll_x_, top - scroll_y_,
                                std::max(1, right - left), std::max(1, bottom - top));
  }
  applying_layout_ = false;
}
void WinDesktopWindowPresenter::ScrollContent(int bar, WPARAM command) {
  SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(hwnd_, bar, &info);
  int position = info.nPos;
  switch (LOWORD(command)) {
    case SB_LINEUP: position -= static_cast<int>(24 * LayoutScale()); break;
    case SB_LINEDOWN: position += static_cast<int>(24 * LayoutScale()); break;
    case SB_PAGEUP: position -= static_cast<int>(info.nPage); break;
    case SB_PAGEDOWN: position += static_cast<int>(info.nPage); break;
    case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
    case SB_TOP: position = info.nMin; break;
    case SB_BOTTOM: position = info.nMax; break;
    default: return;
  }
  SetScrollPos(hwnd_, bar, position, TRUE);
  if (bar == SB_VERT) scroll_y_ = GetScrollPos(hwnd_, bar); else scroll_x_ = GetScrollPos(hwnd_, bar);
  RefreshLayout();
}
void WinDesktopWindowPresenter::EnsureFocusedControlVisible() {
  HWND const focused = GetFocus();
  if (!focused || GetParent(focused) != hwnd_ || LayoutRoot() == nullptr) return;
  int x, y, width, height; ReadChildClientFrame(focused, x, y, width, height);
  RECT client{}; GetClientRect(hwnd_, &client);
  if (y < 0) scroll_y_ += y;
  else if (y + height > client.bottom) scroll_y_ += y + height - client.bottom;
  if (x < 0) scroll_x_ += x;
  else if (x + width > client.right) scroll_x_ += x + width - client.right;
  RefreshLayout();
}
bool WinDesktopWindowPresenter::PaintOwnerButton(DRAWITEMSTRUCT const& item) {
  if (!environment_ || !environment_->dark() || item.CtlType != ODT_BUTTON) return false;
  bool const disabled = (item.itemState & ODS_DISABLED) != 0;
  COLORREF background = environment_->control_background();
  if (item.itemState & ODS_SELECTED) background = RGB(65, 65, 65);
  HBRUSH brush = CreateSolidBrush(background);
  FillRect(item.hDC, &item.rcItem, brush); DeleteObject(brush);
  brush = CreateSolidBrush(environment_->border()); FrameRect(item.hDC, &item.rcItem, brush); DeleteObject(brush);
  SetBkMode(item.hDC, TRANSPARENT);
  SetTextColor(item.hDC, disabled ? environment_->disabled_foreground() : environment_->foreground());
  HGDIOBJ const previous = SelectObject(item.hDC, environment_->font());
  int const length = GetWindowTextLengthW(item.hwndItem);
  std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(item.hwndItem, text.data(), length + 1);
  RECT content = item.rcItem; InflateRect(&content, -static_cast<int>(12 * LayoutScale()), -static_cast<int>(6 * LayoutScale()));
  RECT measured = content;
  DrawTextW(item.hDC, text.c_str(), length, &measured, DT_CALCRECT | DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
  content.top += std::max(0L, (content.bottom - content.top - measured.bottom + measured.top) / 2);
  DrawTextW(item.hDC, text.c_str(), length, &content, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(item.hDC, previous);
  if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
    RECT focus = item.rcItem; InflateRect(&focus, -3, -3); DrawFocusRect(item.hDC, &focus);
  }
  return true;
}

WinDesktopWindowPresenter* WinListContainerPresenter::Desktop() const {
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinListContainerPresenter::OnLoad() {
  auto* desktop = Desktop();
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", L"",
      // The common solver owns the exact outer rectangle. Default integral
      // item sizing silently rounds this height and defeats native equality.
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
      layout_x, layout_y, layout_width, layout_height, desktop->hwnd(),
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
      GetModuleHandleW(nullptr), nullptr);
  if (hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW Ui list", GetLastError());
  }
  EnableWindow(hwnd_, TRUE);
  SetUserData(hwnd_, this);
  desktop->RegisterChildControl(hwnd_, control_id);
  SyncFromMirror();
}

bool WinListContainerPresenter::OnCommand(std::uint32_t command_id,
                                          std::uint16_t notification_code) {
  (void)command_id;
  return HandleListNotification(notification_code);
}

bool WinListContainerPresenter::HandleListNotification(
    std::uint16_t notification_code) {
  if (notify_list_selection_change &&
      notification_code == kLbnSelchange) {
    LRESULT const sel = SendMessageW(hwnd_, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) {
      return true;
    }
    SubmitListRowSelectionChangedFromUi(static_cast<std::size_t>(sel));
    return true;
  }
  if (notification_code != kLbnDblclk) {
    return false;
  }
  LRESULT const sel = SendMessageW(hwnd_, LB_GETCURSEL, 0, 0);
  if (sel == LB_ERR) {
    return true;
  }
  SubmitListRowActivatedFromUi(static_cast<std::size_t>(sel));
  return true;
}

namespace {

std::vector<std::wstring> ReadListBoxStrings(HWND hwnd) {
  LRESULT const count = SendMessageW(hwnd, LB_GETCOUNT, 0, 0);
  if (count == LB_ERR) {
    FatalWin32("LB_GETCOUNT Ui list", ERROR_INVALID_DATA);
  }
  std::vector<std::wstring> out;
  out.reserve(static_cast<std::size_t>(count));
  for (LRESULT i = 0; i < count; ++i) {
    LRESULT const len = SendMessageW(hwnd, LB_GETTEXTLEN, static_cast<WPARAM>(i),
                                     0);
    if (len == LB_ERR) {
      FatalWin32("LB_GETTEXTLEN Ui list", ERROR_INVALID_DATA);
    }
    std::wstring text(static_cast<std::size_t>(len) + 1, L'\0');
    if (SendMessageW(hwnd, LB_GETTEXT, static_cast<WPARAM>(i),
                     reinterpret_cast<LPARAM>(text.data())) == LB_ERR) {
      FatalWin32("LB_GETTEXT Ui list", ERROR_INVALID_DATA);
    }
    text.resize(static_cast<std::size_t>(len));
    out.push_back(std::move(text));
  }
  return out;
}

void ApplyPublishedListSelection(HWND hwnd, bool notify_list_selection_change,
                                 ui::UiListContainer const* container,
                                 std::size_t row_count) {
  if (!notify_list_selection_change) {
    return;
  }
  std::int32_t const desired = container->selection_index;
  LRESULT const current_sel = SendMessageW(hwnd, LB_GETCURSEL, 0, 0);
  if (desired >= 0 && desired < static_cast<std::int32_t>(row_count)) {
    if (current_sel != desired) {
      if (SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(desired), 0) == LB_ERR) {
        FatalWin32("LB_SETCURSEL Ui list", ERROR_INVALID_DATA);
      }
    }
    return;
  }
  if (desired == -1 && current_sel != LB_ERR) {
    // Clearing the selection returns LB_ERR even on success.
    SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
  }
}

void RestoreListSelection(HWND hwnd, LRESULT selection) {
  if (SendMessageW(hwnd, LB_SETCURSEL, static_cast<WPARAM>(selection), 0) == LB_ERR) {
    FatalWin32("LB_SETCURSEL Ui list restore", ERROR_INVALID_DATA);
  }
}

void RestoreListTop(HWND hwnd, LRESULT previous_top, std::size_t row_count) {
  if (previous_top < 0 || row_count == 0) return;
  auto const top = std::min(previous_top, static_cast<LRESULT>(row_count - 1));
  if (SendMessageW(hwnd, LB_SETTOPINDEX, static_cast<WPARAM>(top), 0) == LB_ERR) {
    FatalWin32("LB_SETTOPINDEX Ui list", ERROR_INVALID_DATA);
  }
}

}  // namespace

void WinListContainerPresenter::SyncFromMirror() {
  auto const apply_layout = [this] {
    ui::UiControlLayout const layout = window_presenter->ResolveChildLayout(
        {layout_x, layout_y, layout_width, layout_height, control_id});
    ApplyListLayoutFromMirror(hwnd_, layout.x, layout.y, layout.width, layout.height);
    Desktop()->RefreshLayout();
  };
  int const language_index = window_presenter->locale->language_index();
  std::vector<std::string> const labels =
      ResolveListRowPrimaryTexts(language_index);
  std::vector<std::wstring> desired;
  desired.reserve(labels.size());
  for (std::string const& label : labels) {
    desired.push_back(Utf8ToWide(label));
  }

  std::vector<std::wstring> const current = ReadListBoxStrings(hwnd_);
  if (current == desired) {
    ApplyPublishedListSelection(hwnd_, notify_list_selection_change,
                                container.operator->(), desired.size());
    apply_layout();
    return;
  }

  LRESULT const previous_sel = SendMessageW(hwnd_, LB_GETCURSEL, 0, 0);
  LRESULT const previous_top = SendMessageW(hwnd_, LB_GETTOPINDEX, 0, 0);

  if (current.size() == desired.size()) {
    for (std::size_t i = 0; i < desired.size(); ++i) {
      if (current[i] == desired[i]) {
        continue;
      }
      if (SendMessageW(hwnd_, LB_DELETESTRING, static_cast<WPARAM>(i), 0) == LB_ERR) {
        FatalWin32("LB_DELETESTRING Ui list", ERROR_INVALID_DATA);
      }
      if (SendMessageW(hwnd_, LB_INSERTSTRING, static_cast<WPARAM>(i),
                       reinterpret_cast<LPARAM>(desired[i].c_str())) < 0) {
        FatalWin32("LB_INSERTSTRING Ui list", ERROR_NOT_ENOUGH_MEMORY);
      }
    }
    if (notify_list_selection_change) {
      ApplyPublishedListSelection(hwnd_, notify_list_selection_change,
                                  container.operator->(), desired.size());
    } else if (previous_sel != LB_ERR && previous_sel >= 0 &&
               previous_sel < static_cast<LRESULT>(desired.size())) {
      RestoreListSelection(hwnd_, previous_sel);
    }
    RestoreListTop(hwnd_, previous_top, desired.size());
    apply_layout();
    return;
  }

  SendMessageW(hwnd_, LB_RESETCONTENT, 0, 0);
  for (auto const& text : desired) {
    if (SendMessageW(hwnd_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text.c_str())) < 0) {
      FatalWin32("LB_ADDSTRING Ui list", ERROR_NOT_ENOUGH_MEMORY);
    }
  }
  if (notify_list_selection_change) {
    ApplyPublishedListSelection(hwnd_, notify_list_selection_change,
                                container.operator->(), desired.size());
  } else if (previous_sel != LB_ERR && previous_sel >= 0 &&
             previous_sel < static_cast<LRESULT>(desired.size())) {
    RestoreListSelection(hwnd_, previous_sel);
  }
  RestoreListTop(hwnd_, previous_top, desired.size());
  apply_layout();
}

void WinListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void WinListContainerPresenter::OnUnload() {
  Desktop()->UnregisterChildControl(hwnd_);
  SetUserData(hwnd_, nullptr);
  if (DestroyWindow(hwnd_) == 0) {
    FatalWin32("DestroyWindow Ui list", GetLastError());
  }
  hwnd_ = nullptr;
}

}  // namespace apptraverse::ui::windows
