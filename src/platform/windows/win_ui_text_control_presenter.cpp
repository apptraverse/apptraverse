// Define the Windows SDK accessibility GUIDs in this translation unit.
#define INITGUID
#include "apptraverse/platform/windows/win_ui_text_control_presenter.h"
#include <commctrl.h>
#include <objbase.h>
#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
namespace apptraverse::ui::windows {
namespace {
std::wstring ReadText(HWND hwnd) {
  SetLastError(ERROR_SUCCESS);
  int length = GetWindowTextLengthW(hwnd);
  if (length == 0 && GetLastError() != ERROR_SUCCESS)
    FatalWin32("GetWindowTextLengthW text control", GetLastError());
  std::wstring value(length + 1, L'\0');
  int copied = GetWindowTextW(hwnd, value.data(), length + 1);
  if (copied == 0 && length != 0)
    FatalWin32("GetWindowTextW text control", GetLastError());
  value.resize(copied);
  return value;
}
}  // namespace
void WinTextControlPresenter::OnLoad() {
  auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  com_initialized_ = SUCCEEDED(apartment);
  if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE)
    FatalWin32("CoInitializeEx text accessibility",
               static_cast<DWORD>(apartment));
  auto created =
      CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&accessibility_));
  if (FAILED(created))
    FatalWin32("CoCreateInstance text accessibility",
               static_cast<DWORD>(created));
  auto parent = static_cast<HWND>(window_presenter->NativeChildParentHandle());
  auto kind = control->kind;
  wchar_t const* type = kind == UiTextControlKind::kLabel ? L"STATIC" : L"EDIT";
  DWORD style = WS_CHILD | (control->visible ? WS_VISIBLE : 0);
  if (kind == UiTextControlKind::kEdit)
    style |= WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP;
  if (kind == UiTextControlKind::kMultiline)
    style |= WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL;
  hwnd_ =
      CreateWindowExW(0, type, L"", style, 0, 0, 0, 0, parent,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(control_id)),
                      GetModuleHandleW(nullptr), nullptr);
  if (!hwnd_) FatalWin32("CreateWindowExW text control", GetLastError());
  SetLastError(ERROR_SUCCESS);
  auto previous =
      SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  if (previous == 0 && GetLastError() != ERROR_SUCCESS)
    FatalWin32("SetWindowLongPtrW text control", GetLastError());
  if (!SetWindowSubclass(hwnd_, Subclass, 1, reinterpret_cast<DWORD_PTR>(this)))
    FatalWin32("SetWindowSubclass text control", GetLastError());
  if (kind == UiTextControlKind::kEdit)
    SendMessageW(hwnd_, EM_SETLIMITTEXT, 8192, 0);
  if (kind == UiTextControlKind::kMultiline)
    SendMessageW(hwnd_, EM_SETLIMITTEXT, 0, 0);
  WinDesktopWindowPresenter::FromNativeWindow(parent)->RegisterChildControl(
      hwnd_, control_id);
  OnModelChanged();
}
void WinTextControlPresenter::OnModelChanged() {
  applying_ = true;
  if (control->kind != UiTextControlKind::kLabel) {
    if (control->accessible_label.empty())
      FatalWin32("Missing semantic text control name", ERROR_INVALID_DATA);
    if (applied_accessible_label_ != control->accessible_label) {
      auto label = Utf8ToWide(control->accessible_label);
      auto result = accessibility_->SetHwndPropStr(
          hwnd_, OBJID_CLIENT, CHILDID_SELF, PROPID_ACC_NAME, label.c_str());
      if (FAILED(result))
        FatalWin32("SetHwndPropStr text name", static_cast<DWORD>(result));
      applied_accessible_label_ = control->accessible_label;
    }
  }
  EnableWindow(hwnd_, control->enabled);
  ShowWindow(hwnd_, control->visible ? SW_SHOW : SW_HIDE);
  if (control->revision >= pending_edit) {
    auto value = control->text;
    if (control->kind == UiTextControlKind::kMultiline) {
      value.clear();
      for (auto const& row : control->rows) {
        if (!value.empty()) value += "\r\n";
        value += row;
      }
    }
    auto text = Utf8ToWide(value);
    if (ReadText(hwnd_) != text) {
      if (!SetWindowTextW(hwnd_, text.c_str()))
        FatalWin32("SetWindowTextW text control", GetLastError());
      if (control->kind == UiTextControlKind::kMultiline) {
        SendMessageW(hwnd_, EM_SETSEL, text.size(), text.size());
        SendMessageW(hwnd_, EM_SCROLLCARET, 0, 0);
      }
    }
  }
  if (control->kind == UiTextControlKind::kEdit ||
      control->kind == UiTextControlKind::kMultiline)
    SendMessageW(hwnd_, EM_SETREADONLY, control->read_only, 0);
  applying_ = false;
  WinDesktopWindowPresenter::FromNativeWindow(GetParent(hwnd_))
      ->RefreshLayout();
}
bool WinTextControlPresenter::OnCommand(std::uint32_t,
                                        std::uint16_t notification) {
  if (notification != EN_CHANGE || applying_ || control->read_only)
    return false;
  QueueEditedFromUi(WideToUtf8(ReadText(hwnd_)));
  return true;
}
LRESULT CALLBACK WinTextControlPresenter::Subclass(HWND hwnd, UINT msg,
                                                   WPARAM w, LPARAM l, UINT_PTR,
                                                   DWORD_PTR data) {
  auto* self = reinterpret_cast<WinTextControlPresenter*>(data);
  if (msg == WM_GETDLGCODE && self->control->kind == UiTextControlKind::kEdit) {
    auto const* input = reinterpret_cast<MSG const*>(l);
    if (input && input->message == WM_KEYDOWN && input->wParam == VK_RETURN)
      return DLGC_WANTMESSAGE;
  }
  if (msg == WM_KEYDOWN && w == VK_RETURN &&
      self->control->kind == UiTextControlKind::kEdit) {
    self->QueueAcceptFromUi();
    return 0;
  }
  if (msg == WM_CHAR && w == VK_RETURN &&
      self->control->kind == UiTextControlKind::kEdit)
    return 0;
  return DefSubclassProc(hwnd, msg, w, l);
}
void WinTextControlPresenter::OnUnload() {
  WinDesktopWindowPresenter::FromNativeWindow(GetParent(hwnd_))
      ->UnregisterChildControl(hwnd_);
  RemoveWindowSubclass(hwnd_, Subclass, 1);
  if (!DestroyWindow(hwnd_))
    FatalWin32("DestroyWindow text control", GetLastError());
  hwnd_ = nullptr;
  accessibility_->Release();
  accessibility_ = nullptr;
  if (com_initialized_) {
    CoUninitialize();
    com_initialized_ = false;
  }
  applied_accessible_label_.clear();
}
}  // namespace apptraverse::ui::windows
