#if !defined(_WIN32)
int main() {
  return 0;
}
#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application_ui_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_frame.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_main_native_frame_queue.h"
#include "window_catalog_main_native_frame_wiring.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"

namespace apptraverse::test {
namespace {

constexpr std::uint32_t kListControlId = 5001;
constexpr std::uint32_t kAddControlId = 5002;

using Clock = std::chrono::steady_clock;

int64_t MsSince(Clock::time_point const& t0) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0)
      .count();
}

std::filesystem::path ExeDir() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path{path}.parent_path();
}

std::filesystem::path ResolveDemo(char const* name) {
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
  if (auto p = try_path(ExeDir() / ".." / "examples" / "surfaces_demo" / "windows" /
                        name);
      !p.empty()) {
    return p;
  }
  return ExeDir() / name;
}

std::filesystem::path FreshStateDir(char const* tag) {
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_diag" / tag;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::filesystem::path CopyProfile(char const* tag,
                                  std::filesystem::path const& source) {
  auto const dest = FreshStateDir(tag);
  std::error_code ec;
  std::filesystem::copy(source, dest,
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        ec);
  return dest;
}

bool WaitUntil(std::chrono::milliseconds timeout, auto predicate) {
  auto const deadline = Clock::now() + timeout;
  while (Clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    Sleep(10);
  }
  return false;
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

std::vector<HWND> TopLevelWindowsForPid(DWORD pid) {
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

HWND FindTopLevelWithTitle(DWORD pid, std::wstring const& needle) {
  for (HWND h : TopLevelWindowsForPid(pid)) {
    if (GetWindowTextHwnd(h).find(needle) != std::wstring::npos) {
      return h;
    }
  }
  return nullptr;
}

HWND WaitMain(DWORD pid, std::chrono::milliseconds timeout) {
  HWND main = nullptr;
  if (!WaitUntil(timeout, [&] {
        for (HWND h : TopLevelWindowsForPid(pid)) {
          if (GetDlgItem(h, static_cast<int>(kListControlId)) != nullptr) {
            main = h;
            return true;
          }
        }
        return false;
      })) {
    return nullptr;
  }
  return main;
}

void ClickAdd(HWND main) {
  HWND const child = GetDlgItem(main, static_cast<int>(kAddControlId));
  SendMessageW(main, WM_COMMAND, MAKEWPARAM(kAddControlId, BN_CLICKED),
               reinterpret_cast<LPARAM>(child));
}

std::size_t CountObjSysLines(std::filesystem::path const& path) {
  std::ifstream in(path);
  if (!in) {
    return 0;
  }
  std::size_t count = 0;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find("OBJ_SYS:") != std::string::npos) {
      ++count;
    }
  }
  return count;
}

struct ProcessRun {
  PROCESS_INFORMATION pi{};
  std::filesystem::path stderr_log;

  ~ProcessRun() {
    if (pi.hProcess != nullptr) {
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 2000);
      CloseHandle(pi.hProcess);
    }
  }
};

bool StartDemo(ProcessRun& run, std::filesystem::path const& exe,
               std::filesystem::path const& state_dir) {
  run.stderr_log = state_dir / "diag_stderr.txt";
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE err_file = CreateFileW(
      run.stderr_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (err_file == INVALID_HANDLE_VALUE) {
    return false;
  }
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --state-dir \"" +
                     state_dir.wstring() + L"\"";
  std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
  cmd_buf.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = err_file;
  if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, exe.parent_path().wstring().c_str(), &si, &run.pi)) {
    CloseHandle(err_file);
    return false;
  }
  CloseHandle(run.pi.hThread);
  CloseHandle(err_file);
  return true;
}

void BurstResizeMain(HWND main, int iterations) {
  for (int i = 0; i < iterations; ++i) {
    std::int32_t const w = (i % 2 == 0) ? 900 : 500;
    std::int32_t const ch = (i % 2 == 0) ? 500 : 900;
    SendMessageW(main, WM_ENTERSIZEMOVE, 0, 0);
    int outer_w = 0;
    int outer_h = 0;
  std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t cw = 0;
    std::int32_t ch0 = 0;
    apptraverse::ui::windows::ReadModelFrameFromHwnd(main, x, y, cw, ch0);
    apptraverse::ui::windows::ClientSizeToOuter(w, ch, outer_w, outer_h);
    SetWindowPos(main, nullptr, x, y, outer_w, outer_h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  SendMessageW(main, WM_EXITSIZEMOVE, 0, 0);
}

struct ChildCloseTiming {
  int64_t hwnd_gone_ms{-1};
  int64_t list_closed_ms{-1};
};

ChildCloseTiming CloseChildWindow(DWORD pid, HWND main,
                                  std::wstring const& title) {
  ChildCloseTiming out{};
  HWND const child_hwnd = FindTopLevelWithTitle(pid, title);
  auto const t0 = Clock::now();
  if (child_hwnd != nullptr) {
    SendMessageW(child_hwnd, WM_CLOSE, 0, 0);
  }
  WaitUntil(std::chrono::seconds{30}, [&] {
    if (out.hwnd_gone_ms < 0 && child_hwnd != nullptr &&
        (!IsWindow(child_hwnd) || !IsWindowVisible(child_hwnd))) {
      out.hwnd_gone_ms = MsSince(t0);
    }
    HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
    if (out.list_closed_ms < 0 && list != nullptr) {
      LRESULT const count = SendMessageW(list, LB_GETCOUNT, 0, 0);
      for (LRESULT i = 0; i < count; ++i) {
        wchar_t buf[256]{};
        SendMessageW(list, LB_GETTEXT, i, reinterpret_cast<LPARAM>(buf));
        if (wcsstr(buf, L"(Closed)") != nullptr &&
            wcsstr(buf, title.c_str()) != nullptr) {
          out.list_closed_ms = MsSince(t0);
          return true;
        }
      }
    }
    return out.hwnd_gone_ms >= 0 && out.list_closed_ms >= 0;
  });
  if (out.list_closed_ms < 0) {
    out.list_closed_ms = MsSince(t0);
  }
  return out;
}

int64_t CloseMainAndWaitProcess(PROCESS_INFORMATION& pi, HWND main) {
  auto const t0 = Clock::now();
  SendMessageW(main, WM_CLOSE, 0, 0);
  WaitUntil(std::chrono::seconds{120}, [&] {
    return WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0;
  });
  return MsSince(t0);
}

struct Harness {
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  WindowCatalog::ptr model_catalog;
  std::unique_ptr<ApplicationUiSession> ui_session;
  std::deque<ModelObjectProxy::ModelWork> model_work;
  ModelObjectProxy model_proxy;
  WindowCatalogMainNativeFrameQueue queue;

  Harness()
      : model_proxy{[this](ModelObjectProxy::ModelWork work) {
          model_work.push_back(std::move(work));
        }} {
    EnsureObjectRegistration();
    EnsureWindowCatalogGraphRegistration();
    queue.BindPostWork([this](ModelObjectProxy::ModelWork work) {
      model_work.push_back(std::move(work));
    });
    model_catalog = BuildWindowCatalogGraph(model_domain);
    PrepareWindowCatalogForRun(*model_catalog);
    ByteSink initial;
    SerializeInitialPublication(*model_catalog, initial);
    ui_session = std::make_unique<ApplicationUiSession>(
        initial.bytes, nullptr, model_proxy,
        [this](ae::Obj& root) {
          WireWindowCatalogMainNativeFrameQueue(root, queue);
        });
  }

  void Pump() {
    while (!model_work.empty()) {
      auto work = std::move(model_work.front());
      model_work.pop_front();
      work(model_domain);
    }
    ByteSink sink;
    SerializeStructuralNodePublication(*model_catalog, sink);
    ui_session->ApplyStructural(sink.bytes);
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
};

void PrintInProcessTimings() {
  Harness short_h;
  short_h.model_catalog->AddWindow();
  short_h.Pump();
  short_h.model_catalog->persisted.list->rows.back().Load();
  WindowHolder& holder =
      *static_cast<WindowHolder*>(&*short_h.model_catalog->persisted.list->rows.back());
  ae::ObjId const child_id = holder.window.id();
  auto const t0 = Clock::now();
  short_h.model_proxy.Invoke(short_h.model_catalog->obj_id,
                             &WindowCatalog::UserCloseWindow, child_id);
  short_h.Pump();
  std::cout << "INPROC_CHILD_CLOSE_MODEL_UI_APPLY_MS=" << MsSince(t0) << '\n';
  ByteSink pub;
  auto const t_pub = Clock::now();
  SerializeStructuralNodePublication(*short_h.model_catalog, pub);
  std::cout << "INPROC_STRUCTURAL_PUBLISH_BYTES=" << pub.bytes.size()
            << " MS=" << MsSince(t_pub) << '\n';

  auto const t_snap = Clock::now();
  SaveWindowCatalogPersistentSnapshot(*short_h.model_catalog);
  std::cout << "INPROC_RAM_SNAPSHOT_MS=" << MsSince(t_snap) << '\n';
}

void RunSavedProfileScenario(std::filesystem::path const& exe,
                             std::filesystem::path const& seed_profile) {
  if (!std::filesystem::exists(seed_profile)) {
    return;
  }
  auto const copied = CopyProfile("DEBUG_SAVED_PROFILE_COPY", seed_profile);
  std::cout << "DEBUG_SAVED_PROFILE_COPY=" << copied.string() << '\n';
  ProcessRun run;
  if (!StartDemo(run, exe, copied)) {
    std::cout << "DEBUG_SAVED_PROFILE START_FAILED\n";
    return;
  }
  HWND main = WaitMain(run.pi.dwProcessId, std::chrono::seconds{45});
  if (main == nullptr) {
    std::cout << "DEBUG_SAVED_PROFILE MAIN_TIMEOUT\n";
    return;
  }
  ChildCloseTiming const child =
      CloseChildWindow(run.pi.dwProcessId, main, L"Window 1");
  std::cout << "DEBUG_SAVED_PROFILE CHILD_HWND_GONE_MS=" << child.hwnd_gone_ms
            << " CHILD_LIST_CLOSED_MS=" << child.list_closed_ms << '\n';
  std::cout << "DEBUG_SAVED_PROFILE MAIN_PROCESS_EXIT_MS="
            << CloseMainAndWaitProcess(run.pi, main)
            << " OBJ_SYS_LINES=" << CountObjSysLines(run.stderr_log) << '\n';
}

void RunSubprocessScenario(char const* label, std::filesystem::path const& exe,
                           bool burst_resize) {
  ProcessRun run;
  auto const state_dir = FreshStateDir(label);
  if (!StartDemo(run, exe, state_dir)) {
    std::cout << label << " START_FAILED\n";
    return;
  }
  HWND main = WaitMain(run.pi.dwProcessId, std::chrono::seconds{45});
  if (main == nullptr) {
    std::cout << label << " MAIN_TIMEOUT\n";
    return;
  }
  ClickAdd(main);
  WaitUntil(std::chrono::seconds{15}, [&] {
    return FindTopLevelWithTitle(run.pi.dwProcessId, L"Window 1") != nullptr;
  });
  if (burst_resize) {
    BurstResizeMain(main, 31);
    Sleep(500);
  }
  ChildCloseTiming const child = CloseChildWindow(run.pi.dwProcessId, main, L"Window 1");
  std::cout << label << " CHILD_HWND_GONE_MS=" << child.hwnd_gone_ms
            << " CHILD_LIST_CLOSED_MS=" << child.list_closed_ms << '\n';
  int64_t const exit_ms = CloseMainAndWaitProcess(run.pi, main);
  std::cout << label << " MAIN_PROCESS_EXIT_MS=" << exit_ms
            << " OBJ_SYS_LINES=" << CountObjSysLines(run.stderr_log) << '\n';
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  std::cout << "CLOSE_TIMING_DIAG GIT_BASE=a94f90c8aed524d9892adf4a54d510bd443bbc2e\n";
#if defined(AE_NO_DEBUG_LOG)
  std::cout << "TEST_COMPILE_AE_NO_DEBUG_LOG=" << AE_NO_DEBUG_LOG << '\n';
#else
  std::cout << "TEST_COMPILE_AE_NO_DEBUG_LOG=undefined\n";
#endif
  std::cout << "CMAKE_CACHE_AE_NO_DEBUG_LOG=OFF (object logging expected)\n";

  auto const debug_demo =
      apptraverse::test::ResolveDemo("win32_window_catalog_demo.exe");
  std::cout << "DEBUG_DEMO=" << debug_demo.string() << '\n';
  apptraverse::test::PrintInProcessTimings();
  apptraverse::test::RunSubprocessScenario("DEBUG_SHORT", debug_demo, false);
  apptraverse::test::RunSubprocessScenario("DEBUG_AFTER_RESIZE", debug_demo, true);

  apptraverse::test::RunSavedProfileScenario(
      debug_demo, debug_demo.parent_path() / "surfaces_catalog_agent_fresh_embedded");

  auto const build_root = debug_demo.parent_path().parent_path().parent_path().parent_path().parent_path();
  auto const min_demo = build_root / "win64-ninja-msvc-minsizerel" / "examples" /
                        "surfaces_demo" / "windows" / "win32_window_catalog_demo.exe";
  if (std::filesystem::exists(min_demo)) {
    std::cout << "MINSIZEREL_DEMO=" << min_demo.string()
              << " (observation only; binary not rebuilt this step)\n";
    apptraverse::test::RunSubprocessScenario("MINSIZEREL_SHORT", min_demo, false);
    apptraverse::test::RunSubprocessScenario("MINSIZEREL_AFTER_RESIZE", min_demo,
                                           true);
  } else {
    std::cout << "MINSIZEREL_DEMO=missing\n";
  }

  std::cout << "window_catalog_win32_close_timing_diagnostic_test OK\n";
  return 0;
}

#endif
