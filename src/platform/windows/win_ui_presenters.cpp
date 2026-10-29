#include "apptraverse/platform/windows/win_ui_presenters.h"

#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {
namespace {

constexpr wchar_t kDesktopWindowClass[] = L"AppTraverseUiDesktopWindow";
constexpr std::uint16_t kBnClicked = 0;
constexpr std::uint16_t kLbnSelchange = 1;
constexpr std::uint16_t kLbnDblclk = 2;

void SetUserData(HWND hwnd, void* value) {
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
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

bool CreateFailed(HWND hwnd) { return hwnd == nullptr; }

}  // namespace

#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
int WinDesktopWindowPresenter::test_set_window_pos_count_{0};

int WinDesktopWindowPresenter::TestSetWindowPosCount() {
  return test_set_window_pos_count_;
}

void WinDesktopWindowPresenter::TestResetSetWindowPosCount() {
  test_set_window_pos_count_ = 0;
}

void WinDesktopWindowPresenter::TestReportNativeFrame(std::int32_t x,
                                                      std::int32_t y,
                                                      std::int32_t client_width,
                                                      std::int32_t client_height) {
  (void)x;
  (void)y;
  (void)client_width;
  (void)client_height;
}

void WinDesktopWindowPresenter::TestMoveNativeOuter(std::int32_t x,
                                                    std::int32_t y,
                                                    std::int32_t client_width,
                                                    std::int32_t client_height) {
  if (hwnd_ == nullptr) {
    return;
  }
  applying_native_frame_ = true;
  int outer_w = 0;
  int outer_h = 0;
  ClientSizeToOuter(client_width, client_height, outer_w, outer_h);
  SetWindowPos(hwnd_, nullptr, x, y, outer_w, outer_h,
               SWP_NOZORDER | SWP_NOACTIVATE);
  applying_native_frame_ = false;
  if (interactive_move_active_) {
    TrackInteractiveNativeFrame();
  }
}

void WinDesktopWindowPresenter::TestSimulateInteractiveNativeMove(
    std::int32_t x, std::int32_t y, std::int32_t client_width,
    std::int32_t client_height) {
  TestSetInteractiveMoveActive(true);
  TestMoveNativeOuter(x, y, client_width, client_height);
}

void WinDesktopWindowPresenter::TestFinishInteractiveNativeMove() {
  TestSetInteractiveMoveActive(false);
  HandleNativeFrameChanged();
}
#endif

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
    presenter->interactive_move_active_ = false;
    if (!presenter->applying_native_frame_) {
      presenter->HandleNativeFrameChanged();
    }
  }
  if (msg == WM_WINDOWPOSCHANGED && !presenter->applying_native_frame_) {
    if (presenter->interactive_move_active_) {
      presenter->TrackInteractiveNativeFrame();
    } else {
      presenter->HandleNativeFrameChanged();
    }
  }
  if (msg == WM_CLOSE) {
    if (!presenter->applying_native_frame_) {
      presenter->HandleNativeFrameChanged();
    }
    auto* host =
        static_cast<WinPresentationHost*>(presenter->presentation_host);
    if (host != nullptr) {
      if (host->on_desktop_window_user_close != nullptr) {
        host->on_desktop_window_user_close(host->opaque,
                                           presenter->window.operator->());
      } else if (host->notify_hwnd != nullptr) {
        PostMessageW(host->notify_hwnd, WM_CLOSE, 0, 0);
      }
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::wstring WinDesktopWindowPresenter::ResolveTitleText() const {
  if (!title || !locale) {
    return {};
  }
  return Utf8ToWide(title->Resolve(locale->language_index()));
}

void WinDesktopWindowPresenter::OnLoad() {
  auto const desktop = desktop_window();
  if (!desktop) {
    return;
  }
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &WinDesktopWindowPresenter::WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kDesktopWindowClass;
  RegisterClassW(&wc);

  int outer_w = 0;
  int outer_h = 0;
  ClientSizeToOuter(desktop->client_width(), desktop->client_height(), outer_w,
                    outer_h);
  auto const title = ResolveTitleText();
  hwnd_ = CreateWindowExW(0, kDesktopWindowClass, title.c_str(),
                          WindowStyleForClientFrame(), desktop->x(),
                          desktop->y(), outer_w, outer_h, nullptr, nullptr,
                          instance, nullptr);
  if (CreateFailed(hwnd_)) {
    return;
  }
  SetUserData(hwnd_, this);
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
}

WinDesktopWindowPresenter::ClientFrame
WinDesktopWindowPresenter::ReadMirrorClientFrame() const {
  ClientFrame frame{};
  auto const desktop = desktop_window();
  if (!desktop) {
    return frame;
  }
  frame.x = desktop->x();
  frame.y = desktop->y();
  frame.client_width = desktop->client_width();
  frame.client_height = desktop->client_height();
  return frame;
}

bool WinDesktopWindowPresenter::FramesEqual(ClientFrame a, ClientFrame b) {
  return a.x == b.x && a.y == b.y && a.client_width == b.client_width &&
         a.client_height == b.client_height;
}

void WinDesktopWindowPresenter::TrackInteractiveNativeFrame() {
  if (hwnd_ == nullptr) {
    return;
  }
  (void)ReadNativeClientFrame();
}

WinDesktopWindowPresenter::ClientFrame
WinDesktopWindowPresenter::ReadNativeClientFrame() const {
  ClientFrame frame{};
  if (hwnd_ == nullptr) {
    return frame;
  }
  ReadModelFrameFromHwnd(hwnd_, frame.x, frame.y, frame.client_width,
                         frame.client_height);
  return frame;
}

void WinDesktopWindowPresenter::ApplyRequestedFrameFromMirror() {
  if (interactive_move_active_ || hwnd_ == nullptr) {
    return;
  }
  auto const desktop = desktop_window();
  if (!desktop) {
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
#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
  ++test_set_window_pos_count_;
#endif
  SetWindowPos(hwnd_, nullptr, mirror.x, mirror.y, outer_w, outer_h,
               SWP_NOZORDER | SWP_NOACTIVATE);
  applying_native_frame_ = false;
}

void WinDesktopWindowPresenter::HandleNativeFrameChanged() {
  if (applying_native_frame_) {
    return;
  }
  auto const desktop = desktop_window();
  if (hwnd_ == nullptr || !desktop) {
    return;
  }
  ClientFrame const native = ReadNativeClientFrame();
#if defined(APPTRAVERSE_WIN_DESKTOP_FRAME_SYNC_TEST)
  ++test_model_frame_submit_count_;
#endif
  SubmitFrameFromNative(native.x, native.y, native.client_width,
                        native.client_height);
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
  if (hwnd_ == nullptr) {
    return;
  }
  SetWindowTextW(hwnd_, ResolveTitleText().c_str());
}

void WinDesktopWindowPresenter::OnUnload() {
  if (hwnd_ == nullptr) {
    return;
  }
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
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinListContainerPresenter::OnLoad() {
  auto* desktop = Desktop();
  if (desktop == nullptr || desktop->hwnd() == nullptr || !container) {
    return;
  }
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"LISTBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
      layout_x, layout_y, layout_width, layout_height, desktop->hwnd(),
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
      GetModuleHandleW(nullptr), nullptr);
  if (CreateFailed(hwnd_)) {
    return;
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
  if (count == LB_ERR || count <= 0) {
    return {};
  }
  std::vector<std::wstring> out;
  out.reserve(static_cast<std::size_t>(count));
  for (LRESULT i = 0; i < count; ++i) {
    LRESULT const len = SendMessageW(hwnd, LB_GETTEXTLEN, static_cast<WPARAM>(i),
                                     0);
    if (len == LB_ERR || len <= 0) {
      out.emplace_back();
      continue;
    }
    std::wstring text(static_cast<std::size_t>(len), L'\0');
    SendMessageW(hwnd, LB_GETTEXT, static_cast<WPARAM>(i),
                 reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<std::size_t>(len));
    out.push_back(std::move(text));
  }
  return out;
}

}  // namespace

void WinListContainerPresenter::SyncFromMirror() {
  if (hwnd_ == nullptr || !container) {
    return;
  }
  int language_index = 0;
  if (window_presenter && window_presenter->locale) {
    language_index = window_presenter->locale->language_index();
  }
  std::vector<std::wstring> desired;
  desired.reserve(container->rows.size());
  for (auto const& row : container->rows) {
    if (!row || !row->primary_text) {
      continue;
    }
    desired.push_back(
        Utf8ToWide(row->primary_text->Resolve(language_index)));
  }

  std::vector<std::wstring> const current = ReadListBoxStrings(hwnd_);
  if (current == desired) {
    if (notify_list_selection_change && container &&
        container->selection_index >= 0 &&
        container->selection_index < static_cast<std::int32_t>(desired.size())) {
      LRESULT const cur_sel = SendMessageW(hwnd_, LB_GETCURSEL, 0, 0);
      if (cur_sel != container->selection_index) {
        SendMessageW(hwnd_, LB_SETCURSEL,
                     static_cast<WPARAM>(container->selection_index), 0);
      }
    }
    return;
  }

  LRESULT const previous_sel = SendMessageW(hwnd_, LB_GETCURSEL, 0, 0);
  LRESULT const previous_top = SendMessageW(hwnd_, LB_GETTOPINDEX, 0, 0);

  if (current.size() == desired.size()) {
    for (std::size_t i = 0; i < desired.size(); ++i) {
      if (current[i] == desired[i]) {
        continue;
      }
      SendMessageW(hwnd_, LB_DELETESTRING, static_cast<WPARAM>(i), 0);
      SendMessageW(hwnd_, LB_INSERTSTRING, static_cast<WPARAM>(i),
                   reinterpret_cast<LPARAM>(desired[i].c_str()));
    }
    if (previous_sel != LB_ERR && previous_sel >= 0 &&
        previous_sel < static_cast<LRESULT>(desired.size())) {
      SendMessageW(hwnd_, LB_SETCURSEL, static_cast<WPARAM>(previous_sel), 0);
    }
    if (previous_top != LB_ERR && previous_top >= 0) {
      SendMessageW(hwnd_, LB_SETTOPINDEX, static_cast<WPARAM>(previous_top), 0);
    }
    return;
  }

  SendMessageW(hwnd_, LB_RESETCONTENT, 0, 0);
  for (auto const& text : desired) {
    SendMessageW(hwnd_, LB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(text.c_str()));
  }
  if (previous_sel != LB_ERR && previous_sel >= 0 &&
      previous_sel < static_cast<LRESULT>(desired.size())) {
    SendMessageW(hwnd_, LB_SETCURSEL, static_cast<WPARAM>(previous_sel), 0);
  }
  if (previous_top != LB_ERR && previous_top >= 0) {
    SendMessageW(hwnd_, LB_SETTOPINDEX, static_cast<WPARAM>(previous_top), 0);
  }

  if (notify_list_selection_change && container &&
      container->selection_index >= 0 &&
      container->selection_index < static_cast<std::int32_t>(desired.size())) {
    SendMessageW(hwnd_, LB_SETCURSEL,
                 static_cast<WPARAM>(container->selection_index), 0);
  }
}

void WinListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void WinListContainerPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

}  // namespace apptraverse::ui::windows
