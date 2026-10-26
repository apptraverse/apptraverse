#include "apptraverse/platform/windows/win_ui_presenters.h"

#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse::ui::windows {
namespace {

constexpr wchar_t kBitmapViewportClass[] = L"AppTraverseUiBitmapViewport";

bool g_bitmap_viewport_class_registered{false};

void SetUserData(HWND hwnd, void* value) {
  SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
}

bool CreateFailed(HWND hwnd) { return hwnd == nullptr; }

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

}  // namespace

WinDesktopWindowPresenter* WinEditBoxPresenter::Desktop() const {
  if (!window_presenter) {
    return nullptr;
  }
  return static_cast<WinDesktopWindowPresenter*>(window_presenter.operator->());
}

bool WinEditBoxPresenter::OnCommand(std::uint32_t command_id,
                                     std::uint16_t notification_code) {
  // The window router already validated HWND parent and control id.
  (void)command_id;
  if (notification_code != EN_CHANGE) {
    return false;
  }
  ReadFromNativeAndSubmit();
  return true;
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

void WinEditBoxPresenter::OnModelChanged() { SyncFromMirror(); }

void WinEditBoxPresenter::OnUnload() {
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
