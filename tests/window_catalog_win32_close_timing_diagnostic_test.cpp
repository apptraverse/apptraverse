#if !defined(_WIN32)
int main() { return 2; }
#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/windows/win_ui_frame.h"

namespace apptraverse::test {
namespace {

constexpr std::uint32_t kListControlId = 5001;
constexpr std::uint32_t kAddControlId = 5002;
constexpr int kRepeats = 5;
constexpr DWORD kSendTimeoutMs = 5000;
constexpr auto kObserveDeadline = std::chrono::seconds{30};
constexpr auto kMainExitDeadline = std::chrono::seconds{120};
constexpr auto kSavedProfileMainWait = std::chrono::seconds{90};
constexpr auto kSavedProfileChromeWait = std::chrono::seconds{45};

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

std::filesystem::path FreshDir(std::filesystem::path const& parent,
                               char const* tag) {
  auto dir = parent / tag;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::filesystem::path CopyProfile(std::filesystem::path const& parent,
                                  char const* tag,
                                  std::filesystem::path const& source) {
  auto const dest = FreshDir(parent, tag);
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

LRESULT SendMsgTimeout(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  DWORD_PTR result = 0;
  if (SendMessageTimeoutW(hwnd, msg, wparam, lparam, SMTO_NORMAL, kSendTimeoutMs,
                        &result) == 0) {
    return static_cast<LRESULT>(-1);
  }
  return static_cast<LRESULT>(result);
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

std::vector<HWND> TopLevelWindowsForPid(DWORD pid, bool visible_only) {
  std::vector<HWND> out;
  struct Ctx {
    DWORD pid;
    bool visible_only;
    std::vector<HWND>* out_hwnds;
  } ctx{pid, visible_only, &out};
  EnumWindows(
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
        DWORD p = 0;
        GetWindowThreadProcessId(hwnd, &p);
        if (p != c->pid) {
          return TRUE;
        }
        if (c->visible_only && !IsWindowVisible(hwnd)) {
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

HWND FindMainWindow(DWORD pid) {
  for (HWND h : TopLevelWindowsForPid(pid, true)) {
    if (GetDlgItem(h, static_cast<int>(kListControlId)) != nullptr) {
      return h;
    }
  }
  return nullptr;
}

int CountChildClass(HWND parent, wchar_t const* class_name) {
  struct Ctx {
    wchar_t const* class_name;
    int count;
  } ctx{class_name, 0};
  EnumChildWindows(
      parent,
      [](HWND hwnd, LPARAM lparam) -> BOOL {
        auto* c = reinterpret_cast<Ctx*>(lparam);
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

bool WaitChromeReady(HWND main) {
  return WaitUntil(std::chrono::seconds{20}, [&] {
    return CountChildClass(main, L"Button") >= 4 &&
           GetDlgItem(main, static_cast<int>(kListControlId)) != nullptr;
  });
}

HWND WaitMain(DWORD pid, std::chrono::milliseconds timeout) {
  HWND main = nullptr;
  if (!WaitUntil(timeout, [&] {
        main = FindMainWindow(pid);
        return main != nullptr;
      })) {
    return nullptr;
  }
  return main;
}

std::wstring ListBoxItem(HWND list, int index) {
  LRESULT const len = SendMsgTimeout(list, LB_GETTEXTLEN, index, 0);
  if (len == LB_ERR || len < 0) {
    return {};
  }
  std::wstring buf(static_cast<std::size_t>(len + 1), L'\0');
  if (SendMsgTimeout(list, LB_GETTEXT, index,
                     reinterpret_cast<LPARAM>(buf.data())) == LB_ERR) {
    return {};
  }
  buf.resize(static_cast<std::size_t>(len));
  return buf;
}

std::vector<HWND> ChildTopLevels(DWORD pid, HWND main) {
  std::vector<HWND> out;
  for (HWND h : TopLevelWindowsForPid(pid, true)) {
    if (h == main) {
      continue;
    }
    out.push_back(h);
  }
  return out;
}

bool ClickAdd(HWND main) {
  HWND const add = GetDlgItem(main, static_cast<int>(kAddControlId));
  if (add == nullptr) {
    return false;
  }
  return SendMsgTimeout(main, WM_COMMAND,
                        MAKEWPARAM(kAddControlId, BN_CLICKED),
                        reinterpret_cast<LPARAM>(add)) != -1;
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

struct ChildTarget {
  HWND hwnd{};
  std::wstring open_row_text;
  int list_index{-1};
  bool found{false};
};

std::optional<ChildTarget> WaitNewChildAfterAdd(DWORD pid, HWND main) {
  auto const before = ChildTopLevels(pid, main);
  if (!ClickAdd(main)) {
    return std::nullopt;
  }
  ChildTarget target{};
  bool ok = WaitUntil(std::chrono::seconds{30}, [&] {
    HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
    if (list == nullptr) {
      return false;
    }
    LRESULT const count = SendMsgTimeout(list, LB_GETCOUNT, 0, 0);
    if (count <= 0 || count == LB_ERR) {
      return false;
    }
    auto const after = ChildTopLevels(pid, main);
    if (after.size() <= before.size()) {
      return false;
    }
    for (HWND h : after) {
      if (std::find(before.begin(), before.end(), h) == before.end()) {
        target.hwnd = h;
        std::wstring const title = GetWindowTextHwnd(h);
        for (LRESULT i = 0; i < count; ++i) {
          if (ListBoxItem(list, static_cast<int>(i)) == title) {
            target.open_row_text = title;
            target.list_index = static_cast<int>(i);
            target.found = true;
            return true;
          }
        }
      }
    }
    return false;
  });
  if (!ok || !target.found) {
    return std::nullopt;
  }
  return target;
}

std::optional<ChildTarget> FindOpenChildFromList(DWORD pid, HWND main) {
  HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
  if (list == nullptr) {
    return std::nullopt;
  }
  LRESULT const count = SendMsgTimeout(list, LB_GETCOUNT, 0, 0);
  if (count <= 0 || count == LB_ERR) {
    return std::nullopt;
  }
  auto const children = ChildTopLevels(pid, main);
  for (LRESULT i = 0; i < count; ++i) {
    std::wstring const row = ListBoxItem(list, static_cast<int>(i));
    if (row.empty()) {
      continue;
    }
    for (HWND h : children) {
      if (GetWindowTextHwnd(h) == row) {
        ChildTarget target{};
        target.hwnd = h;
        target.open_row_text = row;
        target.list_index = static_cast<int>(i);
        target.found = true;
        return target;
      }
    }
  }
  return std::nullopt;
}

bool RowStillOpen(HWND main, ChildTarget const& target) {
  HWND const list = GetDlgItem(main, static_cast<int>(kListControlId));
  if (list == nullptr || target.list_index < 0) {
    return false;
  }
  return ListBoxItem(list, target.list_index) == target.open_row_text;
}

enum class ObsStatus { NotApplicable, Ok, Timeout };

struct CloseObservation {
  ObsStatus wm_close_post{ObsStatus::NotApplicable};
  int64_t wm_close_post_ms{-1};
  ObsStatus hwnd_hidden{ObsStatus::NotApplicable};
  int64_t hwnd_hidden_ms{-1};
  ObsStatus hwnd_destroyed{ObsStatus::NotApplicable};
  int64_t hwnd_destroyed_ms{-1};
  ObsStatus list_closed{ObsStatus::NotApplicable};
  int64_t list_closed_ms{-1};
};

void ObserveChildClose(HWND main, ChildTarget const& target, CloseObservation& out) {
  if (!target.found || target.hwnd == nullptr) {
    return;
  }
  auto const t0 = Clock::now();
  auto const deadline = t0 + kObserveDeadline;
  auto const t_post0 = Clock::now();
  BOOL const posted = PostMessageW(target.hwnd, WM_CLOSE, 0, 0);
  out.wm_close_post_ms = MsSince(t_post0);
  out.wm_close_post = posted ? ObsStatus::Ok : ObsStatus::Timeout;

  while (Clock::now() < deadline) {
  HWND const still = target.hwnd;
    if (out.hwnd_hidden == ObsStatus::NotApplicable && IsWindow(still) &&
        !IsWindowVisible(still)) {
      out.hwnd_hidden = ObsStatus::Ok;
      out.hwnd_hidden_ms = MsSince(t0);
    }
    if (out.hwnd_destroyed == ObsStatus::NotApplicable && !IsWindow(still)) {
      out.hwnd_destroyed = ObsStatus::Ok;
      out.hwnd_destroyed_ms = MsSince(t0);
    }
    if (out.list_closed == ObsStatus::NotApplicable && !RowStillOpen(main, target)) {
      out.list_closed = ObsStatus::Ok;
      out.list_closed_ms = MsSince(t0);
    }
    if (out.hwnd_destroyed == ObsStatus::Ok && out.list_closed == ObsStatus::Ok) {
      break;
    }
    Sleep(10);
  }
  if (out.hwnd_hidden == ObsStatus::NotApplicable &&
      out.hwnd_destroyed == ObsStatus::Ok) {
    out.hwnd_hidden = ObsStatus::NotApplicable;
    out.hwnd_hidden_ms = -1;
  } else if (out.hwnd_hidden == ObsStatus::NotApplicable) {
    out.hwnd_hidden = ObsStatus::Timeout;
    out.hwnd_hidden_ms = MsSince(t0);
  }
  if (out.hwnd_destroyed == ObsStatus::NotApplicable) {
    out.hwnd_destroyed = ObsStatus::Timeout;
    out.hwnd_destroyed_ms = MsSince(t0);
  }
  if (out.list_closed == ObsStatus::NotApplicable) {
    out.list_closed = ObsStatus::Timeout;
    out.list_closed_ms = MsSince(t0);
  }
}

struct ProcessRun {
  PROCESS_INFORMATION pi{};
  HANDLE stdout_w{};
  HANDLE stderr_w{};
  std::filesystem::path stdout_log;
  std::filesystem::path stderr_log;

  ~ProcessRun() {
    if (stdout_w != nullptr) {
      CloseHandle(stdout_w);
    }
    if (stderr_w != nullptr) {
      CloseHandle(stderr_w);
    }
    if (pi.hProcess != nullptr) {
      DWORD code = STILL_ACTIVE;
      if (GetExitCodeProcess(pi.hProcess, &code) && code == STILL_ACTIVE) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
      }
      CloseHandle(pi.hProcess);
    }
  }
};

bool StartDemo(ProcessRun& run, std::filesystem::path const& exe,
               std::filesystem::path const& state_dir,
               std::filesystem::path const& log_dir) {
  run.stdout_log = log_dir / "subprocess_stdout.txt";
  run.stderr_log = log_dir / "subprocess_stderr.txt";
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  run.stdout_w = CreateFileW(
      run.stdout_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  run.stderr_w = CreateFileW(
      run.stderr_log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (run.stdout_w == INVALID_HANDLE_VALUE || run.stderr_w == INVALID_HANDLE_VALUE) {
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
  si.hStdOutput = run.stdout_w;
  si.hStdError = run.stderr_w;
  if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, exe.parent_path().wstring().c_str(), &si, &run.pi)) {
    return false;
  }
  CloseHandle(run.pi.hThread);
  return true;
}

void BurstResizeOnDemo(HWND main) {
  SendMessageTimeoutW(main, WM_ENTERSIZEMOVE, 0, 0, SMTO_NORMAL, kSendTimeoutMs,
                      nullptr);
  for (int i = 0; i < 31; ++i) {
    std::int32_t const w = (i % 2 == 0) ? 900 : 500;
    std::int32_t const ch = (i % 2 == 0) ? 500 : 900;
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
  }
  SendMessageTimeoutW(main, WM_EXITSIZEMOVE, 0, 0, SMTO_NORMAL, kSendTimeoutMs,
                       nullptr);
}

bool WaitMainClientSizeStable(HWND main, std::chrono::milliseconds timeout) {
  std::int32_t last_w = -1;
  std::int32_t last_h = -1;
  int stable_reads = 0;
  return WaitUntil(timeout, [&] {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t cw = 0;
    std::int32_t ch = 0;
    apptraverse::ui::windows::ReadModelFrameFromHwnd(main, x, y, cw, ch);
    if (cw == last_w && ch == last_h) {
      ++stable_reads;
    } else {
      stable_reads = 0;
      last_w = cw;
      last_h = ch;
    }
    return stable_reads >= 3;
  });
}

struct MainExitObservation {
  ObsStatus exit{ObsStatus::NotApplicable};
  int64_t ms{-1};
  DWORD exit_code{0};
};

MainExitObservation CloseMainAndWaitProcess(PROCESS_INFORMATION& pi, HWND main) {
  MainExitObservation out{};
  auto const t0 = Clock::now();
  PostMessageW(main, WM_CLOSE, 0, 0);
  bool const exited = WaitUntil(kMainExitDeadline, [&] {
    return WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0;
  });
  out.ms = MsSince(t0);
  if (!exited) {
    out.exit = ObsStatus::Timeout;
    return out;
  }
  out.exit = ObsStatus::Ok;
  GetExitCodeProcess(pi.hProcess, &out.exit_code);
  return out;
}

bool ProfileReachesListUi(std::filesystem::path const& exe,
                          std::filesystem::path const& profile,
                          std::filesystem::path const& log_dir) {
  ProcessRun run;
  if (!StartDemo(run, exe, profile, log_dir)) {
    return false;
  }
  HWND const main = WaitMain(run.pi.dwProcessId, kSavedProfileMainWait);
  if (main == nullptr) {
    return false;
  }
  return WaitUntil(kSavedProfileChromeWait, [&] {
    return GetDlgItem(main, static_cast<int>(kListControlId)) != nullptr;
  });
}

std::filesystem::path BuildRuntimeSavedSeed(std::filesystem::path const& exe,
                                            std::filesystem::path const& run_root) {
  auto const seed_dir = run_root / "runtime_agent_fresh_seed";
  std::error_code ec;
  std::filesystem::remove_all(seed_dir, ec);
  std::filesystem::create_directories(seed_dir, ec);
  auto const log_dir = FreshDir(run_root, "runtime_seed_build_logs");
  ProcessRun run;
  if (!StartDemo(run, exe, seed_dir, log_dir)) {
    return {};
  }
  HWND main = WaitMain(run.pi.dwProcessId, std::chrono::seconds{45});
  if (main == nullptr || !WaitChromeReady(main)) {
    return {};
  }
  auto const child = WaitNewChildAfterAdd(run.pi.dwProcessId, main);
  if (!child.has_value()) {
    return {};
  }
  Sleep(2000);
  MainExitObservation const exit = CloseMainAndWaitProcess(run.pi, main);
  if (exit.exit != ObsStatus::Ok || exit.exit_code != 0) {
    return {};
  }
  return seed_dir;
}

std::filesystem::path ResolveSavedProfileSeed(std::filesystem::path const& exe,
                                              std::filesystem::path const& run_root) {
  auto const bundled = exe.parent_path() / "surfaces_catalog_agent_fresh_embedded";
  if (std::filesystem::exists(bundled)) {
    auto const probe_log = FreshDir(run_root, "seed_probe_bundled_logs");
    if (ProfileReachesListUi(exe, bundled, probe_log)) {
      std::cout << "SAVED_PROFILE_SEED=bundled\n";
      return bundled;
    }
    std::cout << "SAVED_PROFILE_SEED=bundled_invalid\n";
  } else {
    std::cout << "SAVED_PROFILE_SEED=bundled_missing\n";
  }
  auto const built = BuildRuntimeSavedSeed(exe, run_root);
  if (built.empty()) {
    std::cout << "SAVED_PROFILE_SEED=runtime_build_failed\n";
    return {};
  }
  std::cout << "SAVED_PROFILE_SEED=runtime_prepared\n";
  return built;
}

char const* StatusTag(ObsStatus s) {
  switch (s) {
    case ObsStatus::Ok:
      return "OK";
    case ObsStatus::Timeout:
      return "TIMEOUT";
    default:
      return "NA";
  }
}

int64_t Median(std::vector<int64_t> values) {
  if (values.empty()) {
    return -1;
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

struct ScenarioOutcome {
  bool required{true};
  bool ok{false};
  std::string label;
};

void PrintMedianLine(char const* label, std::vector<CloseObservation> const& rows) {
  std::vector<int64_t> destroy_ms;
  std::vector<int64_t> hidden_ms;
  std::vector<int64_t> list_ms;
  for (auto const& row : rows) {
    if (row.hwnd_destroyed == ObsStatus::Ok) {
      destroy_ms.push_back(row.hwnd_destroyed_ms);
    }
    if (row.hwnd_hidden == ObsStatus::Ok) {
      hidden_ms.push_back(row.hwnd_hidden_ms);
    }
    if (row.list_closed == ObsStatus::Ok) {
      list_ms.push_back(row.list_closed_ms);
    }
  }
  std::cout << label << " MEDIAN_MS hwnd_destroy=" << Median(destroy_ms)
            << " hwnd_hidden=" << Median(hidden_ms)
            << " list_closed=" << Median(list_ms) << '\n';
}

ScenarioOutcome RunColdCloseScenario(std::filesystem::path const& exe,
                                     std::filesystem::path const& run_root,
                                     char const* label, bool burst,
                                     bool wait_settle) {
  ScenarioOutcome outcome{true, false, label};
  std::vector<CloseObservation> rows;
  std::size_t obj_sys_stdout = 0;
  std::size_t obj_sys_stderr = 0;
  for (int rep = 0; rep < kRepeats; ++rep) {
    ProcessRun run;
    std::string const prof_tag = std::string(label) + "_prof_" + std::to_string(rep);
    std::string const log_tag = std::string(label) + "_logs_" + std::to_string(rep);
    auto const state_dir = FreshDir(run_root, prof_tag.c_str());
    auto const log_dir = FreshDir(run_root, log_tag.c_str());
    if (!StartDemo(run, exe, state_dir, log_dir)) {
      std::cout << label << " rep=" << rep << " START_FAILED\n";
      return outcome;
    }
    HWND main = WaitMain(run.pi.dwProcessId, std::chrono::seconds{45});
    if (main == nullptr || !WaitChromeReady(main)) {
      std::cout << label << " rep=" << rep << " MAIN_NOT_FOUND\n";
      return outcome;
    }
    auto child = WaitNewChildAfterAdd(run.pi.dwProcessId, main);
    if (!child.has_value()) {
      std::cout << label << " rep=" << rep << " TARGET_NOT_FOUND (Add)\n";
      return outcome;
    }
    if (!RowStillOpen(main, *child)) {
      std::cout << label << " rep=" << rep << " TARGET_NOT_OPEN\n";
      return outcome;
    }
    if (burst) {
      BurstResizeOnDemo(main);
      if (wait_settle) {
        WaitMainClientSizeStable(main, std::chrono::seconds{10});
      }
    }
    CloseObservation obs{};
    ObserveChildClose(main, *child, obs);
    rows.push_back(obs);
    MainExitObservation const main_exit = CloseMainAndWaitProcess(run.pi, main);
    obj_sys_stdout += CountObjSysLines(run.stdout_log);
    obj_sys_stderr += CountObjSysLines(run.stderr_log);
    std::cout << label << " rep=" << rep << " wm_close=" << StatusTag(obs.wm_close_post)
              << " hwnd_hidden=" << StatusTag(obs.hwnd_hidden) << "@"
              << obs.hwnd_hidden_ms
              << " hwnd_destroy=" << StatusTag(obs.hwnd_destroyed) << "@"
              << obs.hwnd_destroyed_ms << " list=" << StatusTag(obs.list_closed)
              << "@" << obs.list_closed_ms
              << " main_exit=" << StatusTag(main_exit.exit) << "@"
              << main_exit.ms << " code=" << main_exit.exit_code
              << " OBJ_SYS stdout=" << CountObjSysLines(run.stdout_log)
              << " stderr=" << CountObjSysLines(run.stderr_log) << '\n';
    if (main_exit.exit != ObsStatus::Ok || main_exit.exit_code != 0) {
      return outcome;
    }
    if (obs.hwnd_destroyed != ObsStatus::Ok || obs.list_closed != ObsStatus::Ok) {
      return outcome;
    }
  }
  PrintMedianLine(label, rows);
  std::cout << label << " OBJ_SYS_TOTAL stdout=" << obj_sys_stdout
            << " stderr=" << obj_sys_stderr << '\n';
  outcome.ok = true;
  return outcome;
}

ScenarioOutcome RunSavedProfileScenario(std::filesystem::path const& exe,
                                        std::filesystem::path const& run_root,
                                        std::filesystem::path const& seed,
                                        char const* label) {
  ScenarioOutcome outcome{true, false, label};
  if (!std::filesystem::exists(seed)) {
    std::cout << label << " SEED_MISSING\n";
    return outcome;
  }
  std::vector<CloseObservation> rows;
  for (int rep = 0; rep < kRepeats; ++rep) {
    auto const state_dir =
        CopyProfile(run_root,
                    (std::string(label) + "_prof_" + std::to_string(rep)).c_str(),
                    seed);
    std::string const log_tag = std::string(label) + "_logs_" + std::to_string(rep);
    auto const log_dir = FreshDir(run_root, log_tag.c_str());
    ProcessRun run;
    if (!StartDemo(run, exe, state_dir, log_dir)) {
      std::cout << label << " rep=" << rep << " START_FAILED\n";
      return outcome;
    }
    HWND main = WaitMain(run.pi.dwProcessId, kSavedProfileMainWait);
    if (main == nullptr) {
      std::cout << label << " rep=" << rep << " MAIN_NOT_FOUND\n";
      return outcome;
    }
    if (!WaitUntil(kSavedProfileChromeWait, [&] {
          return GetDlgItem(main, static_cast<int>(kListControlId)) != nullptr;
        })) {
      std::cout << label << " rep=" << rep << " MAIN_NOT_FOUND\n";
      return outcome;
    }
    auto child = FindOpenChildFromList(run.pi.dwProcessId, main);
    if (!child.has_value()) {
      child = WaitNewChildAfterAdd(run.pi.dwProcessId, main);
    }
    if (!child.has_value() || !RowStillOpen(main, *child)) {
      std::cout << label << " rep=" << rep << " TARGET_NOT_FOUND\n";
      return outcome;
    }
    CloseObservation obs{};
    ObserveChildClose(main, *child, obs);
    rows.push_back(obs);
    MainExitObservation const main_exit = CloseMainAndWaitProcess(run.pi, main);
    std::cout << label << " rep=" << rep << " hwnd_destroy="
              << StatusTag(obs.hwnd_destroyed) << "@" << obs.hwnd_destroyed_ms
              << " list=" << StatusTag(obs.list_closed) << "@" << obs.list_closed_ms
              << " main_exit=" << StatusTag(main_exit.exit) << "@"
              << main_exit.ms << " code=" << main_exit.exit_code
              << " OBJ_SYS stdout=" << CountObjSysLines(run.stdout_log)
              << " stderr=" << CountObjSysLines(run.stderr_log) << '\n';
    if (obs.hwnd_destroyed != ObsStatus::Ok || obs.list_closed != ObsStatus::Ok ||
        main_exit.exit != ObsStatus::Ok || main_exit.exit_code != 0) {
      return outcome;
    }
  }
  PrintMedianLine(label, rows);
  outcome.ok = true;
  return outcome;
}

bool RunAllScenarios(std::filesystem::path const& demo) {
  auto const run_root =
      FreshDir(std::filesystem::temp_directory_path() / "apptraverse_diag",
               "close_timing_run");
  bool all_ok = true;
  auto run = [&](ScenarioOutcome const& o) {
    if (!o.ok) {
      std::cout << "SCENARIO_FAIL " << o.label << '\n';
      all_ok = false;
    }
  };
  run(RunColdCloseScenario(demo, run_root, "COLD_ADD_CLOSE", false, false));
  run(RunColdCloseScenario(demo, run_root, "BURST_CLOSE_IMMEDIATE", true, false));
  run(RunColdCloseScenario(demo, run_root, "BURST_CLOSE_AFTER_SETTLE", true, true));
  auto const seed = ResolveSavedProfileSeed(demo, run_root);
  if (seed.empty()) {
    std::cout << "SCENARIO_FAIL SAVED_PROFILE_CLOSE\n";
    return false;
  }
  run(RunSavedProfileScenario(demo, run_root, seed, "SAVED_PROFILE_CLOSE"));
  return all_ok;
}

void PrintBuildLoggingFacts() {
#if defined(AE_NO_DEBUG_LOG)
  std::cout << "HARNESS_COMPILE_AE_NO_DEBUG_LOG=" << AE_NO_DEBUG_LOG << '\n';
#else
  std::cout << "HARNESS_COMPILE_AE_NO_DEBUG_LOG=undefined\n";
#endif
  std::ifstream cache(ExeDir() / ".." / "CMakeCache.txt");
  if (cache) {
    std::string line;
    while (std::getline(cache, line)) {
      if (line.find("AE_NO_DEBUG_LOG") != std::string::npos) {
        std::cout << "CMAKE_CACHE " << line << '\n';
      }
    }
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  auto const demo =
      apptraverse::test::ResolveDemo("win32_window_catalog_demo.exe");
  std::cout << "CLOSE_TIMING_DIAG GIT_BASE=088ea28e59610c3b95a66f65979293baed13354f\n";
  std::cout << "DEBUG_DEMO=" << demo.string() << '\n';
  apptraverse::test::PrintBuildLoggingFacts();
  bool const ok = apptraverse::test::RunAllScenarios(demo);
  if (!ok) {
    std::cout << "window_catalog_win32_close_timing_diagnostic_test FAILED\n";
    return 2;
  }
  std::cout << "window_catalog_win32_close_timing_diagnostic_test OK\n";
  return 0;
}

#endif
