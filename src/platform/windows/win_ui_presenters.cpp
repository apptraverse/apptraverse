#include "apptraverse/platform/windows/win_ui_presenters.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {
namespace {

constexpr wchar_t kDesktopWindowClass[] = L"AppTraverseUiDesktopWindow";
constexpr wchar_t kBitmapViewportClass[] = L"AppTraverseUiBitmapViewport";
constexpr std::uint16_t kEnChange = 0x0300;
constexpr std::uint16_t kLbnSelchange = 1;
constexpr std::uint16_t kLbnDblclk = 2;

bool g_bitmap_viewport_class_registered{false};

LRESULT CALLBACK BitmapViewportWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam) {
  if (msg == WM_PAINT) {
    auto* presenter = reinterpret_cast<WinBitmapImagePresenter*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    PAINTSTRUCT ps{};
    HDC const hdc = BeginPaint(hwnd, &ps);
    if (presenter != nullptr) {
      presenter->PaintToHdc(hdc);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void EnsureBitmapViewportClass(HINSTANCE instance) {
  if (g_bitmap_viewport_class_registered) {
    return;
  }
  WNDCLASSW wc{};
  wc.lpfnWndProc = &BitmapViewportWndProc;
  wc.hInstance = instance;
  wc.lpszClassName = kBitmapViewportClass;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassW(&wc);
  g_bitmap_viewport_class_registered = true;
}

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
  RecordNativeFrameSubmission({x, y, client_width, client_height});
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
  RecordNativeFrameSubmission({x, y, client_width, client_height});
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
    presenter->HandleNativeFrameChanged();
  }
  if (msg == WM_CLOSE) {
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

void WinDesktopWindowPresenter::RecordNativeFrameSubmission(
    ClientFrame frame) {
  if (native_frame_submit_serial_ != 0 && frame.x == latest_native_frame_.x &&
      frame.y == latest_native_frame_.y &&
      frame.client_width == latest_native_frame_.client_width &&
      frame.client_height == latest_native_frame_.client_height) {
    return;
  }
  latest_native_frame_ = frame;
  ++native_frame_submit_serial_;
  if (auto const desktop = desktop_window()) {
    ui_generation_at_native_submit_ = desktop->Generation();
  }
  model_generation_at_native_submit_ = 0;
  NativeSubmitRecord record;
  record.serial = native_frame_submit_serial_;
  record.frame = frame;
  if (native_submit_history_count_ < kNativeSubmitHistoryCap) {
    native_submit_history_[native_submit_history_count_++] = record;
  } else {
    for (std::size_t i = 1; i < kNativeSubmitHistoryCap; ++i) {
      native_submit_history_[i - 1] = native_submit_history_[i];
    }
    native_submit_history_[kNativeSubmitHistoryCap - 1] = record;
  }
}

bool WinDesktopWindowPresenter::IsMirrorStaleNativeSubmission(
    ClientFrame mirror) const {
  for (std::size_t i = 0; i < native_submit_history_count_; ++i) {
    NativeSubmitRecord const& record = native_submit_history_[i];
    if (record.serial >= native_frame_submit_serial_) {
      continue;
    }
    if (record.serial <= native_frame_ack_serial_) {
      continue;
    }
    if (record.frame.x == mirror.x && record.frame.y == mirror.y &&
        record.frame.client_width == mirror.client_width &&
        record.frame.client_height == mirror.client_height) {
      if (record.frame.x != latest_native_frame_.x ||
          record.frame.y != latest_native_frame_.y ||
          record.frame.client_width != latest_native_frame_.client_width ||
          record.frame.client_height != latest_native_frame_.client_height) {
        return true;
      }
    }
  }
  return false;
}

void WinDesktopWindowPresenter::TryAcknowledgeMirrorFrame(ClientFrame mirror) {
  auto const desktop = desktop_window();
  if (!desktop.is_valid()) {
    return;
  }
  if (mirror.x != latest_native_frame_.x ||
      mirror.y != latest_native_frame_.y ||
      mirror.client_width != latest_native_frame_.client_width ||
      mirror.client_height != latest_native_frame_.client_height) {
    return;
  }
  if (desktop->Generation() <= ui_generation_at_native_submit_) {
    return;
  }
  if (native_frame_ack_serial_ >= native_frame_submit_serial_) {
    return;
  }
  if (IsMirrorStaleNativeSubmission(mirror)) {
    return;
  }
  if (model_generation_at_native_submit_ != 0 &&
      desktop->Generation() < model_generation_at_native_submit_) {
    return;
  }
  native_frame_ack_serial_ = native_frame_submit_serial_;
  ui_generation_at_native_submit_ = desktop->Generation();
}

bool WinDesktopWindowPresenter::FramePublicationIsCurrent() const {
  return native_frame_ack_serial_ >= native_frame_submit_serial_;
}

bool WinDesktopWindowPresenter::ShouldApplyMirrorFrameToNative(
    ClientFrame mirror, ClientFrame native) const {
  if (interactive_move_active_) {
    return false;
  }
  if (mirror.x == native.x && mirror.y == native.y &&
      mirror.client_width == native.client_width &&
      mirror.client_height == native.client_height) {
    return false;
  }
  if (native_frame_submit_serial_ > native_frame_ack_serial_) {
    bool const mirror_is_latest =
        mirror.x == latest_native_frame_.x &&
        mirror.y == latest_native_frame_.y &&
        mirror.client_width == latest_native_frame_.client_width &&
        mirror.client_height == latest_native_frame_.client_height;
    if (mirror_is_latest) {
      return false;
    }
    if (IsMirrorStaleNativeSubmission(mirror)) {
      return false;
    }
    auto const desktop = desktop_window();
    if (!desktop.is_valid() ||
        desktop->Generation() <= ui_generation_at_native_submit_) {
      return false;
    }
  }
  return true;
}

void WinDesktopWindowPresenter::SyncNativeFrameFromMirror() {
  auto const desktop = desktop_window();
  if (hwnd_ == nullptr || !desktop) {
    return;
  }
  ClientFrame const mirror = ReadMirrorClientFrame();
  ClientFrame const native = ReadNativeClientFrame();
  TryAcknowledgeMirrorFrame(mirror);
  if (!ShouldApplyMirrorFrameToNative(mirror, native)) {
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
  RecordNativeFrameSubmission(native);
  ae::ObjId const window_id = desktop->obj_id;
  SubmitFrameFromNative(native.x, native.y, native.client_width,
                        native.client_height);
  if (model_proxy != nullptr) {
    WinDesktopWindowPresenter* const self = this;
    model_proxy->EnqueueModelWork(
        [window_id, self](ae::Domain& domain) {
          auto object = domain.Find(window_id);
          if (!object) {
            return;
          }
          auto* const model_window =
              object.as<ui::UiDesktopWindow>();
          if (model_window == nullptr) {
            return;
          }
          self->model_generation_at_native_submit_ =
              model_window->Generation();
        });
  }
}

bool WinDesktopWindowPresenter::RouteChildCommand(std::uint32_t command_id,
                                                  std::uint16_t notification_code,
                                                  HWND child) {
  auto* child_presenter = PresenterFromChildHwnd(child);
  if (child_presenter == nullptr) {
    return false;
  }
  if (notification_code == kEnChange) {
    auto* edit = static_cast<WinEditBoxPresenter*>(child_presenter);
    if (!edit->OnCommand(command_id, notification_code)) {
      return false;
    }
    edit->ReadFromNativeAndSubmit();
    return true;
  }
  return child_presenter->OnCommand(command_id, notification_code);
}

void WinDesktopWindowPresenter::OnModelChanged() {
  if (hwnd_ == nullptr) {
    return;
  }
  SetWindowTextW(hwnd_, ResolveTitleText().c_str());
  SyncNativeFrameFromMirror();
}

void WinDesktopWindowPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

WinDesktopWindowPresenter* WinEditBoxPresenter::Desktop() const {
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinEditBoxPresenter::OnLoad() {
  auto* desktop = Desktop();
  if (desktop == nullptr || desktop->hwnd() == nullptr || !edit_box ||
      control_id == 0) {
    return;
  }
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, layout_x, layout_y, layout_width,
      layout_height, desktop->hwnd(),
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
      GetModuleHandleW(nullptr), nullptr);
  if (CreateFailed(hwnd_)) {
    return;
  }
  if (edit_box->read_only()) {
    SendMessageW(hwnd_, EM_SETREADONLY, TRUE, 0);
  }
  SetUserData(hwnd_, this);
  SyncFromMirror();
}

void WinEditBoxPresenter::SyncFromMirror() {
  if (hwnd_ == nullptr || !edit_box) {
    return;
  }
  int const len = GetWindowTextLengthW(hwnd_);
  std::wstring wide_native(static_cast<std::size_t>(len), L'\0');
  if (len > 0) {
    GetWindowTextW(hwnd_, wide_native.data(), len + 1);
  }
  wide_native.resize(static_cast<std::size_t>(len));
  auto const current_native = WideToUtf8(wide_native);
  bool const needs_update =
      !mirror_text_applied_ || current_native != edit_box->text() ||
      edit_box->Generation() != last_applied_generation_;
  if (!needs_update) {
    return;
  }
  auto const wide = Utf8ToWide(edit_box->text());
  applying_mirror_text_ = true;
  SetWindowTextW(hwnd_, wide.c_str());
  int const caret =
      WideCaretFromUtf8Offset(wide, edit_box->caret_utf8_offset());
  SendMessageW(hwnd_, EM_SETSEL, caret, caret);
  applying_mirror_text_ = false;
  mirror_text_applied_ = true;
  last_applied_generation_ = edit_box->Generation();
}

void WinEditBoxPresenter::ReadFromNativeAndSubmit() {
  if (hwnd_ == nullptr || applying_mirror_text_) {
    return;
  }
  int const len = GetWindowTextLengthW(hwnd_);
  std::wstring wide(static_cast<std::size_t>(len), L'\0');
  if (len > 0) {
    GetWindowTextW(hwnd_, wide.data(), len + 1);
  }
  DWORD const sel = static_cast<DWORD>(SendMessageW(hwnd_, EM_GETSEL, 0, 0));
  int const wide_caret = HIWORD(sel);
  auto const utf8 = WideToUtf8(wide);
  auto const caret = Utf8OffsetFromWideCaret(wide, wide_caret);
  SubmitTextFromUi(utf8, caret);
}

bool WinEditBoxPresenter::OnCommand(std::uint32_t command_id,
                                    std::uint16_t notification_code) {
  return UiEditBoxPresenter::OnCommand(command_id, notification_code);
}

void WinEditBoxPresenter::OnModelChanged() { SyncFromMirror(); }

void WinEditBoxPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

WinDesktopWindowPresenter* WinPushButtonPresenter::Desktop() const {
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinPushButtonPresenter::OnLoad() {
  auto* desktop = Desktop();
  if (desktop == nullptr || desktop->hwnd() == nullptr || !button ||
      control_id == 0) {
    return;
  }
  std::wstring caption;
  if (button->label && window_presenter && window_presenter->locale) {
    caption = Utf8ToWide(
        button->label->Resolve(window_presenter->locale->language_index()));
  }
  hwnd_ = CreateWindowExW(
      0, L"BUTTON", caption.c_str(),
      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, layout_x, layout_y, layout_width,
      layout_height, desktop->hwnd(),
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
      GetModuleHandleW(nullptr), nullptr);
  if (CreateFailed(hwnd_)) {
    return;
  }
  SetUserData(hwnd_, this);
  SyncFromMirror();
}

void WinPushButtonPresenter::SyncFromMirror() {
  if (hwnd_ == nullptr || !button) {
    return;
  }
  if (button->label && window_presenter && window_presenter->locale) {
    SetWindowTextW(
        hwnd_, Utf8ToWide(button->label->Resolve(
                          window_presenter->locale->language_index()))
                   .c_str());
  }
  EnableWindow(hwnd_, button->enabled() ? TRUE : FALSE);
}

void WinPushButtonPresenter::OnModelChanged() { SyncFromMirror(); }

void WinPushButtonPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
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
  if (control_id == 0 || command_id != control_id || hwnd_ == nullptr) {
    return UiListContainerPresenter::OnCommand(command_id, notification_code);
  }
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
    return UiListContainerPresenter::OnCommand(command_id, notification_code);
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
    if (notify_list_selection_change && window_presenter &&
        window_presenter->locale) {
      int const language_index = window_presenter->locale->language_index();
      LRESULT const cur_sel = SendMessageW(hwnd_, LB_GETCURSEL, 0, 0);
      if (language_index >= 0 &&
          language_index < static_cast<int>(desired.size()) &&
          cur_sel != language_index) {
        SendMessageW(hwnd_, LB_SETCURSEL,
                     static_cast<WPARAM>(language_index), 0);
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

  if (notify_list_selection_change && window_presenter &&
      window_presenter->locale) {
    int const language_index = window_presenter->locale->language_index();
    if (language_index >= 0 &&
        language_index < static_cast<int>(desired.size())) {
      SendMessageW(hwnd_, LB_SETCURSEL,
                   static_cast<WPARAM>(language_index), 0);
    }
  }
}

void WinListContainerPresenter::OnModelChanged() { SyncFromMirror(); }

void WinListContainerPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

WinDesktopWindowPresenter* WinBitmapImagePresenter::Desktop() const {
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinBitmapImagePresenter::OnLoad() {
  auto* desktop = Desktop();
  if (desktop == nullptr || desktop->hwnd() == nullptr) {
    return;
  }
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  EnsureBitmapViewportClass(instance);
  hwnd_ = CreateWindowExW(
      0, kBitmapViewportClass, L"", WS_CHILD | WS_VISIBLE, layout_x, layout_y,
      layout_width, layout_height, desktop->hwnd(), nullptr, instance, nullptr);
  if (CreateFailed(hwnd_)) {
    return;
  }
  SetUserData(hwnd_, this);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void WinBitmapImagePresenter::OnModelChanged() {
  if (hwnd_ != nullptr) {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void WinBitmapImagePresenter::PaintToHdc(HDC hdc) const {
  if (!image || image->width == 0 || image->height == 0 ||
      image->bgra_bytes.empty()) {
    return;
  }
  std::size_t const expected =
      static_cast<std::size_t>(image->width) *
      static_cast<std::size_t>(image->height) * 4U;
  if (image->bgra_bytes.size() < expected) {
    return;
  }
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = static_cast<LONG>(image->width);
  bmi.bmiHeader.biHeight = -static_cast<LONG>(image->height);
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  RECT client{};
  GetClientRect(hwnd_, &client);
  StretchDIBits(hdc, client.left, client.top,
                client.right - client.left, client.bottom - client.top, 0, 0,
                static_cast<int>(image->width), static_cast<int>(image->height),
                image->bgra_bytes.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void WinBitmapImagePresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

WinDesktopWindowPresenter* WinLabelPresenter::Desktop() const {
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

void WinLabelPresenter::OnLoad() {
  auto* desktop = Desktop();
  if (desktop == nullptr || desktop->hwnd() == nullptr || !label) {
    return;
  }
  std::wstring caption;
  if (label->text && window_presenter && window_presenter->locale) {
    caption = Utf8ToWide(
        label->text->Resolve(window_presenter->locale->language_index()));
  }
  hwnd_ = CreateWindowExW(0, L"STATIC", caption.c_str(), WS_CHILD | WS_VISIBLE,
                          layout_x, layout_y, layout_width, layout_height,
                          desktop->hwnd(), nullptr, GetModuleHandleW(nullptr),
                          nullptr);
  if (CreateFailed(hwnd_)) {
    return;
  }
}

void WinLabelPresenter::SyncFromMirror() {
  if (hwnd_ == nullptr || !label || !window_presenter ||
      !window_presenter->locale) {
    return;
  }
  SetWindowTextW(
      hwnd_, Utf8ToWide(label->text->Resolve(
                        window_presenter->locale->language_index()))
                 .c_str());
}

void WinLabelPresenter::OnModelChanged() { SyncFromMirror(); }

void WinLabelPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

}  // namespace apptraverse::ui::windows
