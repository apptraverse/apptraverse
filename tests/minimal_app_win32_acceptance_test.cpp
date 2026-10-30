#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_application.h"

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

constexpr wchar_t kDesktopClass[] = L"AppTraverseUiDesktopWindow";
constexpr wchar_t kLoadingClass[] = L"AppTraverseMinimalAppLoading";

struct Frame {
  int x = 0;
  int y = 0;
  int client_width = 0;
  int client_height = 0;
};

Frame ReadFrame(HWND hwnd) {
  Frame frame;
  apptraverse::ui::windows::ReadModelFrameFromHwnd(
      hwnd, frame.x, frame.y, frame.client_width, frame.client_height);
  return frame;
}

bool FramesNear(Frame const& a, Frame const& b, int tolerance = 3) {
  return std::abs(a.x - b.x) <= tolerance &&
         std::abs(a.y - b.y) <= tolerance &&
         std::abs(a.client_width - b.client_width) <= tolerance &&
         std::abs(a.client_height - b.client_height) <= tolerance;
}

void ApplyFrame(HWND hwnd, int x, int y, int cw, int ch) {
  int outer_w = 0;
  int outer_h = 0;
  apptraverse::ui::windows::ClientSizeToOuter(cw, ch, outer_w, outer_h);
  CHECK(SetWindowPos(hwnd, nullptr, x, y, outer_w, outer_h,
                     SWP_NOZORDER | SWP_NOACTIVATE) != 0);
  SendMessageW(hwnd, WM_EXITSIZEMOVE, 0, 0);
}

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

std::filesystem::path ExeDir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path{path}.parent_path();
}

std::filesystem::path DemoExe(char const* name) {
  std::error_code ec;
  auto try_path = [&](std::filesystem::path const& p) {
    auto abs = std::filesystem::weakly_canonical(p, ec);
    if (!ec && std::filesystem::exists(abs)) {
      return abs;
    }
    return std::filesystem::path{};
  };
  if (auto p = try_path(ExeDir() / name); !p.empty()) {
    return p;
  }
  if (auto p = try_path(ExeDir() / ".." / "examples" / "minimal_app" / "windows" /
                        name);
      !p.empty()) {
    return p;
  }
  return ExeDir() / name;
}

std::filesystem::path FreshStateDir() {
  auto dir = std::filesystem::temp_directory_path() /
             "apptraverse_test" / "minimal_app_win32_acceptance";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::filesystem::path NegativeStateDir(char const* suffix) {
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_test" /
             ("minimal_app_win32_acceptance_" + std::string{suffix});
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

void CopyStateDir(std::filesystem::path const& from,
                  std::filesystem::path const& to) {
  std::error_code ec;
  std::filesystem::copy(from, to,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        ec);
  CHECK(!ec);
}

std::string ReadAllBytes(std::filesystem::path const& path) {
  HANDLE file =
      CreateFileW(path.wstring().c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(file != INVALID_HANDLE_VALUE);
  DWORD size = GetFileSize(file, nullptr);
  CHECK(size != INVALID_FILE_SIZE);
  std::string out(size, '\0');
  DWORD read = 0;
  CHECK(ReadFile(file, out.data(), size, &read, nullptr) != 0);
  CloseHandle(file);
  out.resize(read);
  return out;
}

std::size_t CountNeedle(std::string const& hay, char const* needle) {
  std::size_t n = 0;
  for (std::size_t pos = 0;;) {
    pos = hay.find(needle, pos);
    if (pos == std::string::npos) {
      break;
    }
    ++n;
    ++pos;
  }
  return n;
}

struct ChildRunResult {
  DWORD exit_code = 0;
  std::string stderr_text;
};

ChildRunResult RunLoadOnlyCaptureStderr(
    std::filesystem::path const& exe,
    std::filesystem::path const& state_dir,
    DWORD timeout_ms = 30000) {
  auto err_path = state_dir;
  err_path += L".stderr.txt";
  std::error_code ec;
  std::filesystem::remove(err_path, ec);

  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE errf = CreateFileW(err_path.wstring().c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(errf != INVALID_HANDLE_VALUE);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_EXISTING, 0, nullptr);
  CHECK(nul != INVALID_HANDLE_VALUE);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = nul;
  si.hStdOutput = nul;
  si.hStdError = errf;
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(exe.wstring().c_str(), cmd_buf.data(), nullptr,
                           nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &si, &pi);
  CloseHandle(errf);
  CloseHandle(nul);
  CHECK(ok);
  CloseHandle(pi.hThread);

  DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms);
  if (wait == WAIT_TIMEOUT) {
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CHECK(false);
  }
  ChildRunResult result;
  GetExitCodeProcess(pi.hProcess, &result.exit_code);
  CloseHandle(pi.hProcess);
  result.stderr_text = ReadAllBytes(err_path);
  std::filesystem::remove(err_path, ec);
  return result;
}

void ExpectFatalLoadOnly(ChildRunResult const& result, char const* needle) {
  CHECK(result.exit_code != 0);
  if (result.stderr_text.find(needle) == std::string::npos) {
    std::cerr << "load-only stderr was:\n" << result.stderr_text << '\n';
    std::exit(1);
  }
  CHECK(CountNeedle(result.stderr_text, needle) >= 1);
}

void RegisterMinimalAppTypes() {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  example::minimal::EnsureMinimalApplicationRegistration();
}

void WriteProfileMissingMainWindow(std::filesystem::path const& state_dir) {
  RegisterMinimalAppTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          domain, example::minimal::MinimalApplicationRootId());
  ae::Ptr<example::minimal::MinimalApplication> app = loaded.Load();
  app->main_window = ui::UiDesktopWindow::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  if (app->base.is_valid()) {
    if (!app->base.is_loaded()) {
      app->base.Load();
    }
    auto& base_app = static_cast<example::minimal::MinimalApplication&>(
        *app->base.operator->());
    base_app.main_window = ui::UiDesktopWindow::ptr::Declare(
        ae::CreateWith{domain}.with_id(ae::ObjId{}));
    app->base.Save();
  }
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(app, example::minimal::MinimalApplicationRootId())
            .IsOk());
}

void AssertMainWindowRefAbsent(std::filesystem::path const& state_dir) {
  RegisterMinimalAppTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          domain, example::minimal::MinimalApplicationRootId());
  ae::Ptr<example::minimal::MinimalApplication> app = loaded.Load();
  CHECK(!app->main_window.is_valid());
}

void WriteProfileMissingPresenter(std::filesystem::path const& state_dir) {
  RegisterMinimalAppTypes();
  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  example::minimal::MinimalApplication::ptr loaded =
      LoadApplication<example::minimal::MinimalApplication>(
          domain, example::minimal::MinimalApplicationRootId());
  ae::Ptr<example::minimal::MinimalApplication> app = loaded.Load();
  ae::Ptr<ui::UiDesktopWindow> window = app->main_window.Load();
  window->presenter = ui::windows::WinDesktopWindowPresenter::ptr::Declare(
      ae::CreateWith{domain}.with_id(ae::ObjId{}));
  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(window, example::minimal::MinimalMainWindowId()).IsOk());
}

void WriteProfileMissingPresenterObject(std::filesystem::path const& state_dir) {
  RegisterMinimalAppTypes();
  DirectoryDomainStorage storage{state_dir};
  storage.Remove(example::minimal::MinimalPresenterId());
}

PROCESS_INFORMATION StartDemo(std::filesystem::path const& exe,
                              std::filesystem::path const& state_dir) {
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  CHECK(CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       nullptr, &si, &pi));
  CloseHandle(pi.hThread);
  return pi;
}

int CountChildWindows(HWND parent) {
  int count = 0;
  EnumChildWindows(
      parent,
      [](HWND, LPARAM lparam) -> BOOL {
        ++*reinterpret_cast<int*>(lparam);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&count));
  return count;
}

std::vector<HWND> DesktopWindowsForPid(DWORD pid) {
  std::vector<HWND> out;
  struct Ctx {
    DWORD pid;
    std::vector<HWND>* out;
  } ctx{pid, &out};
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
        if (wcscmp(cls, kLoadingClass) == 0) {
          return TRUE;
        }
        if (wcscmp(cls, kDesktopClass) != 0) {
          return TRUE;
        }
        c->out->push_back(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&ctx));
  return out;
}

HWND WaitMainWindow(DWORD pid, std::chrono::milliseconds timeout) {
  HWND found = nullptr;
  WaitUntil(timeout, [&] {
    auto windows = DesktopWindowsForPid(pid);
    if (windows.size() == 1) {
      found = windows[0];
      return true;
    }
    return false;
  });
  return found;
}

void CloseGracefully(HWND main, PROCESS_INFORMATION& pi) {
  SendMessageW(main, WM_CLOSE, 0, 0);
  CHECK(WaitUntil(std::chrono::seconds{20}, [&] {
    return WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0;
  }));
  DWORD exit_code = 1;
  CHECK(GetExitCodeProcess(pi.hProcess, &exit_code));
  CHECK(exit_code == 0);
  CloseHandle(pi.hProcess);
  pi.hProcess = nullptr;
}

void ProbePersistedGraph(std::filesystem::path const& state_dir,
                         Frame const& expected_frame) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  example::minimal::EnsureMinimalApplicationRegistration();

  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<example::minimal::MinimalApplication>(
      domain, example::minimal::MinimalApplicationRootId());

  CHECK(application->obj_id == example::minimal::MinimalApplicationRootId());
  CHECK(application->main_window.is_valid());
  CHECK(application->main_window.id() ==
        example::minimal::MinimalMainWindowId());
  if (!application->main_window.is_loaded()) {
    application->main_window.Load();
  }
  auto& window =
      static_cast<ui::UiDesktopWindow&>(*application->main_window.operator->());
  CHECK(window.presenter.is_valid());
  CHECK(window.presenter.id() == example::minimal::MinimalPresenterId());
  if (!window.presenter.is_loaded()) {
    window.presenter.Load();
  }
  CHECK(window.presenter->title.is_valid());
  CHECK(window.presenter->locale.is_valid());
  CHECK(window.presenter->title->Resolve(0) == "Minimal App");
  CHECK(window.presenter->window.id() == example::minimal::MinimalMainWindowId());

  CHECK(application->base.is_valid());
  CHECK(application->base.is_loaded());
  auto const& base_app =
      static_cast<example::minimal::MinimalApplication const&>(
          *application->base.operator->());
  CHECK(base_app.main_window.is_valid());
  CHECK(base_app.main_window.id() == example::minimal::MinimalMainWindowId());
  auto const& base_window = static_cast<ui::UiDesktopWindow const&>(
      *base_app.main_window.operator->());
  CHECK(base_window.presenter.is_valid());
  CHECK(base_window.presenter.id() == example::minimal::MinimalPresenterId());

  Frame model_frame;
  model_frame.x = window.x();
  model_frame.y = window.y();
  model_frame.client_width = window.client_width();
  model_frame.client_height = window.client_height();
  CHECK(FramesNear(model_frame, expected_frame));
}

std::filesystem::path RunAcceptance() {
  auto const state_dir = FreshStateDir();
  auto const demo = DemoExe("win32_minimal_app.exe");
  auto const load_only = DemoExe("win32_minimal_app_load_only.exe");
  CHECK(std::filesystem::exists(demo));
  CHECK(std::filesystem::exists(load_only));

  PROCESS_INFORMATION pi = StartDemo(demo, state_dir);
  DWORD const pid = pi.dwProcessId;

  CHECK(WaitUntil(std::chrono::seconds{5}, [&] {
    return FindWindowW(kLoadingClass, nullptr) == nullptr;
  }));

  HWND main = WaitMainWindow(pid, std::chrono::seconds{30});
  CHECK(main != nullptr);
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    return CountChildWindows(main) == 2;
  }));
  CHECK(DesktopWindowsForPid(pid).size() == 1);

  ApplyFrame(main, 220, 140, 720, 460);
  Frame const saved = ReadFrame(main);
  CHECK(WaitUntil(std::chrono::seconds{10}, [&] {
    return FramesNear(ReadFrame(main), saved);
  }));

  CloseGracefully(main, pi);

  ProbePersistedGraph(state_dir, saved);

  pi = StartDemo(load_only, state_dir);
  DWORD const load_pid = pi.dwProcessId;
  main = WaitMainWindow(load_pid, std::chrono::seconds{30});
  CHECK(main != nullptr);
  CHECK(WaitUntil(std::chrono::seconds{15}, [&] {
    return CountChildWindows(main) == 2;
  }));
  CHECK(DesktopWindowsForPid(load_pid).size() == 1);
  CHECK(FramesNear(ReadFrame(main), saved));

  CloseGracefully(main, pi);
  return state_dir;
}

void RunRequiredGraphNegativeTests(std::filesystem::path const& good_state,
                                   std::filesystem::path const& load_only) {
  {
    auto const empty_dir = NegativeStateDir("missing_root");
    auto const result = RunLoadOnlyCaptureStderr(load_only, empty_dir);
    ExpectFatalLoadOnly(result, "fatal: LoadApplication failed");
  }

  {
    auto const dir = NegativeStateDir("missing_main_window");
    CopyStateDir(good_state, dir);
    WriteProfileMissingMainWindow(dir);
    AssertMainWindowRefAbsent(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result,
                        "fatal: LoadPresentedWindow required window missing");
  }

  {
    auto const dir = NegativeStateDir("missing_presenter_ref");
    CopyStateDir(good_state, dir);
    WriteProfileMissingPresenter(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    ExpectFatalLoadOnly(result,
                        "fatal: LoadPresentedWindow required presenter missing");
  }

  {
    auto const dir = NegativeStateDir("missing_presenter_object");
    CopyStateDir(good_state, dir);
    WriteProfileMissingPresenterObject(dir);
    auto const result = RunLoadOnlyCaptureStderr(load_only, dir);
    bool const presenter_missing =
        result.stderr_text.find(
            "fatal: LoadPresentedWindow required presenter missing") !=
        std::string::npos;
    bool const load_failed =
        result.stderr_text.find("fatal: LoadApplication failed") !=
        std::string::npos;
    CHECK(result.exit_code != 0);
    CHECK(presenter_missing || load_failed);
    if (!presenter_missing && !load_failed) {
      std::cerr << "missing presenter object stderr was:\n"
                << result.stderr_text << '\n';
      std::exit(1);
    }
  }
}

void RunAcceptanceAndRequiredGraphNegatives() {
  auto const good_state = RunAcceptance();
  RunRequiredGraphNegativeTests(
      good_state, DemoExe("win32_minimal_app_load_only.exe"));
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunAcceptanceAndRequiredGraphNegatives();
  std::cout << "minimal_app_win32_acceptance_test OK\n";
  return 0;
}
