#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

constexpr std::uint32_t kAddControlId = 5002;
constexpr std::uint32_t kOpenControlId = 5003;
constexpr std::uint32_t kDeleteControlId = 5004;
constexpr std::uint32_t kLanguageControlId = 5005;

std::filesystem::path ExeDir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path{path}.parent_path();
}

std::filesystem::path DemoExe(char const* name) {
  auto const dir = ExeDir();
  auto candidate = dir / name;
  if (std::filesystem::exists(candidate)) {
    return candidate;
  }
  return dir / ".." / "examples" / "surfaces_demo" / "windows" / name;
}

std::filesystem::path FreshStateDir(char const* tag) {
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_test" /
             tag;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

int CountChildClass(HWND parent, wchar_t const* class_name);

bool WaitUntil(std::chrono::milliseconds timeout, auto predicate) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    Sleep(25);
  }
  return predicate();
}

HWND WaitCatalogMainWindow(DWORD pid, std::chrono::milliseconds timeout) {
  HWND found = nullptr;
  WaitUntil(timeout, [&] {
    struct Ctx {
      DWORD pid;
      HWND* out;
    } ctx{pid, &found};
    EnumWindows(
        [](HWND hwnd, LPARAM lparam) -> BOOL {
          auto* c = reinterpret_cast<Ctx*>(lparam);
          DWORD window_pid = 0;
          GetWindowThreadProcessId(hwnd, &window_pid);
          if (window_pid != c->pid || !IsWindowVisible(hwnd)) {
            return TRUE;
          }
          wchar_t cls[64]{};
          GetClassNameW(hwnd, cls, 64);
          if (wcscmp(cls, L"AppTraverseWindowCatalogLoading") == 0) {
            return TRUE;
          }
          if (CountChildClass(hwnd, L"ListBox") < 1) {
            return TRUE;
          }
          *c->out = hwnd;
          return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return found != nullptr;
  });
  return found;
}

struct CountChildClassCtx {
  wchar_t const* class_name;
  int count;
};

int CountChildClass(HWND parent, wchar_t const* class_name) {
  CountChildClassCtx ctx{class_name, 0};
  EnumChildWindows(
      parent,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<CountChildClassCtx*>(lparam);
        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, c->class_name) == 0) {
          ++c->count;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return ctx.count;
}

std::vector<HWND> TopLevelWindowsForPidFixed(DWORD pid) {
  std::vector<HWND> out;
  struct Ctx {
    DWORD pid;
    std::vector<HWND>* out_hwnds;
  } ctx{pid, &out};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD p = 0;
        GetWindowThreadProcessId(hwnd, &p);
        if (p != c->pid || !IsWindowVisible(hwnd)) {
          return TRUE;
        }
        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"AppTraverseWindowCatalogLoading") == 0) {
          return TRUE;
        }
        c->out_hwnds->push_back(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return out;
}

void ClickControl(HWND parent, std::uint32_t control_id) {
  HWND const child = GetDlgItem(parent, static_cast<int>(control_id));
  CHECK(child != nullptr);
  SendMessageW(parent, WM_COMMAND,
               MAKEWPARAM(control_id, BN_CLICKED),
               reinterpret_cast<LPARAM>(child));
}

std::wstring GetWindowTextHwnd(HWND hwnd) {
  int const len = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<std::size_t>(len + 1), L'\0');
  if (len > 0) {
    GetWindowTextW(hwnd, text.data(), len + 1);
  }
  text.resize(static_cast<std::size_t>(len));
  return text;
}

void WaitChromeReady(HWND main) {
  CHECK(WaitUntil(std::chrono::seconds{20}, [&] {
    return CountChildClass(main, L"Button") >= 4;
  }));
}

void RunAcceptance() {
  auto const state_dir = FreshStateDir("window_catalog_win32_acceptance");
  auto const demo = DemoExe("win32_window_catalog_demo.exe");
  auto const load_only = DemoExe("win32_window_catalog_demo_load_only.exe");
  CHECK(std::filesystem::exists(demo));
  CHECK(std::filesystem::exists(load_only));

  std::wstring cmd = L"\"" + demo.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  CHECK(CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi));
  CloseHandle(pi.hThread);
  HWND main = WaitCatalogMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);
  CHECK(GetWindowTextHwnd(main).find(L"Window Catalog") != std::wstring::npos);

  for (int i = 0; i < 3; ++i) {
    ClickControl(main, kAddControlId);
    WaitUntil(std::chrono::seconds{5}, [&] {
      return CountChildClass(main, L"ListBox") >= 1;
    });
  }
  WaitUntil(std::chrono::seconds{10}, [&] {
    return TopLevelWindowsForPidFixed(pi.dwProcessId).size() >= 4;
  });
  auto tops = TopLevelWindowsForPidFixed(pi.dwProcessId);
  CHECK(tops.size() >= 4);

  HWND child2 = tops.size() >= 2 ? tops[1] : nullptr;
  for (HWND h : tops) {
    if (h != main && GetWindowTextHwnd(h).find(L"Window 2") != std::wstring::npos) {
      child2 = h;
      break;
    }
  }
  CHECK(child2 != nullptr);
  SendMessageW(child2, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return !IsWindow(child2);
  }));

  HWND list = GetDlgItem(main, static_cast<int>(5001));
  CHECK(list != nullptr);
  SendMessageW(list, LB_SETCURSEL, 1, 0);
  ClickControl(main, kOpenControlId);
  WaitUntil(std::chrono::seconds{10}, [&] {
    for (HWND h : TopLevelWindowsForPidFixed(pi.dwProcessId)) {
      if (GetWindowTextHwnd(h).find(L"Window 2") != std::wstring::npos) {
        return true;
      }
    }
    return false;
  });

  ClickControl(main, kLanguageControlId);
  WaitUntil(std::chrono::seconds{10}, [&] {
    return GetWindowTextHwnd(GetDlgItem(main, kAddControlId)).find(L"\x0414") !=
               std::wstring::npos ||
           GetWindowTextHwnd(GetDlgItem(main, kAddControlId)).find(L"Добавить") !=
               std::wstring::npos;
  });
  ClickControl(main, kLanguageControlId);

  SendMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{20}, [&] { return WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0; }));
  CloseHandle(pi.hProcess);

  cmd = L"\"" + load_only.wstring() + L"\" --state-dir \"" +
        state_dir.wstring() + L"\"";
  cmd_buf.assign(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');
  CHECK(CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &si, &pi));
  CloseHandle(pi.hThread);
  main = WaitCatalogMainWindow(pi.dwProcessId, std::chrono::seconds{30});
  CHECK(main != nullptr);
  WaitChromeReady(main);
  WaitUntil(std::chrono::seconds{10}, [&] {
    return TopLevelWindowsForPidFixed(pi.dwProcessId).size() >= 3;
  });
  SendMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{20}, [&] { return WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0; }));
  CloseHandle(pi.hProcess);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunAcceptance();
  std::cout << "window_catalog_win32_acceptance_test OK\n";
  return 0;
}
