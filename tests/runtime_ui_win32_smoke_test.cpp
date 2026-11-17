#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
namespace {
void Check(bool result, char const* message) {
  if (!result) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
void Until(std::function<bool()> predicate, char const* message) {
  auto const limit =
      std::chrono::steady_clock::now() + std::chrono::seconds(12);
  while (!predicate() && std::chrono::steady_clock::now() < limit)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Check(predicate(), message);
}
struct Process {
  PROCESS_INFORMATION info{};
  Process(std::filesystem::path const& executable,
          std::filesystem::path const& profile) {
    auto command = L"\"" + executable.wstring() + L"\" --state-dir \"" +
                   profile.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                         &info) != 0,
          "launch");
  }
  ~Process() {
    if (WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT)
      TerminateProcess(info.hProcess, 1);
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
  }
  HWND Window() const {
    struct State {
      DWORD pid;
      HWND hwnd;
    } state{info.dwProcessId, nullptr};
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
          auto& s = *reinterpret_cast<State*>(parameter);
          DWORD pid{};
          GetWindowThreadProcessId(window, &pid);
          if (pid == s.pid && IsWindowVisible(window)) {
            wchar_t title[128]{};
            GetWindowTextW(window, title, 128);
#if defined(RUNTIME_UI_TEST_DYNAMIC)
            if (std::wstring{title} == L"Dynamic Objects") {
              s.hwnd = window;
              return FALSE;
            }
#else
            if (std::wstring{title} == L"Main Window Runtime") {
              s.hwnd = window;
              return FALSE;
            }
#endif
          }
          return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));
    return state.hwnd;
  }
  void Close(HWND window) {
    PostMessageW(window, WM_CLOSE, 0, 0);
    Check(WaitForSingleObject(info.hProcess, 12000) == WAIT_OBJECT_0,
          "native close completes");
    DWORD status{};
    Check(GetExitCodeProcess(info.hProcess, &status) != 0 && status == 0,
          "shutdown persisted successfully");
  }
};
std::filesystem::path UserDirectory(std::filesystem::path const& root) {
  for (auto const& entry : std::filesystem::directory_iterator{root})
    if (entry.is_directory() &&
        entry.path().filename().string().starts_with("debug-run-"))
      return entry.path() / "user";
  Check(false, "fresh Debug directory");
  return {};
}
void Click(HWND window, int id) {
  HWND control = GetDlgItem(window, id);
  Check(control != nullptr, "required native control");
  SendMessageW(control, BM_CLICK, 0, 0);
}
}  // namespace
int main() {
  auto root =
      std::filesystem::temp_directory_path() /
      ("apptraverse-runtime-ui-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::create_directories(root);
  RECT placed{};
  {
    Process process{RUNTIME_UI_DEMO_EXE, root};
    HWND window{};
    Until(
        [&] {
          window = process.Window();
          return window != nullptr;
        },
        "initial native presented window");
#if defined(RUNTIME_UI_TEST_DYNAMIC)
    Until([&] { return GetDlgItem(window, 100) && GetDlgItem(window, 1001); },
          "initial Add and per-item Remove");
    Click(window, 100);
    Until([&] { return GetDlgItem(window, 1002) != nullptr; },
          "Add creates a native dynamic Item");
    Click(window, 100);
    Until([&] { return GetDlgItem(window, 1003) != nullptr; },
          "second Add creates distinct Item");
    Click(window, 1001);
    Until([&] { return GetDlgItem(window, 1001) == nullptr; },
          "Remove destroys only selected Item");
    Check(GetDlgItem(window, 1002) != nullptr &&
              GetDlgItem(window, 1003) != nullptr,
          "other native Item identities survive");
    RECT button{};
    GetWindowRect(GetDlgItem(window, 1003), &button);
    Check(button.right > button.left && button.bottom > button.top,
          "native measured layout");
#endif
    SendMessageW(window, WM_ENTERSIZEMOVE, 0, 0);
    Check(SetWindowPos(window, nullptr, 210, 170, 710, 510,
                       SWP_NOZORDER | SWP_NOACTIVATE) != 0,
          "native user resize");
    SendMessageW(window, WM_EXITSIZEMOVE, 0, 0);
    GetWindowRect(window, &placed);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    process.Close(window);
  }
  auto const user = UserDirectory(root);
  Check(std::filesystem::exists(user / "user.bin"), "portable USER persisted");
  Check(std::filesystem::exists(user / "local" / "user.bin"),
        "separate native LOCAL persisted");
  {
    Process process{RUNTIME_UI_LOAD_ONLY_EXE, user};
    HWND window{};
    Until(
        [&] {
          window = process.Window();
          return window != nullptr;
        },
        "cold load native window");
    Until(
        [&] {
          RECT actual{};
          GetWindowRect(window, &actual);
          return abs(actual.left - placed.left) <= 2 &&
                 abs(actual.top - placed.top) <= 2 &&
                 abs((actual.right - actual.left) -
                     (placed.right - placed.left)) <= 2 &&
                 abs((actual.bottom - actual.top) -
                     (placed.bottom - placed.top)) <= 2;
        },
        "LOCAL restores native outer frame");
#if defined(RUNTIME_UI_TEST_DYNAMIC)
    Until([&] { return GetDlgItem(window, 1002) && GetDlgItem(window, 1003); },
          "cold load retains dynamic Items");
    Check(GetDlgItem(window, 1001) == nullptr, "removed Item stays removed");
    Click(window, 100);
    Until([&] { return GetDlgItem(window, 1004) != nullptr; },
          "cold load Add preserves numbering");
    Click(window, 1002);
    Until([&] { return GetDlgItem(window, 1002) == nullptr; },
          "cold load Remove stays functional");
#endif
    process.Close(window);
  }
  std::cout << "PASS: native UI, dynamic topology (when enabled), Debug fresh "
               "graph, USER/LOCAL shutdown and cold frame restore\n";
}
