#include "win_add_peer_dialog.h"

#include <string>

namespace apptraverse {
namespace {

constexpr wchar_t const* kDialogClass = L"AppTraverseAddPeerDialog";
constexpr int kIdLocalUid = 101;
constexpr int kIdRemoteUid = 102;
constexpr int kIdAdd = 103;
constexpr int kIdCancel = 104;

struct DialogState {
  std::wstring local_uid;
  std::function<AddPeerUiResult(std::string const&)> on_add;
  bool accepted{false};
  bool open{true};
  HWND remote_edit{nullptr};
};

std::wstring Utf8ToWide(std::string const& utf8) {
  if (utf8.empty()) {
    return {};
  }
  int const size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                       static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      out.data(), size);
  return out;
}

std::string WideToUtf8(std::wstring const& wide) {
  if (wide.empty()) {
    return {};
  }
  int const size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                       static_cast<int>(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring TrimWide(std::wstring text) {
  while (!text.empty() &&
         (text.front() == L' ' || text.front() == L'\t' || text.front() == L'\r' ||
          text.front() == L'\n')) {
    text.erase(text.begin());
  }
  while (!text.empty() &&
         (text.back() == L' ' || text.back() == L'\t' || text.back() == L'\r' ||
          text.back() == L'\n')) {
    text.pop_back();
  }
  return text;
}

void ShowValidationError(HWND hwnd, AddPeerUiResult result) {
  wchar_t const* message = L"Invalid Aether ID";
  if (result == AddPeerUiResult::Self) {
    message = L"You cannot add your own Aether ID";
  }
  MessageBoxW(hwnd, message, L"Add participant", MB_OK | MB_ICONWARNING);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<DialogState*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg) {
    case WM_CREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
      state = static_cast<DialogState*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

      CreateWindowExW(0, L"STATIC", L"My Aether ID", WS_CHILD | WS_VISIBLE, 12,
                      12, 360, 18, hwnd, nullptr, GetModuleHandleW(nullptr),
                      nullptr);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->local_uid.c_str(),
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                          ES_READONLY,
                      12, 34, 360, 24, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdLocalUid)),
                      GetModuleHandleW(nullptr), nullptr);

      CreateWindowExW(0, L"STATIC", L"Remote Aether ID", WS_CHILD | WS_VISIBLE,
                      12, 70, 360, 18, hwnd, nullptr, GetModuleHandleW(nullptr),
                      nullptr);
      state->remote_edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 12, 92, 360, 24,
          hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRemoteUid)),
          GetModuleHandleW(nullptr), nullptr);

      CreateWindowExW(0, L"BUTTON", L"Cancel",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 196,
                      136, 80, 28, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)),
                      GetModuleHandleW(nullptr), nullptr);
      CreateWindowExW(0, L"BUTTON", L"Add",
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 292,
                      136, 80, 28, hwnd,
                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdAdd)),
                      GetModuleHandleW(nullptr), nullptr);

      if (state->remote_edit != nullptr) {
        SetFocus(state->remote_edit);
      }
      return 0;
    }
    case WM_COMMAND: {
      int const id = LOWORD(wparam);
      if (id == kIdCancel ||
          (id == IDCANCEL && HIWORD(wparam) == 0)) {
        DestroyWindow(hwnd);
        return 0;
      }
      if (id == kIdAdd || id == IDOK) {
        if (state == nullptr || state->remote_edit == nullptr ||
            !state->on_add) {
          DestroyWindow(hwnd);
          return 0;
        }
        int const len = GetWindowTextLengthW(state->remote_edit);
        std::wstring wide;
        wide.resize(static_cast<std::size_t>(len) + 1);
        int const copied =
            GetWindowTextW(state->remote_edit, &wide[0], len + 1);
        wide.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
        wide = TrimWide(std::move(wide));
        auto const result = state->on_add(WideToUtf8(wide));
        if (result == AddPeerUiResult::Ok) {
          state->accepted = true;
          DestroyWindow(hwnd);
        } else {
          ShowValidationError(hwnd, result);
          SetFocus(state->remote_edit);
        }
        return 0;
      }
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (state != nullptr) {
        state->open = false;
      }
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

}  // namespace

bool ShowAddPeerDialog(
    HWND owner, std::string const& local_aether_uid,
    std::function<AddPeerUiResult(std::string const&)> on_add) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = &DialogProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kDialogClass;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);
    registered = true;
  }

  DialogState state;
  state.local_uid = Utf8ToWide(local_aether_uid);
  state.on_add = std::move(on_add);

  RECT owner_rect{};
  if (owner != nullptr) {
    GetWindowRect(owner, &owner_rect);
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &owner_rect, 0);
  }
  int const width = 400;
  int const height = 210;
  int const x =
      owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
  int const y =
      owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;

  HWND dialog = CreateWindowExW(
      WS_EX_DLGMODALFRAME, kDialogClass, L"Add participant",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, width, height,
      owner, nullptr, GetModuleHandleW(nullptr), &state);
  if (dialog == nullptr) {
    return false;
  }

  if (owner != nullptr) {
    EnableWindow(owner, FALSE);
  }

  MSG msg{};
  while (state.open && GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsWindow(dialog)) {
      break;
    }
    if (!IsDialogMessageW(dialog, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }

  if (owner != nullptr) {
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
  }
  return state.accepted;
}

}  // namespace apptraverse
