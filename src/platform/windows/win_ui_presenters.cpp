#include "apptraverse/platform/windows/win_ui_presenters.h"

#include <algorithm>

#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {
namespace {

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
  if (msg == WM_ACTIVATE && LOWORD(wparam) != WA_INACTIVE) {
    presenter->NotifyActivatedFromUi();
  }
  if (msg == WM_COMMAND) {
    HWND const child = reinterpret_cast<HWND>(lparam);
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
    if (!presenter->applying_native_frame_) {
      presenter->HandleNativeFrameChanged(true);
    }
    presenter->interactive_move_active_ = false;
  }
  if (msg == WM_WINDOWPOSCHANGED && !presenter->applying_native_frame_) {
    if (presenter->interactive_move_active_) {
      presenter->TrackInteractiveNativeFrame();
    } else {
      presenter->HandleNativeFrameChanged(false);
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
  ClientSizeToOuter(desktop->restore_client_width(),
                    desktop->restore_client_height(), outer_w, outer_h);
  auto const title = ResolveTitleText();
  hwnd_ = CreateWindowExW(0, kDesktopWindowClass, title.c_str(),
                          WindowStyleForClientFrame(),
                          desktop->HasExplicitPosition() ? desktop->x() : CW_USEDEFAULT,
                          desktop->HasExplicitPosition() ? desktop->y() : CW_USEDEFAULT, outer_w, outer_h, nullptr, nullptr,
                          instance, nullptr);
  if (hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW Ui desktop window", GetLastError());
  }
  SetUserData(hwnd_, this);
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  ClientFrame const initial = ReadNativeClientFrame();
  last_native_client_width_ = initial.client_width;
  last_native_client_height_ = initial.client_height;
  ReportNativeFrameToModel(initial.x, initial.y, initial.client_width,
                           initial.client_height, true);
  MONITORINFO monitor{sizeof(MONITORINFO)};
  if (!GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) {
    FatalWin32("GetMonitorInfoW Ui desktop window", GetLastError());
  }
  auto const& work = monitor.rcWork;
  SubmitScreenFromNative(work.left, work.top, work.right - work.left,
                         work.bottom - work.top);
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
  if (interactive_move_active_ || hwnd_ == nullptr) {
    return;
  }
  ClientFrame const mirror = ReadMirrorClientFrame();
  ClientFrame const native = ReadNativeClientFrame();
  if (FramesEqual(mirror, native)) {
    return;
  }
  applying_native_frame_ = true;
  int outer_w = 0;
  int outer_h = 0;
  ClientSizeToOuter(mirror.client_width, mirror.client_height, outer_w,
                    outer_h);
  if (!SetWindowPos(hwnd_, nullptr, mirror.x, mirror.y, outer_w, outer_h,
                    SWP_NOZORDER | SWP_NOACTIVATE)) {
    FatalWin32("SetWindowPos Ui desktop window", GetLastError());
  }
  applying_native_frame_ = false;
}

void WinDesktopWindowPresenter::HandleNativeFrameChanged(
    bool updates_restore_geometry) {
  if (applying_native_frame_) {
    return;
  }
  auto const desktop = desktop_window();
  if (hwnd_ == nullptr) {
    return;
  }
  ClientFrame const native = ReadNativeClientFrame();
  bool const client_dims_changed =
      native.client_width != last_native_client_width_ ||
      native.client_height != last_native_client_height_;
  if (interactive_move_active_ && !updates_restore_geometry) {
    if (!client_dims_changed) {
      return;
    }
    ReportNativeFrameToModel(native.x, native.y, native.client_width,
                             native.client_height, false);
    last_native_client_width_ = native.client_width;
    last_native_client_height_ = native.client_height;
    return;
  }
  ReportNativeFrameToModel(native.x, native.y, native.client_width,
                           native.client_height, updates_restore_geometry);
  if (client_dims_changed || updates_restore_geometry) {
    last_native_client_width_ = native.client_width;
    last_native_client_height_ = native.client_height;
  }
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
  if (!SetWindowTextW(hwnd_, ResolveTitleText().c_str())) {
    FatalWin32("SetWindowTextW Ui desktop window", GetLastError());
  }
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
}

WinDesktopWindowPresenter* WinListContainerPresenter::Desktop() const {
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinListContainerPresenter::OnLoad() {
  auto* desktop = Desktop();
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
      layout_x, layout_y, layout_width, layout_height, desktop->hwnd(),
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
      GetModuleHandleW(nullptr), nullptr);
  if (hwnd_ == nullptr) {
    FatalWin32("CreateWindowExW Ui list", GetLastError());
  }
  EnableWindow(hwnd_, TRUE);
  SetUserData(hwnd_, this);
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
    ApplyListLayoutFromMirror(hwnd_, layout_x, layout_y, layout_width,
                              layout_height);
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
  SetUserData(hwnd_, nullptr);
  if (DestroyWindow(hwnd_) == 0) {
    FatalWin32("DestroyWindow Ui list", GetLastError());
  }
  hwnd_ = nullptr;
}

}  // namespace apptraverse::ui::windows
