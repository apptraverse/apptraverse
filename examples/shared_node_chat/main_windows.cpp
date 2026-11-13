#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "apptraverse/platform/windows/win32_fatal.h"
#include "apptraverse/platform/windows/win_ui_utf8.h"
#include "chat_controls.h"
#include "chat_session.h"

#ifndef APPTRAVERSE_CHAT_TITLE
#  define APPTRAVERSE_CHAT_TITLE L"SharedNode Chat"
#endif

namespace apptraverse::chat {
namespace {
using ui::windows::Utf8ToWide;
using ui::windows::WideToUtf8;
constexpr UINT kViewChanged = WM_APP + 1;

void Close(HANDLE handle) {
  if (!CloseHandle(handle)) {
    DWORD const error = GetLastError();
    FatalWin32("CloseHandle chat profile mutex", error);
  }
}

void Place(HWND control, int x, int y, int width, int height) {
  if (!MoveWindow(control, x, y, width, height, TRUE)) {
    DWORD const error = GetLastError();
    FatalWin32("MoveWindow chat control", error);
  }
}

void SetText(HWND control, std::wstring const& value) {
  if (!SetWindowTextW(control, value.c_str())) {
    DWORD const error = GetLastError();
    FatalWin32("SetWindowTextW chat control", error);
  }
}

std::wstring Text(HWND control) {
  auto const size = GetWindowTextLengthW(control);
  std::wstring value(static_cast<std::size_t>(size) + 1, L'\0');
  auto const copied = GetWindowTextW(control, value.data(), size + 1);
  value.resize(static_cast<std::size_t>(std::max(0, copied)));
  return value;
}

void SetChanged(HWND control, std::wstring const& value) {
  if (Text(control) != value) {
    SetText(control, value);
  }
}

struct Window {
  Window(std::filesystem::path profile, std::string name)
      : session{std::move(profile), std::move(name), [this] {
                  if (!PostMessageW(hwnd, kViewChanged, 0, 0)) {
                    FatalWin32("PostMessageW chat publication", GetLastError());
                  }
                }} {}

  ChatSession session;
  HWND hwnd{}, uid_label{}, uid{}, host{}, invite_label{}, invite{}, join{};
  HWND people_label{}, people{}, messages_label{}, messages{}, input{}, send{},
      status{};
  HFONT font{};
  bool stopping{false};
  std::wstring shown_invite;
  std::vector<std::wstring> shown_people;
  std::wstring shown_messages;
  std::wstring input_error;

  HWND Control(wchar_t const* type, wchar_t const* text, DWORD style,
               int id = 0) {
    auto control =
        CreateWindowExW((style & WS_BORDER) ? WS_EX_CLIENTEDGE : 0, type, text,
                        WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                        GetModuleHandleW(nullptr), nullptr);
    if (!control) {
      DWORD const error = GetLastError();
      FatalWin32("CreateWindowExW chat control", error);
    }
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return control;
  }

  void Create(HWND value) {
    hwnd = value;
    font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    uid_label = Control(L"STATIC", L"My UID", 0);
    uid = Control(L"EDIT", L"Connecting...",
                  WS_BORDER | ES_AUTOHSCROLL | ES_READONLY | WS_TABSTOP,
                  kUidControl);
    host = Control(L"BUTTON", L"Host room", BS_PUSHBUTTON | WS_TABSTOP,
                   kHostControl);
    invite_label = Control(L"STATIC", L"Room invite", 0);
    invite = Control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                     kInviteControl);
    join =
        Control(L"BUTTON", L"Join", BS_PUSHBUTTON | WS_TABSTOP, kJoinControl);
    people_label = Control(L"STATIC", L"Participants", 0);
    people =
        Control(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
                kParticipantsControl);
    messages_label = Control(L"STATIC", L"Messages", 0);
    messages = Control(
        L"EDIT", L"",
        WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        kMessagesControl);
    SendMessageW(messages, EM_SETLIMITTEXT, 0, 0);
    input = Control(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                    kInputControl);
    SendMessageW(input, EM_SETLIMITTEXT, 4096, 0);
    send = Control(L"BUTTON", L"Send", BS_DEFPUSHBUTTON | WS_TABSTOP,
                   kSendControl);
    status =
        Control(L"STATIC", L"Connecting to Aether...", SS_LEFT, kStatusControl);
    EnableWindow(host, FALSE);
    EnableWindow(join, FALSE);
    EnableWindow(input, FALSE);
    EnableWindow(send, FALSE);
    session.Start();
    Layout();
  }

  void Layout() const {
    RECT client{};
    if (!GetClientRect(hwnd, &client)) {
      DWORD const error = GetLastError();
      FatalWin32("GetClientRect chat window", error);
    }
    int const width = std::max(400, static_cast<int>(client.right));
    int const height = std::max(300, static_cast<int>(client.bottom));
    constexpr int margin = 12, gap = 8, label_width = 78, button_width = 100;
    int const right = width - margin;
    int const edit_x = margin + label_width;
    Place(uid_label, margin, 17, label_width - gap, 20);
    Place(uid, edit_x, 12, right - edit_x - gap - button_width, 26);
    Place(host, right - button_width, 12, button_width, 26);
    Place(invite_label, margin, 55, label_width - gap, 20);
    Place(invite, edit_x, 50, right - edit_x - gap - button_width, 26);
    Place(join, right - button_width, 50, button_width, 26);
    constexpr int body_top = 108;
    int const people_width = std::min(210, std::max(130, width / 4));
    int const message_x = margin + people_width + gap;
    int const message_width = right - message_x;
    int const input_y = height - 70;
    int const body_height = std::max(70, input_y - gap - body_top);
    Place(people_label, margin, 88, people_width, 20);
    Place(messages_label, message_x, 88, message_width, 20);
    Place(people, margin, body_top, people_width, body_height + 34);
    Place(messages, message_x, body_top, message_width, body_height);
    Place(input, message_x, input_y, std::max(60, message_width - gap - 76),
          28);
    Place(send, right - 76, input_y, 76, 28);
    Place(status, margin, height - 30, right - margin, 22);
  }

  void Refresh() {
    auto const view = session.View();
    SetChanged(uid, Utf8ToWide(view.local_uid.empty() ? "Connecting..."
                                                      : view.local_uid));
    SetChanged(status, input_error.empty()
                           ? Utf8ToWide(std::string{view.locally_online
                                                        ? "Online | "
                                                        : "Offline | "} +
                                        view.status)
                           : input_error);
    if (!view.invite.empty() && Utf8ToWide(view.invite) != shown_invite) {
      shown_invite = Utf8ToWide(view.invite);
      SetText(invite, shown_invite);
    }
    EnableWindow(host, view.ready && !view.in_room);
    EnableWindow(join, view.ready && !view.in_room);
    EnableWindow(input, view.ready && view.in_room);
    EnableWindow(send, view.ready && view.in_room);
    SendMessageW(invite, EM_SETREADONLY, view.in_room, 0);
    std::vector<std::wstring> rows;
    for (auto const& person : view.participants) {
      auto const presence = person.presence == Presence::Online    ? "online"
                            : person.presence == Presence::Offline ? "offline"
                                                                   : "unknown";
      rows.push_back(Utf8ToWide(person.name + " [" + presence + "]"));
    }
    if (rows != shown_people) {
      SendMessageW(people, WM_SETREDRAW, FALSE, 0);
      SendMessageW(people, LB_RESETCONTENT, 0, 0);
      for (auto const& row : rows) {
        SendMessageW(people, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(row.c_str()));
      }
      shown_people = std::move(rows);
      SendMessageW(people, WM_SETREDRAW, TRUE, 0);
      InvalidateRect(people, nullptr, TRUE);
    }
    std::string log;
    for (auto const& message : view.messages) {
      if (!log.empty()) {
        log += "\r\n";
      }
      log += message;
    }
    auto rendered = Utf8ToWide(log);
    if (rendered != shown_messages) {
      shown_messages = std::move(rendered);
      SetText(messages, shown_messages);
      SendMessageW(messages, EM_SETSEL, static_cast<WPARAM>(-1), -1);
      SendMessageW(messages, EM_SCROLLCARET, 0, 0);
    }
  }

  void Send() {
    if (!IsWindowEnabled(send)) {
      return;
    }
    auto text = Text(input);
    if (text.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
      return;
    }
    auto encoded = WideToUtf8(text);
    if (encoded.size() > 8192) {
      input_error = L"Message is too long (maximum 8192 UTF-8 bytes).";
      SetChanged(status, input_error);
      SetFocus(input);
      return;
    }
    input_error.clear();
    session.Send(std::move(encoded));
    SetText(input, L"");
    SetFocus(input);
  }

  void Close() {
    if (stopping) {
      return;
    }
    stopping = true;
    EnableWindow(hwnd, FALSE);
    session.Stop();
    if (!DestroyWindow(hwnd)) {
      DWORD const error = GetLastError();
      FatalWin32("DestroyWindow chat window", error);
    }
  }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  auto* window =
      reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto const* create = reinterpret_cast<CREATESTRUCTW const*>(lparam);
    window = static_cast<Window*>(create->lpCreateParams);
    SetLastError(ERROR_SUCCESS);
    auto const previous = SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                            reinterpret_cast<LONG_PTR>(window));
    DWORD const error = GetLastError();
    if (previous == 0 && error != ERROR_SUCCESS) {
      FatalWin32("SetWindowLongPtrW chat window", error);
    }
  }
  if (!window) {
    return DefWindowProcW(hwnd, message, wparam, lparam);
  }
  switch (message) {
    case WM_CREATE:
      window->Create(hwnd);
      return 0;
    case WM_SIZE:
      window->Layout();
      return 0;
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      info->ptMinTrackSize = {560, 400};
      return 0;
    }
    case kViewChanged:
      window->Refresh();
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kHostControl:
          window->session.CreateRoom();
          return 0;
        case kJoinControl:
          window->session.JoinRoom(WideToUtf8(Text(window->invite)));
          return 0;
        case kSendControl:
          window->Send();
          return 0;
        default:
          break;
      }
      break;
    case WM_CLOSE:
      window->Close();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

std::wstring Arg(int argc, wchar_t** argv, wchar_t const* option,
                 std::wstring fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == std::wstring{option}) {
      return argv[i + 1];
    }
  }
  return fallback;
}

std::wstring ProfileMutex(std::filesystem::path const& profile) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (wchar_t c : profile.wstring()) {
    if (c >= L'A' && c <= L'Z') {
      c += L'a' - L'A';
    }
    hash ^= static_cast<std::uint64_t>(c);
    hash *= 1099511628211ULL;
  }
  return L"Local\\AppTraverseSharedNodeChat-" + std::to_wstring(hash);
}
}  // namespace
}  // namespace apptraverse::chat

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  using namespace apptraverse::chat;  // Native executable entry point.
  int argc = 0;
  auto* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    DWORD const error = GetLastError();
    apptraverse::FatalWin32("CommandLineToArgvW chat", error);
  }
  auto const name = Arg(argc, argv, L"--name", L"User");
  auto const profile_argument =
      Arg(argc, argv, L"--profile", L"shared-chat-state");
  LocalFree(argv);
  std::error_code error;
  auto const profile =
      std::filesystem::absolute(profile_argument, error).lexically_normal();
  if (error) {
    MessageBoxW(nullptr, L"The profile path is invalid.", L"SharedNode Chat",
                MB_OK | MB_ICONERROR);
    return 1;
  }
  HANDLE const profile_lock =
      CreateMutexW(nullptr, FALSE, ProfileMutex(profile).c_str());
  DWORD const mutex_error = GetLastError();
  if (!profile_lock) {
    apptraverse::FatalWin32("CreateMutexW chat profile", mutex_error);
  }
  if (mutex_error == ERROR_ALREADY_EXISTS) {
    Close(profile_lock);
    MessageBoxW(nullptr,
                L"This profile is already open. Use a different --profile for "
                L"another participant.",
                L"SharedNode Chat", MB_OK | MB_ICONINFORMATION);
    return 1;
  }
  WNDCLASSEXW type{};
  type.cbSize = sizeof(type);
  type.hInstance = instance;
  type.lpfnWndProc = WindowProc;
  type.lpszClassName = kWindowClass;
  type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  if (!RegisterClassExW(&type)) {
    DWORD const register_error = GetLastError();
    apptraverse::FatalWin32("RegisterClassExW chat window", register_error);
  }
  Window window{profile, apptraverse::ui::windows::WideToUtf8(name)};
  auto const title = std::wstring{APPTRAVERSE_CHAT_TITLE} + L" - " + name;
  auto hwnd = CreateWindowExW(0, kWindowClass, title.c_str(),
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              840, 560, nullptr, nullptr, instance, &window);
  if (!hwnd) {
    DWORD const create_error = GetLastError();
    apptraverse::FatalWin32("CreateWindowExW chat window", create_error);
  }
  ShowWindow(hwnd, show);
  UpdateWindow(hwnd);
  MSG message{};
  int result = 0;
  for (;;) {
    result = GetMessageW(&message, nullptr, 0, 0);
    if (result == -1) {
      DWORD const message_error = GetLastError();
      apptraverse::FatalWin32("GetMessageW chat window", message_error);
    }
    if (result == 0) {
      break;
    }
    if (message.hwnd == window.input && message.message == WM_KEYDOWN &&
        message.wParam == VK_RETURN) {
      window.Send();
      continue;
    }
    if (!IsDialogMessageW(hwnd, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }
  window.session.Stop();
  Close(profile_lock);
  if (!UnregisterClassW(kWindowClass, instance)) {
    DWORD const unregister_error = GetLastError();
    apptraverse::FatalWin32("UnregisterClassW chat window", unregister_error);
  }
  return static_cast<int>(message.wParam);
}
