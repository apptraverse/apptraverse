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
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "apptraverse/directory_domain_storage.h"

#include "main_window_ids.h"
#include "main_window_lifecycle.h"
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

HWND FindOwned(DWORD pid, wchar_t const* class_name, wchar_t const* title) {
  struct Ctx {
    DWORD pid;
    wchar_t const* class_name;
    wchar_t const* title;
    HWND found;
  } ctx{pid, class_name, title, nullptr};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(hwnd, &window_pid);
        if (window_pid != c->pid) {
          return TRUE;
        }
        wchar_t name[256]{};
        if (GetClassNameW(hwnd, name, 256) <= 0 ||
            wcscmp(name, c->class_name) != 0) {
          return TRUE;
        }
        if (c->title != nullptr) {
          wchar_t text[256]{};
          GetWindowTextW(hwnd, text, 256);
          if (wcscmp(text, c->title) != 0) {
            return TRUE;
          }
        }
        if (IsWindowVisible(hwnd) == 0) {
          return TRUE;
        }
        c->found = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.found;
}

int CountOwnedClass(DWORD pid, wchar_t const* class_name) {
  struct Ctx {
    DWORD pid;
    wchar_t const* class_name;
    int count;
  } ctx{pid, class_name, 0};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD window_pid = 0;
        GetWindowThreadProcessId(hwnd, &window_pid);
        if (window_pid != c->pid) {
          return TRUE;
        }
        wchar_t name[256]{};
        if (GetClassNameW(hwnd, name, 256) > 0 &&
            wcscmp(name, c->class_name) == 0 && IsWindowVisible(hwnd) != 0) {
          ++c->count;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.count;
}

void PumpGui(std::chrono::milliseconds slice) {
  MsgWaitForMultipleObjects(0, nullptr, FALSE,
                             static_cast<DWORD>(slice.count()),
                             QS_ALLINPUT | QS_ALLPOSTMESSAGE);
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

bool WaitOwned(DWORD pid, wchar_t const* class_name, wchar_t const* title,
                HWND* out, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    HWND hwnd = FindOwned(pid, class_name, title);
    if (hwnd != nullptr) {
      if (out != nullptr) {
        *out = hwnd;
      }
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

bool WaitOwnedGone(DWORD pid, wchar_t const* class_name, wchar_t const* title,
                    std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (FindOwned(pid, class_name, title) == nullptr) {
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

void WaitPublished(ModelSession& session) {
  std::unique_lock<std::mutex> lock{session.mu};
  CHECK(session.cv.wait_for(lock, std::chrono::seconds{30}, [&] {
    return session.channel.has_unread_published();
  }));
}

void TestInProcessStartupShutdown() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_inproc";
  std::filesystem::remove_all(dir);

  WinApp app;
  std::thread gui{[&] { CHECK(app.Run(dir) == 0); }};

  HWND main = nullptr;
  CHECK(WaitOwned(pid, kMainWindowClass, kMainWindowTitle, &main,
                  std::chrono::seconds{30}));
  CHECK(CountOwnedClass(pid, kMainWindowClass) == 1);
  SendMessageW(main, WM_CLOSE, 0, 0);
  // MSVC: native_handle() is a Win32 HANDLE. MinGW/winpthread: it is not.
#if defined(_MSC_VER)
  CHECK(WaitForSingleObject(gui.native_handle(), 30000) == WAIT_OBJECT_0);
#endif
  gui.join();
  CHECK(FindOwned(pid, kMainWindowClass, kMainWindowTitle) == nullptr);
  std::filesystem::remove_all(dir);
}

void TestInProcessTwice() {
  TestInProcessStartupShutdown();
  TestInProcessStartupShutdown();
}

void TestModelSessionDoesNotCreateMain() {
  DWORD const pid = GetCurrentProcessId();
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_model_no_hwnd";
  std::filesystem::remove_all(dir);
  CHECK(CountOwnedClass(pid, kMainWindowClass) == 0);

  ModelSession session;
  session.state_dir = dir;
  std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
  WaitPublished(session);
  CHECK(CountOwnedClass(pid, kMainWindowClass) == 0);
  session.RequestStop();
  model.join();
  CHECK(CountOwnedClass(pid, kMainWindowClass) == 0);
  std::filesystem::remove_all(dir);
}

#ifdef WIN32_MAIN_WINDOW_DEMO_EXE

struct ChildProcess {
  HANDLE process;
  DWORD pid;
};

ChildProcess StartDemo(std::filesystem::path const& exe,
                       std::filesystem::path const& state_dir,
                       std::filesystem::path const& stderr_path = {}) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  HANDLE errf = INVALID_HANDLE_VALUE;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (!stderr_path.empty()) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    errf = CreateFileW(stderr_path.wstring().c_str(), GENERIC_WRITE,
                        FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(errf != INVALID_HANDLE_VALUE);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = errf;
  }
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.wstring().c_str(), buf.data(), nullptr, nullptr,
                            errf != INVALID_HANDLE_VALUE, 0, nullptr, nullptr,
                            &si, &pi);
  if (errf != INVALID_HANDLE_VALUE) {
    CloseHandle(errf);
  }
  CHECK(ok);
  CloseHandle(pi.hThread);
  return ChildProcess{pi.hProcess, pi.dwProcessId};
}

using StateSnapshot = std::map<std::string, std::vector<char>>;

StateSnapshot SnapshotState(std::filesystem::path const& dir) {
  StateSnapshot files;
  if (!std::filesystem::exists(dir)) {
    return files;
  }
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator{dir}) {
    if (!entry.is_regular_file()) {
      continue;
    }
    std::ifstream in{entry.path(), std::ios::binary};
    files.emplace(std::filesystem::relative(entry.path(), dir).generic_string(),
                  std::vector<char>{std::istreambuf_iterator<char>{in},
                                    std::istreambuf_iterator<char>{}});
  }
  return files;
}

std::size_t CountStateEntries(std::filesystem::path const& dir) {
  std::size_t count = 0;
  if (!std::filesystem::exists(dir)) {
    return 0;
  }
  for (auto const& entry : std::filesystem::directory_iterator{dir}) {
    (void)entry;
    ++count;
  }
  return count;
}

bool WaitRect(HWND hwnd, RECT const& expected, RECT* settled,
              std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    RECT actual{};
    if (GetWindowRect(hwnd, &actual) != 0 && actual.left == expected.left &&
        actual.top == expected.top && actual.right == expected.right &&
        actual.bottom == expected.bottom) {
      if (settled != nullptr) {
        *settled = actual;
      }
      return true;
    }
    PumpGui(std::chrono::milliseconds{20});
  }
  return false;
}

void TestChildProcessResizePersists() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_child_resize";
  auto err_path = dir;
  err_path += ".stderr.txt";
  std::filesystem::remove_all(dir);
  std::filesystem::remove(err_path);
  std::filesystem::path exe{WIN32_MAIN_WINDOW_DEMO_EXE};
  ChildProcess child = StartDemo(exe, dir, err_path);
  HWND main = nullptr;
  CHECK(WaitOwned(child.pid, kMainWindowClass, kMainWindowTitle, &main,
                  std::chrono::seconds{30}));
  auto const before = CountStateEntries(dir);
  RECT desired{120, 90, 120 + 640, 90 + 480};
  CHECK(SetWindowPos(main, nullptr, desired.left, desired.top, 640, 480,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  RECT settled{};
  CHECK(WaitRect(main, desired, &settled, std::chrono::seconds{10}));
  auto const before_files = SnapshotState(dir);
  auto const persist_deadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds{2};
  while (std::chrono::steady_clock::now() < persist_deadline) {
    if (WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0) {
      DWORD code = 1;
      GetExitCodeProcess(child.process, &code);
      std::cerr << "resize child exited early code=" << code << " entries="
                << CountStateEntries(dir) << " before=" << before << '\n';
      std::ifstream err{err_path};
      std::cerr << err.rdbuf();
      std::exit(1);
    }
    CHECK(SnapshotState(dir) == before_files);
    PumpGui(std::chrono::milliseconds{20});
  }
  PostMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(child.process, 30000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(child.process, &code);
  CHECK(code == 0);
  CloseHandle(child.process);

  std::filesystem::path load_only{WIN32_MAIN_WINDOW_LOAD_ONLY_EXE};
  ChildProcess restarted = StartDemo(load_only, dir);
  HWND restored = nullptr;
  CHECK(WaitOwned(restarted.pid, kMainWindowClass, kMainWindowTitle, &restored,
                  std::chrono::seconds{30}));
  CHECK(WaitRect(restored, settled, nullptr, std::chrono::seconds{10}));
  PostMessageW(restored, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(restarted.process, 30000) == WAIT_OBJECT_0);
  GetExitCodeProcess(restarted.process, &code);
  CHECK(code == 0);
  CloseHandle(restarted.process);
  std::filesystem::remove_all(dir);
}

bool RectEqual(RECT const& a, RECT const& b) {
  return a.left == b.left && a.top == b.top && a.right == b.right &&
         a.bottom == b.bottom;
}

void TestChildProcessResizeBurstDoesNotEchoStale() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_child_resize_burst";
  auto err_path = dir;
  err_path += ".stderr.txt";
  std::filesystem::remove_all(dir);
  std::filesystem::remove(err_path);
  std::filesystem::path exe{WIN32_MAIN_WINDOW_DEMO_EXE};
  ChildProcess child = StartDemo(exe, dir, err_path);
  HWND main = nullptr;
  CHECK(WaitOwned(child.pid, kMainWindowClass, kMainWindowTitle, &main,
                  std::chrono::seconds{30}));

  RECT request_a{200, 100, 200 + 600, 100 + 400};
  RECT request_b{160, 120, 160 + 640, 120 + 420};
  RECT request_c{140, 140, 140 + 700, 140 + 480};
  CHECK(SetWindowPos(main, nullptr, request_a.left, request_a.top, 600, 400,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  RECT actual_a{};
  CHECK(GetWindowRect(main, &actual_a) != 0);
  CHECK(SetWindowPos(main, nullptr, request_b.left, request_b.top, 640, 420,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  RECT actual_b{};
  CHECK(GetWindowRect(main, &actual_b) != 0);
  CHECK(SetWindowPos(main, nullptr, request_c.left, request_c.top, 700, 480,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  RECT actual_c{};
  CHECK(GetWindowRect(main, &actual_c) != 0);
  CHECK(actual_c.left != actual_a.left);

  auto const before_files = SnapshotState(dir);
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
  while (std::chrono::steady_clock::now() < deadline) {
    if (WaitForSingleObject(child.process, 0) == WAIT_OBJECT_0) {
      DWORD code = 1;
      GetExitCodeProcess(child.process, &code);
      std::cerr << "resize burst child exited code=" << code << '\n';
      std::ifstream err{err_path};
      std::cerr << err.rdbuf();
      std::exit(1);
    }
    RECT actual{};
    CHECK(GetWindowRect(main, &actual) != 0);
    if (RectEqual(actual, actual_a) || RectEqual(actual, actual_b)) {
      std::cerr << "stale window rect rolled back to an earlier resize\n";
      std::exit(1);
    }
    CHECK(RectEqual(actual, actual_c));
    CHECK(SnapshotState(dir) == before_files);
    PumpGui(std::chrono::milliseconds{20});
  }
  RECT final_rect{};
  CHECK(GetWindowRect(main, &final_rect) != 0);
  CHECK(RectEqual(final_rect, actual_c));

  PostMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(child.process, 30000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(child.process, &code);
  CHECK(code == 0);
  CloseHandle(child.process);

  std::filesystem::path load_only{WIN32_MAIN_WINDOW_LOAD_ONLY_EXE};
  ChildProcess restarted = StartDemo(load_only, dir);
  HWND restored = nullptr;
  CHECK(WaitOwned(restarted.pid, kMainWindowClass, kMainWindowTitle, &restored,
                  std::chrono::seconds{30}));
  CHECK(WaitRect(restored, actual_c, nullptr, std::chrono::seconds{10}));
  PostMessageW(restored, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(restarted.process, 30000) == WAIT_OBJECT_0);
  GetExitCodeProcess(restarted.process, &code);
  CHECK(code == 0);
  CloseHandle(restarted.process);
  std::filesystem::remove_all(dir);
}

void TestChildProcessFreshThenClose() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_child";
  std::filesystem::remove_all(dir);
  std::filesystem::path exe{WIN32_MAIN_WINDOW_DEMO_EXE};
  ChildProcess child = StartDemo(exe, dir);
  HWND main = nullptr;
  CHECK(WaitOwned(child.pid, kMainWindowClass, kMainWindowTitle, &main,
                  std::chrono::seconds{30}));
  CHECK(CountOwnedClass(child.pid, kMainWindowClass) == 1);
  PostMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(child.process, 30000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(child.process, &code);
  CHECK(code == 0);
  CloseHandle(child.process);
  std::filesystem::remove_all(dir);
}

void TestChildProcessLoadOnlyThenClose() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_child_load_only";
  std::filesystem::remove_all(dir);
  {
    ModelSession session;
    session.state_dir = dir;
    std::thread model{[&] {
    session.Run([&session](PublicationKind) { session.cv.notify_all(); });
  }};
    WaitPublished(session);
    session.RequestStop();
    model.join();
  }
  std::filesystem::path exe{WIN32_MAIN_WINDOW_LOAD_ONLY_EXE};
  ChildProcess child = StartDemo(exe, dir);
  HWND main = nullptr;
  CHECK(WaitOwned(child.pid, kMainWindowClass, kMainWindowTitle, &main,
                  std::chrono::seconds{30}));
  PostMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitForSingleObject(child.process, 30000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(child.process, &code);
  CHECK(code == 0);
  CloseHandle(child.process);
  std::filesystem::remove_all(dir);
}

void TestChildProcessLoadOnlyEmptyState() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_main_window_child_load_only_empty";
  auto err_path = dir;
  err_path += ".stderr.txt";
  std::filesystem::remove_all(dir);
  std::filesystem::remove(err_path);
  std::filesystem::create_directories(dir);
  std::filesystem::path exe{WIN32_MAIN_WINDOW_LOAD_ONLY_EXE};
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     dir.wstring() + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE errf = CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(errf != INVALID_HANDLE_VALUE);
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = errf;
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.wstring().c_str(), buf.data(), nullptr, nullptr,
                            TRUE, 0, nullptr, nullptr, &si, &pi);
  CloseHandle(errf);
  CHECK(ok);
  CloseHandle(pi.hThread);
  CHECK(WaitForSingleObject(pi.hProcess, 30000) == WAIT_OBJECT_0);
  DWORD code = 0;
  GetExitCodeProcess(pi.hProcess, &code);
  CHECK(code != 0);
  HANDLE err_in = CreateFileW(err_path.wstring().c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(err_in != INVALID_HANDLE_VALUE);
  DWORD size = GetFileSize(err_in, nullptr);
  CHECK(size != INVALID_FILE_SIZE);
  std::string err(size, '\0');
  DWORD read = 0;
  CHECK(ReadFile(err_in, err.data(), size, &read, nullptr) != 0);
  CloseHandle(err_in);
  err.resize(read);
  if (err.find("fatal: LoadApplication failed") == std::string::npos) {
    std::cerr << "load-only empty stderr was:\n" << err << '\n';
    std::exit(1);
  }
  CHECK(err.find("fatal: LoadApplication failed",
                 err.find("fatal: LoadApplication failed") + 1) ==
        std::string::npos);
  CHECK(FindOwned(pi.dwProcessId, kMainWindowClass, kMainWindowTitle) ==
        nullptr);
  DirectoryDomainStorage storage{dir};
  CHECK(storage
            .Enumerate(ae::ObjId{main_window::ToObjId(
                main_window::ObjId::Application)})
            .empty());
  CloseHandle(pi.hProcess);
  std::filesystem::remove_all(dir);
  std::filesystem::remove(err_path);
}

#endif

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestModelSessionDoesNotCreateMain();
  apptraverse::test::TestInProcessTwice();
#ifdef WIN32_MAIN_WINDOW_DEMO_EXE
  apptraverse::test::TestChildProcessResizePersists();
  apptraverse::test::TestChildProcessResizeBurstDoesNotEchoStale();
  apptraverse::test::TestChildProcessFreshThenClose();
  apptraverse::test::TestChildProcessLoadOnlyThenClose();
  apptraverse::test::TestChildProcessLoadOnlyEmptyState();
#endif
  std::cout << "main_window_win32_smoke_test OK\n";
  return 0;
}
