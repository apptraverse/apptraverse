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
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_desktop_window.h"
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

  std::filesystem::path const checkpoint_path =
      state_dir / "persistent_model_checkpoint.bin";
  std::error_code checkpoint_error;
  if (std::filesystem::exists(checkpoint_path, checkpoint_error)) {
    std::ifstream checkpoint_in(checkpoint_path,
                                std::ios::binary | std::ios::ate);
    auto const checkpoint_size =
        static_cast<std::size_t>(checkpoint_in.tellg());
    checkpoint_in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> checkpoint_bytes(checkpoint_size);
    if (checkpoint_size > 0) {
      checkpoint_in.read(reinterpret_cast<char*>(checkpoint_bytes.data()),
                         static_cast<std::streamsize>(checkpoint_size));
    }
    ByteSource checkpoint;
    checkpoint.data = checkpoint_bytes.data();
    checkpoint.size = checkpoint_bytes.size();
    checkpoint.pos = 0;
    LoadPersistentModelSnapshot(checkpoint, domain, storage, *application);
  }

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
  CHECK(!window.presenter->title.is_valid());
  CHECK(!window.presenter->locale.is_valid());
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

void RunAcceptance() {
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
  CHECK(CountChildWindows(main) == 0);
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
  CHECK(CountChildWindows(main) == 0);
  CHECK(DesktopWindowsForPid(load_pid).size() == 1);
  CHECK(FramesNear(ReadFrame(main), saved));

  CloseGracefully(main, pi);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunAcceptance();
  std::cout << "minimal_app_win32_acceptance_test OK\n";
  return 0;
}
