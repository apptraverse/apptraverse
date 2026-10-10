#include "apptraverse/platform/windows/win_ui_presenters.h"

#include "apptraverse/platform/windows/win_presentation_host.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {
namespace {

constexpr wchar_t kDesktopWindowClass[] = L"AppTraverseUiDesktopWindow";
constexpr std::uint16_t kEnChange = 0x0300;

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
  if (msg == WM_WINDOWPOSCHANGED && !presenter->applying_native_frame_) {
    presenter->HandleNativeFrameChanged();
  }
  if (msg == WM_CLOSE) {
    auto* host =
        static_cast<WinPresentationHost*>(presenter->presentation_host);
    if (host != nullptr) {
      if (host->on_desktop_window_user_close != nullptr) {
        host->on_desktop_window_user_close(host->opaque);
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

void WinDesktopWindowPresenter::SyncNativeFrameFromMirror() {
  auto const desktop = desktop_window();
  if (hwnd_ == nullptr || !desktop) {
    return;
  }
  applying_native_frame_ = true;
  int outer_w = 0;
  int outer_h = 0;
  ClientSizeToOuter(desktop->client_width(), desktop->client_height(), outer_w,
                    outer_h);
  SetWindowPos(hwnd_, nullptr, desktop->x(), desktop->y(), outer_w, outer_h,
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
  int x = 0;
  int y = 0;
  int cw = 0;
  int ch = 0;
  ReadModelFrameFromHwnd(hwnd_, x, y, cw, ch);
  SubmitFrameFromNative(x, y, cw, ch);
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
  auto const wide = Utf8ToWide(edit_box->text());
  auto const current = WideToUtf8(std::wstring_view{
      wide.c_str(), wide.size()});
  if (current != edit_box->text() ||
      edit_box->Generation() != last_applied_generation_) {
    applying_mirror_text_ = true;
    SetWindowTextW(hwnd_, wide.c_str());
    int const caret =
        WideCaretFromUtf8Offset(wide, edit_box->caret_utf8_offset());
    SendMessageW(hwnd_, EM_SETSEL, caret, caret);
    applying_mirror_text_ = false;
    last_applied_generation_ = edit_box->Generation();
  }
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
