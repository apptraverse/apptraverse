#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

#include <cwchar>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "apptraverse/noninteractive_crt.h"

#include "main_window_lifecycle.h"
#include "main_window_model.h"
#include "win_app.h"
#include "win_presenters.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

HWND FindExact(wchar_t const* class_name, wchar_t const* title) {
  return FindWindowW(class_name, title);
}

int CountClassWindows(wchar_t const* class_name) {
  struct Ctx {
    wchar_t const* class_name;
    int count;
  } ctx{class_name, 0};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        wchar_t name[256]{};
        if (GetClassNameW(hwnd, name, 256) > 0 &&
            wcscmp(name, c->class_name) == 0 && IsWindowVisible(hwnd)) {
          ++c->count;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.count;
}

bool WaitForWindow(wchar_t const* class_name, wchar_t const* title, HWND* out,
                    std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    HWND hwnd = FindExact(class_name, title);
    if (hwnd != nullptr) {
      if (out != nullptr) {
        *out = hwnd;
      }
      return true;
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 20,
                              QS_ALLINPUT | QS_ALLPOSTMESSAGE);
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return false;
}

bool WaitGone(wchar_t const* class_name, wchar_t const* title,
              std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (FindExact(class_name, title) == nullptr) {
      return true;
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, 20,
                              QS_ALLINPUT | QS_ALLPOSTMESSAGE);
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return false;
}

void TestInProcessLoadingThenMain() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_inproc";
  std::filesystem::remove_all(dir);

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND loading = nullptr;
  CHECK(WaitForWindow(kLoadingWindowClass, kLoadingWindowTitle, &loading,
                        std::chrono::seconds{30}));
  HWND main = nullptr;
  CHECK(WaitForWindow(kMainWindowClass, kMainWindowTitle, &main,
                      std::chrono::seconds{30}));
  CHECK(WaitGone(kLoadingWindowClass, kLoadingWindowTitle,
                 std::chrono::seconds{10}));
  CHECK(FindExact(kMainWindowClass, kMainWindowTitle) != nullptr);
  CHECK(CountClassWindows(kMainWindowClass) == 1);
  PostMessageW(main, WM_CLOSE, 0, 0);
  gui.join();
  CHECK(FindExact(kMainWindowClass, kMainWindowTitle) == nullptr);
  std::filesystem::remove_all(dir);
}

#ifdef WIN32_MAIN_WINDOW_DEMO_EXE

HANDLE StartDemo(std::filesystem::path const& exe,
                  std::filesystem::path const& state_dir) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.wstring().c_str(), buf.data(), nullptr, nullptr,
                            FALSE, 0, nullptr, nullptr, &si, &pi);
  CHECK(ok);
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

void TestChildProcessFreshThenClose() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_child";
  std::filesystem::remove_all(dir);
  std::filesystem::path exe{WIN32_MAIN_WINDOW_DEMO_EXE};
  HANDLE process = StartDemo(exe, dir);
  HWND loading = nullptr;
  CHECK(WaitForWindow(kLoadingWindowClass, kLoadingWindowTitle, &loading,
                        std::chrono::seconds{30}));
  HWND main = nullptr;
  CHECK(WaitForWindow(kMainWindowClass, kMainWindowTitle, &main,
                      std::chrono::seconds{30}));
  CHECK(WaitGone(kLoadingWindowClass, kLoadingWindowTitle,
                 std::chrono::seconds{10}));
  PostMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(process, 30000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(process, &code);
  CHECK(code == 0);
  CloseHandle(process);
  std::filesystem::remove_all(dir);
}

#endif

}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureMainWindowRegistration();
  apptraverse::EnsureWin32MainWindowPresenterRegistration();
  apptraverse::test::TestInProcessLoadingThenMain();
#ifdef WIN32_MAIN_WINDOW_DEMO_EXE
  apptraverse::test::TestChildProcessFreshThenClose();
#endif
  std::cout << "main_window_win32_smoke_test OK\n";
  return 0;
}
