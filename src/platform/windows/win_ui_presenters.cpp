#include "apptraverse/platform/windows/win_ui_presenters.h"

#include <commctrl.h>

#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {
namespace {

constexpr wchar_t kDesktopWindowClass[] = L"AppTraverseUiDesktopWindow";
constexpr int kUiPushButtonCommand = 1;

std::wstring ResolveTitle(UiDesktopWindowPresenter const& presenter) {
  if (!presenter.title || !presenter.locale) {
    return L"AppTraverse";
  }
  return Utf8ToWide(
      presenter.title->Resolve(presenter.locale->language_index()));
}

void SetUserData(HWND hwnd, void* value) {
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
}

WinDesktopWindowPresenter* DesktopFromHwnd(HWND hwnd) {
  return reinterpret_cast<WinDesktopWindowPresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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
  if (msg == WM_COMMAND) {
    HWND const child = reinterpret_cast<HWND>(lparam);
    if (child != nullptr) {
      auto* child_presenter = reinterpret_cast<Presenter*>(
          GetWindowLongPtrW(child, GWLP_USERDATA));
      if (child_presenter != nullptr &&
          child_presenter->OnCommand(static_cast<std::uint32_t>(LOWORD(wparam)),
                                     static_cast<std::uint16_t>(HIWORD(wparam)))) {
        return 0;
      }
    }
  }
  if (msg == WM_WINDOWPOSCHANGED && !presenter->applying_native_frame_) {
    RECT frame{};
    if (GetWindowRect(hwnd, &frame) != 0) {
      presenter->HandleNativeFrameChanged(frame);
    }
  }
  if (msg == WM_CLOSE) {
    HWND const notify = reinterpret_cast<HWND>(presenter->presentation_host);
    if (notify != nullptr) {
      PostMessageW(notify, WM_CLOSE, 0, 0);
    }
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void WinDesktopWindowPresenter::OnLoad() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &WinDesktopWindowPresenter::WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kDesktopWindowClass;
  RegisterClassW(&wc);

  auto const title = ResolveTitle(*this);
  hwnd_ = CreateWindowExW(0, kDesktopWindowClass, title.c_str(),
                          WS_OVERLAPPEDWINDOW, window->x(), window->y(),
                          window->client_width() + 16, window->client_height() + 39,
                          nullptr, nullptr, instance, nullptr);
  SetUserData(hwnd_, this);
  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
}

void WinDesktopWindowPresenter::SyncNativeFrameFromMirror() {
  if (hwnd_ == nullptr || !window) {
    return;
  }
  applying_native_frame_ = true;
  SetWindowPos(hwnd_, nullptr, window->x(), window->y(),
               window->client_width() + 16, window->client_height() + 39,
               SWP_NOZORDER | SWP_NOACTIVATE);
  applying_native_frame_ = false;
}

void WinDesktopWindowPresenter::HandleNativeFrameChanged(RECT const& frame) {
  if (applying_native_frame_ || !window) {
    return;
  }
  SubmitFrameFromNative(static_cast<std::int32_t>(frame.left),
                        static_cast<std::int32_t>(frame.top),
                        static_cast<std::int32_t>(frame.right - frame.left),
                        static_cast<std::int32_t>(frame.bottom - frame.top));
}

void WinDesktopWindowPresenter::OnModelChanged() {
  if (hwnd_ == nullptr) {
    return;
  }
  SetWindowTextW(hwnd_, ResolveTitle(*this).c_str());
  SyncNativeFrameFromMirror();
}

void WinDesktopWindowPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

LRESULT CALLBACK WinEditBoxPresenter::EditSubclassProc(
    HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, UINT_PTR id,
    DWORD_PTR ref_data) {
  auto* presenter = reinterpret_cast<WinEditBoxPresenter*>(ref_data);
  if (presenter != nullptr && msg == WM_COMMAND && HIWORD(wparam) == EN_CHANGE) {
    if (!presenter->applying_mirror_text_) {
      presenter->ReadFromNativeAndSubmit();
    }
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void WinEditBoxPresenter::OnLoad() {
  auto* desktop =
      static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
  if (desktop->hwnd() == nullptr || !edit_box) {
    return;
  }
  hwnd_ = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
      12, 12, 360, 24, desktop->hwnd(), nullptr, GetModuleHandleW(nullptr),
      nullptr);
  if (edit_box->read_only()) {
    SendMessageW(hwnd_, EM_SETREADONLY, TRUE, 0);
  }
  SetUserData(hwnd_, this);
  SetWindowSubclass(hwnd_, &WinEditBoxPresenter::EditSubclassProc, 1,
                    reinterpret_cast<DWORD_PTR>(this));
  SyncFromMirror();
}

void WinEditBoxPresenter::SyncFromMirror() {
  if (hwnd_ == nullptr || !edit_box) {
    return;
  }
  applying_mirror_text_ = true;
  SetWindowTextW(hwnd_, Utf8ToWide(edit_box->text()).c_str());
  SendMessageW(hwnd_, EM_SETSEL, static_cast<WPARAM>(edit_box->caret_utf8_offset()),
               static_cast<LPARAM>(edit_box->caret_utf8_offset()));
  applying_mirror_text_ = false;
}

void WinEditBoxPresenter::ReadFromNativeAndSubmit() {
  if (hwnd_ == nullptr) {
    return;
  }
  int const len = GetWindowTextLengthW(hwnd_);
  std::wstring wide(static_cast<std::size_t>(len), L'\0');
  GetWindowTextW(hwnd_, wide.data(), len + 1);
  DWORD const sel = static_cast<DWORD>(SendMessageW(hwnd_, EM_GETSEL, 0, 0));
  DWORD const end = HIWORD(sel);
  auto const utf8 = WideToUtf8(wide);
  auto const caret =
      Utf8CaretFromWideSelection(utf8, wide, static_cast<int>(end));
  SubmitTextFromUi(utf8, caret);
}

void WinEditBoxPresenter::OnModelChanged() { SyncFromMirror(); }

void WinEditBoxPresenter::OnUnload() {
  if (hwnd_ != nullptr) {
    RemoveWindowSubclass(hwnd_, &WinEditBoxPresenter::EditSubclassProc, 1);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

void WinPushButtonPresenter::OnLoad() {
  auto* desktop =
      static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
  if (desktop->hwnd() == nullptr || !button) {
    return;
  }
  std::wstring caption = L"Button";
  if (button->label && window_presenter && window_presenter->locale) {
    caption = Utf8ToWide(
        button->label->Resolve(window_presenter->locale->language_index()));
  }
  HWND const hwnd = CreateWindowExW(
      0, L"BUTTON", caption.c_str(), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 12,
      44, 120, 28, desktop->hwnd(),
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kUiPushButtonCommand)),
      GetModuleHandleW(nullptr), nullptr);
  SetUserData(hwnd, this);
}

void WinPushButtonPresenter::OnModelChanged() {
  // Label text updates handled on next load for minimal slice.
}

void WinPushButtonPresenter::OnUnload() {
  // Destroyed with parent window.
}

void WinLabelPresenter::OnLoad() {
  auto* desktop =
      static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
  if (desktop->hwnd() == nullptr || !label) {
    return;
  }
  std::wstring caption = L"Label";
  if (label->text && window_presenter && window_presenter->locale) {
    caption = Utf8ToWide(
        label->text->Resolve(window_presenter->locale->language_index()));
  }
  hwnd_ = CreateWindowExW(0, L"STATIC", caption.c_str(),
                          WS_CHILD | WS_VISIBLE, 12, 80, 360, 20,
                          desktop->hwnd(), nullptr, GetModuleHandleW(nullptr),
                          nullptr);
}

void WinLabelPresenter::OnModelChanged() {
  if (hwnd_ == nullptr || !label || !window_presenter || !window_presenter->locale) {
    return;
  }
  SetWindowTextW(
      hwnd_, Utf8ToWide(label->text->Resolve(
                        window_presenter->locale->language_index()))
                 .c_str());
}

void WinLabelPresenter::OnUnload() {
  hwnd_ = nullptr;
}

}  // namespace apptraverse::ui::windows
