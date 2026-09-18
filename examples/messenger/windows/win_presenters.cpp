#include "win_presenters.h"

#include <cstring>
#include <string>
#include <vector>

#include <commctrl.h>

#include "messenger_win32_messages.h"
#include "win32_fatal.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(Win32SurfacePresenter);

void SetHwndUserData(HWND hwnd, void* value) {
  SetLastError(0);
  LONG_PTR const previous =
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(value));
  if (previous == 0 && GetLastError() != 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowLongPtrW GWLP_USERDATA", err);
  }
}

void PostApplicationStop(void* presentation_host) {
  HWND const notify = reinterpret_cast<HWND>(presentation_host);
  if (PostMessageW(notify, WM_APPTRAVERSE_STOP, 0, 0) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("PostMessageW WM_APPTRAVERSE_STOP", err);
  }
}

std::wstring Utf8ToWide(std::string const& utf8) {
  if (utf8.empty()) {
    return {};
  }
  int const n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                    static_cast<int>(utf8.size()), nullptr, 0);
  if (n <= 0) {
    DWORD const err = GetLastError();
    FatalWin32("MultiByteToWideChar", err);
  }
  std::wstring out(static_cast<size_t>(n), L'\0');
  if (MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                          static_cast<int>(utf8.size()), out.data(), n) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("MultiByteToWideChar write", err);
  }
  return out;
}

std::string WideToUtf8(std::wstring const& wide) {
  if (wide.empty()) {
    return {};
  }
  int const n = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                    static_cast<int>(wide.size()), nullptr, 0,
                                    nullptr, nullptr);
  if (n <= 0) {
    DWORD const err = GetLastError();
    FatalWin32("WideCharToMultiByte", err);
  }
  std::string out(static_cast<size_t>(n), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                          static_cast<int>(wide.size()), out.data(), n, nullptr,
                          nullptr) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("WideCharToMultiByte write", err);
  }
  return out;
}

std::string ReadEditUtf8(HWND edit) {
  int const len = GetWindowTextLengthW(edit);
  if (len < 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowTextLengthW", err);
  }
  if (len == 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(len) + 1, L'\0');
  int const written = GetWindowTextW(edit, wide.data(), len + 1);
  if (written <= 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowTextW", err);
  }
  wide.resize(static_cast<size_t>(written));
  return WideToUtf8(wide);
}

void SetEditUtf8(HWND edit, std::string const& utf8) {
  std::wstring const wide = Utf8ToWide(utf8);
  if (SetWindowTextW(edit, wide.c_str()) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowTextW", err);
  }
}

bool DispatchChildCommand(WPARAM wparam, LPARAM lparam) {
  HWND const child = reinterpret_cast<HWND>(lparam);
  if (child == nullptr) {
    return false;
  }
  auto* owner =
      reinterpret_cast<Presenter*>(GetWindowLongPtrW(child, GWLP_USERDATA));
  if (owner == nullptr) {
    return false;
  }
  return owner->OnCommand(static_cast<std::uint32_t>(LOWORD(wparam)),
                          static_cast<std::uint16_t>(HIWORD(wparam)));
}

LRESULT CALLBACK PeerUidSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                     LPARAM lparam, UINT_PTR, DWORD_PTR ref) {
  auto* presenter = reinterpret_cast<Win32SurfacePresenter*>(ref);
  if (msg == WM_KEYDOWN && wparam == VK_RETURN) {
    presenter->ConfirmPeerUid();
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK DraftSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                   LPARAM lparam, UINT_PTR, DWORD_PTR ref) {
  auto* presenter = reinterpret_cast<Win32SurfacePresenter*>(ref);
  if (msg == WM_KEYDOWN && wparam == VK_RETURN) {
    presenter->ConfirmSendDraft();
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

}  // namespace

void EnsureWin32SurfacePresenterRegistration() {
  EnsureDesktopSurfacePresenterRegistration();
  (void)&g_apptraverse_registrar_Win32SurfacePresenter;
}

void RegisterMessengerWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = &Win32SurfacePresenter::WndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kMessengerWindowClass;
  if (RegisterClassW(&wc) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("RegisterClassW Messenger", err);
  }
}

void UnregisterMessengerWin32Classes() {
  HINSTANCE const instance = GetModuleHandleW(nullptr);
  if (UnregisterClassW(kMessengerWindowClass, instance) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("UnregisterClassW Messenger", err);
  }
}

LRESULT CALLBACK Win32SurfacePresenter::WndProc(HWND hwnd, UINT msg,
                                                WPARAM wparam, LPARAM lparam) {
  if (msg == WM_NCCREATE) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  auto* presenter = reinterpret_cast<Win32SurfacePresenter*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (presenter == nullptr) {
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_COMMAND && DispatchChildCommand(wparam, lparam)) {
    return 0;
  }
  if (msg == WM_SIZE) {
    RECT client{};
    if (GetClientRect(hwnd, &client) == 0) {
      DWORD const err = GetLastError();
      FatalWin32("GetClientRect Messenger WM_SIZE", err);
    }
    presenter->PresentationSizeChanged(client.right - client.left,
                                       client.bottom - client.top);
    presenter->LayoutControls();
    return 0;
  }
  if (msg == WM_CLOSE) {
    PostApplicationStop(presenter->presentation_host);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void Win32SurfacePresenter::OnLoad() {
  hwnd = CreateWindowExW(
      0, kMessengerWindowClass, L"Мессенджер", WS_OVERLAPPEDWINDOW,
      surface->desktop_x, surface->desktop_y, surface->desktop_width,
      surface->desktop_height, nullptr, nullptr, GetModuleHandleW(nullptr),
      this);
  if (hwnd == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW Messenger", err);
  }
  SetHwndUserData(hwnd, this);

  HINSTANCE const instance = GetModuleHandleW(nullptr);

  own_uid_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY, 0, 0, 0, 0, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOwnUidEditId)), instance,
      nullptr);
  if (own_uid_edit == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW own uid", err);
  }

  copy_button = CreateWindowExW(
      0, L"BUTTON", L"Копировать", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
      0, 0, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCopyButtonId)), instance,
      nullptr);
  if (copy_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW copy", err);
  }
  SetHwndUserData(copy_button, static_cast<Presenter*>(this));

  peer_uid_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0,
      0, 0, 0, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPeerUidEditId)), instance,
      nullptr);
  if (peer_uid_edit == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW peer uid", err);
  }
  SetHwndUserData(peer_uid_edit, static_cast<Presenter*>(this));
  SendMessageW(peer_uid_edit, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"UID собеседника"));
  if (SetWindowSubclass(peer_uid_edit, &PeerUidSubclassProc, 1,
                        reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowSubclass peer uid", err);
  }

  add_button = CreateWindowExW(
      0, L"BUTTON", L"Добавить", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0,
      0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAddButtonId)),
      instance, nullptr);
  if (add_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW add", err);
  }
  SetHwndUserData(add_button, static_cast<Presenter*>(this));

  transcript_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY |
          ES_AUTOVSCROLL,
      0, 0, 0, 0, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTranscriptEditId)),
      instance, nullptr);
  if (transcript_edit == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW transcript", err);
  }

  draft_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDraftEditId)), instance,
      nullptr);
  if (draft_edit == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW draft", err);
  }
  SetHwndUserData(draft_edit, static_cast<Presenter*>(this));
  SendMessageW(draft_edit, EM_SETCUEBANNER, TRUE,
               reinterpret_cast<LPARAM>(L"Сообщение"));
  if (SetWindowSubclass(draft_edit, &DraftSubclassProc, 2,
                        reinterpret_cast<DWORD_PTR>(this)) == FALSE) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowSubclass draft", err);
  }

  send_button = CreateWindowExW(
      0, L"BUTTON", L"Отправить", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0,
      0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSendButtonId)),
      instance, nullptr);
  if (send_button == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("CreateWindowExW send", err);
  }
  SetHwndUserData(send_button, static_cast<Presenter*>(this));

  LayoutControls();
  SyncControlsFromModel();
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
}

void Win32SurfacePresenter::LayoutControls() {
  RECT client{};
  if (GetClientRect(hwnd, &client) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetClientRect LayoutControls", err);
  }
  int const width = client.right - client.left;
  int const height = client.bottom - client.top;
  constexpr int kMargin = 12;
  constexpr int kGap = 8;
  constexpr int kRowH = 28;
  constexpr int kCopyW = 110;
  constexpr int kAddW = 100;
  constexpr int kSendW = 110;
  constexpr int kDraftH = 28;

  int const inner_w = width > 2 * kMargin ? width - 2 * kMargin : 1;
  auto const edit_w_for = [inner_w, kGap](int button_w) {
    return inner_w > button_w + kGap ? inner_w - button_w - kGap : 1;
  };
  int const own_w = edit_w_for(kCopyW);
  int const peer_w = edit_w_for(kAddW);
  int const draft_w = edit_w_for(kSendW);
  int y = kMargin;

  if (SetWindowPos(own_uid_edit, nullptr, kMargin, y, own_w, kRowH,
                   SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos own uid", err);
  }
  if (SetWindowPos(copy_button, nullptr, kMargin + own_w + kGap, y, kCopyW,
                   kRowH, SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos copy", err);
  }
  y += kRowH + kGap;
  if (SetWindowPos(peer_uid_edit, nullptr, kMargin, y, peer_w, kRowH,
                   SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos peer uid", err);
  }
  if (SetWindowPos(add_button, nullptr, kMargin + peer_w + kGap, y, kAddW,
                   kRowH, SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos add", err);
  }
  y += kRowH + kGap;
  int const draft_y = height - kMargin - kDraftH;
  int const transcript_h = draft_y - y - kGap;
  if (SetWindowPos(transcript_edit, nullptr, kMargin, y, inner_w,
                   transcript_h > 40 ? transcript_h : 40,
                   SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos transcript", err);
  }
  if (SetWindowPos(draft_edit, nullptr, kMargin, draft_y, draft_w, kDraftH,
                   SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos draft", err);
  }
  if (SetWindowPos(send_button, nullptr, kMargin + draft_w + kGap, draft_y,
                   kSendW, kDraftH, SWP_NOZORDER | SWP_NOACTIVATE) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("SetWindowPos send", err);
  }
}

void Win32SurfacePresenter::SyncControlsFromModel() {
  Dialog& dialog = *surface->dialog;
  if (dialog.own_uid.empty()) {
    if (SetWindowTextW(own_uid_edit, L"ожидание UID...") == 0) {
      DWORD const err = GetLastError();
      FatalWin32("SetWindowTextW waiting uid", err);
    }
    EnableWindow(copy_button, FALSE);
  } else {
    SetEditUtf8(own_uid_edit, dialog.own_uid);
    EnableWindow(copy_button, TRUE);
  }

  // Avoid fighting the user while they type a peer UID that is not yet
  // confirmed. Only sync peer field when it matches the committed value or is
  // empty after a switch that restored another conversation.
  std::string const peer_edit = ReadEditUtf8(peer_uid_edit);
  if (peer_edit != dialog.peer_uid) {
    // If focus is not in the peer field, publish the model value.
    if (GetFocus() != peer_uid_edit) {
      SetEditUtf8(peer_uid_edit, dialog.peer_uid);
    }
  }

  std::string transcript;
  if (dialog.conversation.is_valid()) {
    for (auto const& message : dialog.conversation->messages) {
      if (!transcript.empty()) {
        transcript.push_back('\r');
        transcript.push_back('\n');
      }
      bool const outgoing = message.id.origin_uid == dialog.own_uid;
      transcript += outgoing ? "→ " : "← ";
      transcript += message.text;
    }
  } else {
    for (auto const& line : dialog.messages) {
      if (!transcript.empty()) {
        transcript.push_back('\r');
        transcript.push_back('\n');
      }
      transcript += line.outgoing ? "→ " : "← ";
      transcript += line.text;
    }
  }
  SetEditUtf8(transcript_edit, transcript);
  SendMessageW(transcript_edit, EM_SETSEL, static_cast<WPARAM>(-1),
               static_cast<LPARAM>(-1));
  SendMessageW(transcript_edit, EM_SCROLLCARET, 0, 0);

  ApplyDraftFromModel(dialog.draft);
  EnableWindow(send_button, dialog.conversation.is_valid() ? TRUE : FALSE);
}

void Win32SurfacePresenter::OnModelChanged() { SyncControlsFromModel(); }

void Win32SurfacePresenter::ApplyDraftFromModel(std::string const& model_draft) {
  std::string const edit_text = ReadEditUtf8(draft_edit);
  if (model_draft == synced_draft_ && model_draft == edit_text) {
    return;
  }
  // Confirmed model edit: field still shows the last synced value (e.g. sent
  // draft cleared while focused), or focus is elsewhere.
  bool const apply =
      GetFocus() != draft_edit || edit_text == synced_draft_;
  if (!apply) {
    // Newer local typing than the model revision — keep the edit text.
    synced_draft_ = model_draft;
    return;
  }
  applying_draft_ = true;
  SetEditUtf8(draft_edit, model_draft);
  applying_draft_ = false;
  synced_draft_ = model_draft;
}

bool Win32SurfacePresenter::OnCommand(std::uint32_t command_id,
                                      std::uint16_t notification_code) {
  if (command_id == static_cast<std::uint32_t>(kCopyButtonId) &&
      notification_code == BN_CLICKED) {
    CopyOwnUid();
    return true;
  }
  if (command_id == static_cast<std::uint32_t>(kAddButtonId) &&
      notification_code == BN_CLICKED) {
    ConfirmPeerUid();
    return true;
  }
  if (command_id == static_cast<std::uint32_t>(kSendButtonId) &&
      notification_code == BN_CLICKED) {
    ConfirmSendDraft();
    return true;
  }
  if (command_id == static_cast<std::uint32_t>(kDraftEditId) &&
      notification_code == EN_CHANGE) {
    if (applying_draft_) {
      return true;
    }
    synced_draft_ = ReadEditUtf8(draft_edit);
    DraftEdited(synced_draft_);
    return true;
  }
  return false;
}

void Win32SurfacePresenter::ConfirmPeerUid() {
  PeerUidEntered(ReadEditUtf8(peer_uid_edit));
}

void Win32SurfacePresenter::ConfirmSendDraft() {
  if (!surface->dialog->conversation.is_valid()) {
    return;
  }
  std::string text = ReadEditUtf8(draft_edit);
  if (text.empty()) {
    return;
  }
  SendDraft(std::move(text));
}

void Win32SurfacePresenter::CopyOwnUid() {
  std::string const& uid = surface->dialog->own_uid;
  if (uid.empty()) {
    return;
  }
  std::wstring const wide = Utf8ToWide(uid);
  size_t const bytes = (wide.size() + 1) * sizeof(wchar_t);
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (mem == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("GlobalAlloc clipboard", err);
  }
  void* locked = GlobalLock(mem);
  if (locked == nullptr) {
    DWORD const err = GetLastError();
    FatalWin32("GlobalLock clipboard", err);
  }
  memcpy(locked, wide.c_str(), bytes);
  GlobalUnlock(mem);
  if (OpenClipboard(hwnd) == 0) {
    DWORD const err = GetLastError();
    GlobalFree(mem);
    FatalWin32("OpenClipboard", err);
  }
  EmptyClipboard();
  if (SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
    DWORD const err = GetLastError();
    CloseClipboard();
    GlobalFree(mem);
    FatalWin32("SetClipboardData", err);
  }
  CloseClipboard();
}

void Win32SurfacePresenter::OnUnload() {
  if (peer_uid_edit != nullptr) {
    RemoveWindowSubclass(peer_uid_edit, &PeerUidSubclassProc, 1);
  }
  if (draft_edit != nullptr) {
    RemoveWindowSubclass(draft_edit, &DraftSubclassProc, 2);
  }
  SetHwndUserData(copy_button, nullptr);
  SetHwndUserData(peer_uid_edit, nullptr);
  SetHwndUserData(add_button, nullptr);
  SetHwndUserData(draft_edit, nullptr);
  SetHwndUserData(send_button, nullptr);
  SetHwndUserData(hwnd, nullptr);
  if (DestroyWindow(hwnd) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("DestroyWindow Messenger", err);
  }
  hwnd = nullptr;
  own_uid_edit = nullptr;
  copy_button = nullptr;
  peer_uid_edit = nullptr;
  add_button = nullptr;
  transcript_edit = nullptr;
  draft_edit = nullptr;
  send_button = nullptr;
}

void Win32SurfacePresenter::QueueCurrentBounds() {
  RECT outer{};
  if (GetWindowRect(hwnd, &outer) == 0) {
    DWORD const err = GetLastError();
    FatalWin32("GetWindowRect Messenger", err);
  }
  UpdateModelBounds(outer.left, outer.top, outer.right - outer.left,
                    outer.bottom - outer.top);
}

}  // namespace apptraverse
