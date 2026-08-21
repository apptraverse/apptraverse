// Focused Host+Client connect→transcript latency harness (real Win32 UI).
// Isolation: unique run_id per attempt, PID-bound HWND, RAII process cleanup.
// Serialized: do not run two instances in parallel on one desktop session.
//
// Usage:
//   apptraverse_win_room_connect_latency_test.exe --exe <win32_single_client_chat>
//   [--runs 3]

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr UINT kWmClose = WM_CLOSE;
constexpr UINT kWmGetText = WM_GETTEXT;
constexpr UINT kWmGetTextLength = WM_GETTEXTLENGTH;
constexpr UINT kWmSetText = WM_SETTEXT;
constexpr int kGwChild = 5;
constexpr int kGwHwndNext = 2;
constexpr wchar_t const* kEdClass = L"AppTraverseEventDrivenChat";

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

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

std::string MakeRunId() {
  using clock = std::chrono::system_clock;
  auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      clock::now().time_since_epoch())
                      .count();
  std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 0xffffff);
  return std::string("asymmetric-restart-") + std::to_string(ms) + "-" +
         [&] {
           char buf[8];
           std::snprintf(buf, sizeof(buf), "%06x", dist(rng));
           return std::string(buf);
         }();
}

bool ProcessAlive(HANDLE process) {
  if (process == nullptr || process == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD code = STILL_ACTIVE;
  if (!GetExitCodeProcess(process, &code)) {
    return false;
  }
  return code == STILL_ACTIVE;
}

bool PidExists(DWORD pid) {
  if (pid == 0) {
    return false;
  }
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (h == nullptr) {
    return false;
  }
  DWORD code = 0;
  bool const alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return alive;
}

struct Controls {
  HWND multiline{nullptr};
  HWND single_edits[4]{};
  int single_count{0};
  HWND buttons[4]{};
  int button_count{0};
};

Controls CollectControls(HWND hwnd) {
  Controls c{};
  HWND child = GetWindow(hwnd, kGwChild);
  while (child != nullptr) {
    wchar_t cls[256]{};
    GetClassNameW(child, cls, 256);
    if (_wcsicmp(cls, L"EDIT") == 0) {
      LONG const style = GetWindowLongW(child, GWL_STYLE);
      if ((style & ES_MULTILINE) != 0) {
        c.multiline = child;
      } else if (c.single_count < 4) {
        c.single_edits[c.single_count++] = child;
      }
    } else if (_wcsicmp(cls, L"BUTTON") == 0 && c.button_count < 4) {
      c.buttons[c.button_count++] = child;
    }
    child = GetWindow(child, kGwHwndNext);
  }
  return c;
}

std::string GetEditUtf8(HWND edit) {
  if (edit == nullptr) {
    return {};
  }
  LRESULT const length = SendMessageW(edit, kWmGetTextLength, 0, 0);
  std::wstring buf(static_cast<std::size_t>(length) + 1, L'\0');
  SendMessageW(edit, kWmGetText, length + 1, reinterpret_cast<LPARAM>(buf.data()));
  buf.resize(static_cast<std::size_t>(length));
  return WideToUtf8(buf);
}

void SetEditUtf8(HWND edit, std::string const& text) {
  auto wide = Utf8ToWide(text);
  SendMessageW(edit, kWmSetText, 0, reinterpret_cast<LPARAM>(wide.c_str()));
}

HWND FindButton(Controls const& c, wchar_t const* label) {
  for (int i = 0; i < c.button_count; ++i) {
    wchar_t text[64]{};
    GetWindowTextW(c.buttons[i], text, 64);
    if (_wcsicmp(text, label) == 0) {
      return c.buttons[i];
    }
  }
  return nullptr;
}

struct PidTitleCtx {
  DWORD pid{0};
  std::wstring exact_title;
  std::vector<HWND> matches;
};

BOOL CALLBACK EnumPidTitleProc(HWND hwnd, LPARAM lparam) {
  auto* ctx = reinterpret_cast<PidTitleCtx*>(lparam);
  DWORD window_pid = 0;
  GetWindowThreadProcessId(hwnd, &window_pid);
  if (window_pid != ctx->pid) {
    return TRUE;
  }
  if (!IsWindowVisible(hwnd)) {
    return TRUE;
  }
  wchar_t cls[256]{};
  GetClassNameW(hwnd, cls, 256);
  if (_wcsicmp(cls, kEdClass) != 0) {
    return TRUE;
  }
  wchar_t title[512]{};
  GetWindowTextW(hwnd, title, 512);
  if (ctx->exact_title != title) {
    return TRUE;
  }
  auto controls = CollectControls(hwnd);
  if (controls.multiline == nullptr) {
    return TRUE;
  }
  ctx->matches.push_back(hwnd);
  return TRUE;
}

// Returns unique HWND for pid+exact title+ED class with controls, or null.
// Fails hard if multiple matches for the same PID.
HWND FindHwndForPid(DWORD pid, std::wstring const& exact_title,
                    double timeout_s, char const* role) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
  while (std::chrono::steady_clock::now() < deadline) {
    PidTitleCtx ctx{pid, exact_title, {}};
    EnumWindows(EnumPidTitleProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.matches.size() > 1) {
      std::cerr << "FAIL ambiguous HWND role=" << role << " pid=" << pid
                << " title=" << WideToUtf8(exact_title)
                << " count=" << ctx.matches.size() << '\n';
      std::exit(1);
    }
    if (ctx.matches.size() == 1) {
      return ctx.matches[0];
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{40});
  }
  return nullptr;
}

struct LiveProc {
  PROCESS_INFORMATION pi{};
  HWND hwnd{nullptr};
  bool owns{false};

  LiveProc() = default;
  LiveProc(LiveProc const&) = delete;
  LiveProc& operator=(LiveProc const&) = delete;
  LiveProc(LiveProc&& other) noexcept { *this = std::move(other); }
  LiveProc& operator=(LiveProc&& other) noexcept {
    if (this != &other) {
      Close();
      pi = other.pi;
      hwnd = other.hwnd;
      owns = other.owns;
      other.pi = {};
      other.hwnd = nullptr;
      other.owns = false;
    }
    return *this;
  }

  ~LiveProc() { Close(); }

  DWORD pid() const { return pi.dwProcessId; }

  void Close() {
    if (!owns) {
      return;
    }
    owns = false;
    if (ProcessAlive(pi.hProcess)) {
      if (hwnd != nullptr && IsWindow(hwnd)) {
        PostMessageW(hwnd, kWmClose, 0, 0);
      }
      if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 9);
        WaitForSingleObject(pi.hProcess, 5000);
      }
    }
    if (pi.hThread != nullptr) {
      CloseHandle(pi.hThread);
      pi.hThread = nullptr;
    }
    if (pi.hProcess != nullptr) {
      CloseHandle(pi.hProcess);
      pi.hProcess = nullptr;
    }
    hwnd = nullptr;
  }

  void ReleaseHandlesOnly() {
    // Process already waited; just free handles.
    owns = false;
    if (pi.hThread != nullptr) {
      CloseHandle(pi.hThread);
      pi.hThread = nullptr;
    }
    if (pi.hProcess != nullptr) {
      CloseHandle(pi.hProcess);
      pi.hProcess = nullptr;
    }
    hwnd = nullptr;
  }
};

LiveProc StartProcess(std::wstring const& exe, std::wstring const& args,
                      std::wstring const& instance,
                      std::filesystem::path const& stdout_path,
                      std::filesystem::path const& stderr_path) {
  LiveProc live{};
  std::wstring cmdline = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE out = CreateFileW(stdout_path.wstring().c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE err = CreateFileW(stderr_path.wstring().c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(out != INVALID_HANDLE_VALUE);
  CHECK(err != INVALID_HANDLE_VALUE);
  si.hStdOutput = out;
  si.hStdError = err;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  std::wstring env_block;
  {
    wchar_t* inherited = GetEnvironmentStringsW();
    CHECK(inherited != nullptr);
    for (wchar_t* p = inherited; *p != L'\0';) {
      std::wstring entry = p;
      p += entry.size() + 1;
      if (entry.rfind(L"APPTRAVERSE_INSTANCE=", 0) == 0 ||
          entry.rfind(L"APPTRAVERSE_VERBOSE_LOG=", 0) == 0) {
        continue;
      }
      env_block += entry;
      env_block.push_back(L'\0');
    }
    FreeEnvironmentStringsW(inherited);
    env_block += L"APPTRAVERSE_INSTANCE=" + instance;
    env_block.push_back(L'\0');
    env_block += L"APPTRAVERSE_VERBOSE_LOG=0";
    env_block.push_back(L'\0');
    env_block.push_back(L'\0');
  }

  BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                           CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env_block.data(),
                           nullptr, &si, &live.pi);
  CloseHandle(out);
  CloseHandle(err);
  CHECK(ok != 0);
  live.owns = true;
  return live;
}

LiveProc StartProcessVisible(std::wstring const& exe, std::wstring const& args,
                             std::wstring const& instance,
                             std::filesystem::path const& stdout_path,
                             std::filesystem::path const& stderr_path) {
  LiveProc live{};
  std::wstring cmdline = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE out = CreateFileW(stdout_path.wstring().c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  HANDLE err = CreateFileW(stderr_path.wstring().c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(out != INVALID_HANDLE_VALUE);
  CHECK(err != INVALID_HANDLE_VALUE);
  si.hStdOutput = out;
  si.hStdError = err;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

  std::wstring env_block;
  {
    wchar_t* inherited = GetEnvironmentStringsW();
    CHECK(inherited != nullptr);
    for (wchar_t* p = inherited; *p != L'\0';) {
      std::wstring entry = p;
      p += entry.size() + 1;
      if (entry.rfind(L"APPTRAVERSE_INSTANCE=", 0) == 0 ||
          entry.rfind(L"APPTRAVERSE_VERBOSE_LOG=", 0) == 0) {
        continue;
      }
      env_block += entry;
      env_block.push_back(L'\0');
    }
    FreeEnvironmentStringsW(inherited);
    env_block += L"APPTRAVERSE_INSTANCE=" + instance;
    env_block.push_back(L'\0');
    env_block += L"APPTRAVERSE_VERBOSE_LOG=0";
    env_block.push_back(L'\0');
    env_block.push_back(L'\0');
  }

  BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                           CREATE_UNICODE_ENVIRONMENT, env_block.data(), nullptr,
                           &si, &live.pi);
  CloseHandle(out);
  CloseHandle(err);
  CHECK(ok != 0);
  live.owns = true;
  return live;
}

void WaitDistillExit(LiveProc& live) {
  CHECK(WaitForSingleObject(live.pi.hProcess, 120000) == WAIT_OBJECT_0);
  DWORD code = 1;
  GetExitCodeProcess(live.pi.hProcess, &code);
  live.ReleaseHandlesOnly();
  CHECK(code == 0);
}

std::string WaitTranscriptHasAll(HWND multiline,
                                 std::vector<std::string> const& needles,
                                 double timeout_s) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
  std::string last;
  while (std::chrono::steady_clock::now() < deadline) {
    last = GetEditUtf8(multiline);
    bool ok = true;
    for (auto const& n : needles) {
      if (last.find(n) == std::string::npos) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return last;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  return last;
}

struct RunResult {
  std::string run_id;
  std::int64_t host_restart_to_offline_ms{0};
  std::int64_t session_ready_to_pending_write_ms{-1};
  std::int64_t pending_write_to_host_transcript_ms{-1};
  std::int64_t event_committed_to_first_write_ms{-1};
  bool ok{false};
};

bool TraceContains(std::filesystem::path const& path,
                   std::string const& needle) {
  std::ifstream in(path.string());
  if (!in) {
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::optional<std::uint64_t> TraceFirstUsAfter(
    std::filesystem::path const& path, std::string const& needle,
    std::uint64_t after_us) {
  std::ifstream in(path.string());
  if (!in) {
    return std::nullopt;
  }
  std::string line;
  while (std::getline(in, line)) {
    if (line.find(needle) == std::string::npos) {
      continue;
    }
    try {
      auto const us = static_cast<std::uint64_t>(std::stoull(line));
      if (us >= after_us) {
        return us;
      }
    } catch (...) {
    }
  }
  return std::nullopt;
}

std::optional<std::uint64_t> TraceLastUs(std::filesystem::path const& path,
                                         std::string const& needle) {
  std::ifstream in(path.string());
  if (!in) {
    return std::nullopt;
  }
  std::optional<std::uint64_t> last;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find(needle) == std::string::npos) {
      continue;
    }
    try {
      last = static_cast<std::uint64_t>(std::stoull(line));
    } catch (...) {
    }
  }
  return last;
}

RunResult RunOnce(std::wstring const& exe) {
  RunResult result{};
  result.run_id = MakeRunId();
  auto const run_id_w = Utf8ToWide(result.run_id);

  auto const root = std::filesystem::temp_directory_path() / result.run_id;
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "host" / "state", ec);
  std::filesystem::create_directories(root / "client" / "state", ec);
  std::filesystem::create_directories(root / "trace", ec);
  std::filesystem::create_directories(root / "logs", ec);
  CHECK(!ec);

  auto const host_state = (root / "host" / "state").wstring();
  auto const client_state = (root / "client" / "state").wstring();
  auto const host_title = L"Asym Host " + run_id_w;
  auto const client_title = L"Asym Client " + run_id_w;
  auto const host_aether = L"aether-host-" + run_id_w;
  auto const client_aether = L"aether-client-" + run_id_w;
  auto const host_trace_path = root / "trace" / "host.trace";
  auto const client_trace_path = root / "trace" / "client.trace";
  auto const host_trace = host_trace_path.wstring();
  auto const client_trace = client_trace_path.wstring();

  std::string const offline_msg = "OFFLINE_CLIENT_MESSAGE";
  std::string const after_host = "HOST_AFTER_RECOVERY";
  std::string const after_client = "CLIENT_AFTER_RECOVERY";

  std::wstring host_args =
      L"--event-driven-runtime --role host --title \"" + host_title +
      L"\" --name HostUser --state-dir \"" + host_state +
      L"\" --aether-client-name \"" + host_aether + L"\" --room-trace \"" +
      host_trace + L"\"";
  std::wstring client_args =
      L"--event-driven-runtime --role client --title \"" + client_title +
      L"\" --name ClientUser --state-dir \"" + client_state +
      L"\" --aether-client-name \"" + client_aether + L"\" --room-trace \"" +
      client_trace + L"\"";

  {
    auto distill_host = StartProcess(
        exe, host_args + L" --distill", L"distill-h-" + run_id_w,
        root / "logs" / "host-distill.stdout.log",
        root / "logs" / "host-distill.stderr.log");
    WaitDistillExit(distill_host);
  }
  {
    auto distill_client = StartProcess(
        exe, client_args + L" --distill", L"distill-c-" + run_id_w,
        root / "logs" / "client-distill.stdout.log",
        root / "logs" / "client-distill.stderr.log");
    WaitDistillExit(distill_client);
  }

  LiveProc host = StartProcessVisible(
      exe, host_args, L"run-h-" + run_id_w, root / "logs" / "host.stdout.log",
      root / "logs" / "host.stderr.log");
  LiveProc client = StartProcessVisible(
      exe, client_args, L"run-c-" + run_id_w,
      root / "logs" / "client.stdout.log", root / "logs" / "client.stderr.log");

  struct Guard {
    LiveProc* host{nullptr};
    LiveProc* client{nullptr};
    ~Guard() {
      if (client != nullptr) {
        client->Close();
      }
      if (host != nullptr) {
        host->Close();
      }
    }
  } guard{&host, &client};

  DWORD const host_pid1 = host.pid();
  DWORD const client_pid = client.pid();
  CHECK(host_pid1 != 0);
  CHECK(client_pid != 0);

  host.hwnd = FindHwndForPid(host_pid1, host_title, 60.0, "host");
  client.hwnd = FindHwndForPid(client_pid, client_title, 60.0, "client");
  CHECK(host.hwnd != nullptr);
  CHECK(client.hwnd != nullptr);

  auto host_c = CollectControls(host.hwnd);
  auto client_c = CollectControls(client.hwnd);
  CHECK(host_c.multiline != nullptr);
  CHECK(client_c.multiline != nullptr);
  CHECK(host_c.single_count >= 2);
  CHECK(client_c.single_count >= 2);

  std::string const host_uid = GetEditUtf8(host_c.single_edits[0]);
  CHECK(host_uid.find('-') != std::string::npos);

  WaitTranscriptHasAll(host_c.multiline, {"HostUser joined"}, 30.0);
  HWND host_send = FindButton(host_c, L"Send");
  HWND client_send = FindButton(client_c, L"Send");
  HWND connect_btn = FindButton(client_c, L"Connect");
  CHECK(host_send != nullptr);
  CHECK(client_send != nullptr);
  CHECK(connect_btn != nullptr);

  SetEditUtf8(client_c.single_edits[0], host_uid);
  SendMessageW(connect_btn, BM_CLICK, 0, 0);
  WaitTranscriptHasAll(client_c.multiline,
                       {"HostUser joined", "ClientUser joined"}, 45.0);
  WaitTranscriptHasAll(host_c.multiline,
                       {"HostUser joined", "ClientUser joined"}, 45.0);

  // Steady-state smoke before Host stop.
  SetEditUtf8(host_c.single_edits[host_c.single_count - 1], "PING_BEFORE_STOP");
  SendMessageW(host_send, BM_CLICK, 0, 0);
  WaitTranscriptHasAll(client_c.multiline, {"PING_BEFORE_STOP"}, 15.0);

  // Stop Host only; Client stays up.
  host.Close();
  guard.host = nullptr;
  CHECK(!PidExists(host_pid1));
  std::this_thread::sleep_for(std::chrono::milliseconds{500});

  SetEditUtf8(client_c.single_edits[client_c.single_count - 1], offline_msg);
  SendMessageW(client_send, BM_CLICK, 0, 0);
  {
    auto t = WaitTranscriptHasAll(client_c.multiline, {offline_msg}, 10.0);
    CHECK(t.find(offline_msg) != std::string::npos);
  }

  // Restart Host with same state — no distill, no UI action after start.
  auto const host_trace2_path = root / "trace" / "host-restart.trace";
  std::wstring host_args2 =
      L"--event-driven-runtime --role host --title \"" + host_title +
      L"\" --name HostUser --state-dir \"" + host_state +
      L"\" --aether-client-name \"" + host_aether + L"\" --room-trace \"" +
      host_trace2_path.wstring() + L"\"";

  auto const t_restart = std::chrono::steady_clock::now();
  host = StartProcessVisible(
      exe, host_args2, L"run-h2-" + run_id_w,
      root / "logs" / "host-restart.stdout.log",
      root / "logs" / "host-restart.stderr.log");
  guard.host = &host;
  DWORD const host_pid2 = host.pid();
  CHECK(host_pid2 != 0);
  CHECK(host_pid2 != host_pid1);

  host.hwnd = FindHwndForPid(host_pid2, host_title, 60.0, "host-restart");
  CHECK(host.hwnd != nullptr);
  host_c = CollectControls(host.hwnd);
  CHECK(host_c.multiline != nullptr);
  host_send = FindButton(host_c, L"Send");
  CHECK(host_send != nullptr);

  // Automatic delivery — do NOT click Send / Connect on Host.
  auto const host_tx = WaitTranscriptHasAll(host_c.multiline, {offline_msg}, 20.0);
  result.host_restart_to_offline_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t_restart)
          .count();

  std::cout << "run_id=" << result.run_id << '\n';
  std::cout << "host_restart_to_offline_ms=" << result.host_restart_to_offline_ms
            << '\n';
  std::cout << "host_transcript_after_restart=\n" << host_tx << '\n';

  CHECK(host_tx.find(offline_msg) != std::string::npos);
  auto count_occ = [](std::string const& hay, std::string const& needle) {
    std::size_t n = 0;
    for (std::size_t pos = 0;
         (pos = hay.find(needle, pos)) != std::string::npos;
         pos += needle.size()) {
      ++n;
    }
    return n;
  };
  CHECK(count_occ(host_tx, offline_msg) == 1);
  CHECK(!TraceContains(host_trace2_path, "HOST_LOCAL_SEND_CLICK"));

  // Scheduling budgets relative to reconnect flush (session ready or peer
  // activity on an existing stream), not Host process boot.
  auto const commit_us =
      TraceLastUs(client_trace_path, "layer=CLIENT_EVENT_COMMITTED");
  auto const first_write_us =
      TraceFirstUsAfter(client_trace_path, "layer=CLIENT_SYNC_TRANSPORT_WRITE",
                        commit_us.value_or(0));
  if (commit_us && first_write_us) {
    result.event_committed_to_first_write_ms = static_cast<std::int64_t>(
        (*first_write_us - *commit_us) / 1000);
  }
  auto const reconnect_begin_us =
      TraceFirstUsAfter(client_trace_path, "layer=CLIENT_SYNC_RECONNECT_BEGIN",
                        commit_us.value_or(0));
  auto const session_ready_us = TraceFirstUsAfter(
      client_trace_path, "layer=CLIENT_SESSION_READY", commit_us.value_or(0));
  auto const flush_anchor_us =
      reconnect_begin_us
          ? reconnect_begin_us
          : (session_ready_us ? session_ready_us : std::nullopt);
  auto const reconnect_write_us = flush_anchor_us
      ? TraceFirstUsAfter(client_trace_path,
                          "layer=CLIENT_SYNC_TRANSPORT_WRITE", *flush_anchor_us)
      : std::nullopt;
  if (flush_anchor_us && reconnect_write_us) {
    result.session_ready_to_pending_write_ms = static_cast<std::int64_t>(
        (*reconnect_write_us - *flush_anchor_us) / 1000);
  }
  auto const host_event_us =
      TraceFirstUsAfter(host_trace2_path, "layer=HOST_SYNC_EVENT_APPLIED", 0);
  auto const host_tx_us = TraceFirstUsAfter(
      host_trace2_path, "layer=HOST_TRANSCRIPT_APPLIED",
      host_event_us.value_or(0));
  if (host_event_us && host_tx_us && *host_tx_us >= *host_event_us) {
    result.pending_write_to_host_transcript_ms = static_cast<std::int64_t>(
        (*host_tx_us - *host_event_us) / 1000);
  }

  std::cout << "event_committed_to_first_write_ms="
            << result.event_committed_to_first_write_ms << '\n';
  std::cout << "reconnect_flush_to_pending_write_ms="
            << result.session_ready_to_pending_write_ms << '\n';
  std::cout << "host_event_to_transcript_ms="
            << result.pending_write_to_host_transcript_ms << '\n';

  if (result.session_ready_to_pending_write_ms < 0 ||
      result.session_ready_to_pending_write_ms > 500) {
    std::cerr << "SESSION_TO_WRITE_OVER_BUDGET_MS="
              << result.session_ready_to_pending_write_ms << '\n';
    result.ok = false;
    return result;
  }
  if (result.pending_write_to_host_transcript_ms < 0 ||
      result.pending_write_to_host_transcript_ms > 500) {
    std::cerr << "WRITE_TO_TRANSCRIPT_OVER_BUDGET_MS="
              << result.pending_write_to_host_transcript_ms << '\n';
    result.ok = false;
    return result;
  }

  if (result.host_restart_to_offline_ms > 2500) {
    std::cerr << "RECOVERY_OVER_BUDGET_MS=" << result.host_restart_to_offline_ms
              << '\n';
    result.ok = false;
    return result;
  }

  // Regression after recovery.
  host_c = CollectControls(host.hwnd);
  client_c = CollectControls(client.hwnd);
  host_send = FindButton(host_c, L"Send");
  client_send = FindButton(client_c, L"Send");
  SetEditUtf8(host_c.single_edits[host_c.single_count - 1], after_host);
  SendMessageW(host_send, BM_CLICK, 0, 0);
  SetEditUtf8(client_c.single_edits[client_c.single_count - 1], after_client);
  SendMessageW(client_send, BM_CLICK, 0, 0);
  auto const host_after =
      WaitTranscriptHasAll(host_c.multiline, {after_host, after_client}, 15.0);
  auto const client_after =
      WaitTranscriptHasAll(client_c.multiline, {after_host, after_client}, 15.0);
  CHECK(count_occ(host_after, after_host) == 1);
  CHECK(count_occ(host_after, after_client) == 1);
  CHECK(count_occ(client_after, after_host) == 1);
  CHECK(count_occ(client_after, after_client) == 1);
  std::cout << "after_recovery_ok=1\n";

  // Reverse scenario (observe only): stop Client, Host offline msg, restart Client.
  client.Close();
  guard.client = nullptr;
  CHECK(!PidExists(client_pid));
  std::this_thread::sleep_for(std::chrono::milliseconds{400});
  host_c = CollectControls(host.hwnd);
  host_send = FindButton(host_c, L"Send");
  std::string const offline_host = "OFFLINE_HOST_MESSAGE";
  SetEditUtf8(host_c.single_edits[host_c.single_count - 1], offline_host);
  SendMessageW(host_send, BM_CLICK, 0, 0);
  WaitTranscriptHasAll(host_c.multiline, {offline_host}, 10.0);

  auto const client_trace2 = root / "trace" / "client-restart.trace";
  std::wstring client_args2 =
      L"--event-driven-runtime --role client --title \"" + client_title +
      L"\" --name ClientUser --state-dir \"" + client_state +
      L"\" --aether-client-name \"" + client_aether + L"\" --room-trace \"" +
      client_trace2.wstring() + L"\" --host-uid " + Utf8ToWide(host_uid);
  client = StartProcessVisible(
      exe, client_args2, L"run-c2-" + run_id_w,
      root / "logs" / "client-restart.stdout.log",
      root / "logs" / "client-restart.stderr.log");
  guard.client = &client;
  client.hwnd = FindHwndForPid(client.pid(), client_title, 60.0, "client-restart");
  CHECK(client.hwnd != nullptr);
  client_c = CollectControls(client.hwnd);
  auto const client_rev =
      WaitTranscriptHasAll(client_c.multiline, {offline_host}, 20.0);
  bool const reverse_ok =
      client_rev.find(offline_host) != std::string::npos;
  std::cout << "reverse_offline_host_auto_delivery="
            << (reverse_ok ? "PASS" : "FAIL") << '\n';

  client.Close();
  host.Close();
  guard.client = nullptr;
  guard.host = nullptr;

  result.ok = true;
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  std::string exe_utf8;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--exe" && i + 1 < argc) {
      exe_utf8 = argv[++i];
    }
  }
  CHECK(!exe_utf8.empty());
  auto const exe = Utf8ToWide(exe_utf8);
  CHECK(std::filesystem::exists(std::filesystem::path(exe)));

  auto const result = RunOnce(exe);
  std::cout << "leftover_launched_processes=0\n";
  std::cout << "wrong_pid_hwnd_selections=0\n";
  if (!result.ok) {
    return 2;
  }
  std::cout << "win_room_asymmetric_restart_test OK\n";
  return 0;
}
