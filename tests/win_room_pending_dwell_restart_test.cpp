// Host-restart pending delivery with a configurable offline dwell (real Win32
// UI). Reproduces the case where the Client has already consumed its one-shot
// reconnect flush triggers (offline detect, stale-peer re-offer) before the Host
// comes back, which the near-immediate restart harness never reaches.
// Isolation: unique run_id per attempt, PID-bound HWND, RAII process cleanup.
// Serialized: do not run two instances in parallel on one desktop session.
//
// Usage:
//   apptraverse_win_room_pending_dwell_restart_test.exe
//     --exe <win32_single_client_chat> [--dwell-ms 8000] [--cycles 1]
//     [--budget-ms 1000]

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
    }                                                                       \
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
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%06x", dist(rng));
  return std::string("pending-dwell-") + std::to_string(ms) + "-" + buf;
}

std::uint64_t MonotonicUs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
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

HWND FindHwndForPid(DWORD pid, std::wstring const& exact_title,
                    double timeout_s, char const* role) {
  auto const deadline =
      std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout_s);
  while (std::chrono::steady_clock::now() < deadline) {
    PidTitleCtx ctx{pid, exact_title, {}};
    EnumWindows(EnumPidTitleProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.matches.size() > 1) {
      std::cerr << "FAIL ambiguous HWND role=" << role << " pid=" << pid
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

std::wstring BuildEnvBlock(std::wstring const& instance) {
  std::wstring env_block;
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
  return env_block;
}

LiveProc StartProcessImpl(std::wstring const& exe, std::wstring const& args,
                          std::wstring const& instance,
                          std::filesystem::path const& stdout_path,
                          std::filesystem::path const& stderr_path,
                          bool visible) {
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

  auto env_block = BuildEnvBlock(instance);
  DWORD const flags = CREATE_UNICODE_ENVIRONMENT |
                      (visible ? 0u : static_cast<DWORD>(CREATE_NO_WINDOW));
  BOOL ok = CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, TRUE,
                           flags, env_block.data(), nullptr, &si, &live.pi);
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

std::size_t CountOcc(std::string const& hay, std::string const& needle) {
  std::size_t n = 0;
  for (std::size_t pos = 0; (pos = hay.find(needle, pos)) != std::string::npos;
       pos += needle.size()) {
    ++n;
  }
  return n;
}

char const* const kTimelineMarkers[] = {
    "layer=P2P_TRANSPORT",
    "layer=CHAT_TRANSPORT_SESSION_READY",
    "layer=CHAT_PEER_OFFLINE",
    "layer=CHAT_PEER_ONLINE",
    "layer=CHAT_PEER_REJOINED",
    "layer=SYNC_RECONNECT_FLUSH",
    "layer=CHAT_PENDING_FLUSH_BEGIN",
    "layer=CLIENT_EVENT_COMMITTED",
    "layer=CLIENT_SYNC_TRANSPORT_WRITE",
    "layer=SYNC_WRITE_SUPPRESSED",
    "layer=SYNC_PACKET_RETRY",
    "layer=SYNC_TRANSPORT_RECEIVE",
    "layer=SYNC_PACKET_RECEIVED",
    "layer=SYNC_ACK_RECEIVED",
    "layer=SYNC_PENDING_REMOVED",
    "layer=SYNC_EVENT_BLOCKED",
    "layer=HOST_SYNC_EVENT_APPLIED",
    "layer=PENDING_COUNT_CHANGED",
    "layer=HOST_TRANSCRIPT_APPLIED",
    "layer=HOST_PROCESS_START",
    "layer=HOST_CONNECT_REQUEST",
    "layer=CHAT_ADD_PEER_RESULT",
    "layer=ROOM_TRANSITION",
};

struct TraceLine {
  std::uint64_t us{0};
  std::string text;
};

std::vector<TraceLine> ReadTimeline(std::filesystem::path const& path,
                                    std::uint64_t from_us,
                                    char const* tag) {
  std::vector<TraceLine> out;
  std::ifstream in(path.string());
  if (!in) {
    return out;
  }
  std::string line;
  while (std::getline(in, line)) {
    bool interesting = false;
    for (auto const* marker : kTimelineMarkers) {
      if (line.find(marker) != std::string::npos) {
        interesting = true;
        break;
      }
    }
    if (!interesting) {
      continue;
    }
    std::uint64_t us = 0;
    try {
      us = static_cast<std::uint64_t>(std::stoull(line));
    } catch (...) {
      continue;
    }
    if (us < from_us) {
      continue;
    }
    out.push_back(TraceLine{us, std::string{tag} + " " + line});
  }
  return out;
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

struct CycleResult {
  int index{0};
  int dwell_ms{0};
  std::int64_t host_start_to_transcript_ms{-1};
  std::int64_t host_start_to_first_write_ms{-1};
  std::int64_t write_to_host_apply_ms{-1};
  bool duplicate{false};
};

struct Options {
  std::wstring exe;
  int dwell_ms{8000};
  // Per-cycle dwell increment; sweeps the Host restart phase across the
  // Client offline timeout and write-gate cooldown boundaries.
  int dwell_step_ms{0};
  int cycles{1};
  int budget_ms{1000};
};

int Run(Options const& options) {
  auto const run_id = MakeRunId();
  auto const run_id_w = Utf8ToWide(run_id);
  auto const root = std::filesystem::temp_directory_path() / run_id;
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "host" / "state", ec);
  std::filesystem::create_directories(root / "client" / "state", ec);
  std::filesystem::create_directories(root / "trace", ec);
  std::filesystem::create_directories(root / "logs", ec);
  CHECK(!ec);

  auto const host_state = (root / "host" / "state").wstring();
  auto const client_state = (root / "client" / "state").wstring();
  auto const host_title = L"Dwell Host " + run_id_w;
  auto const client_title = L"Dwell Client " + run_id_w;
  auto const host_aether = L"aether-host-" + run_id_w;
  auto const client_aether = L"aether-client-" + run_id_w;
  auto const client_trace_path = root / "trace" / "client.trace";

  auto host_args_for = [&](std::filesystem::path const& trace) {
    return L"--event-driven-runtime --role host --title \"" + host_title +
           L"\" --name HostUser --state-dir \"" + host_state +
           L"\" --aether-client-name \"" + host_aether + L"\" --room-trace \"" +
           trace.wstring() + L"\"";
  };
  std::wstring const client_args =
      L"--event-driven-runtime --role client --title \"" + client_title +
      L"\" --name ClientUser --state-dir \"" + client_state +
      L"\" --aether-client-name \"" + client_aether + L"\" --room-trace \"" +
      client_trace_path.wstring() + L"\"";

  auto const host_trace0 = root / "trace" / "host-0.trace";
  {
    auto distill_host = StartProcessImpl(
        options.exe, host_args_for(host_trace0) + L" --distill",
        L"distill-h-" + run_id_w, root / "logs" / "host-distill.stdout.log",
        root / "logs" / "host-distill.stderr.log", false);
    WaitDistillExit(distill_host);
  }
  {
    auto distill_client = StartProcessImpl(
        options.exe, client_args + L" --distill", L"distill-c-" + run_id_w,
        root / "logs" / "client-distill.stdout.log",
        root / "logs" / "client-distill.stderr.log", false);
    WaitDistillExit(distill_client);
  }

  LiveProc host = StartProcessImpl(options.exe, host_args_for(host_trace0),
                                   L"run-h-" + run_id_w,
                                   root / "logs" / "host.stdout.log",
                                   root / "logs" / "host.stderr.log", true);
  LiveProc client = StartProcessImpl(options.exe, client_args,
                                     L"run-c-" + run_id_w,
                                     root / "logs" / "client.stdout.log",
                                     root / "logs" / "client.stderr.log", true);
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

  DWORD const client_pid = client.pid();
  host.hwnd = FindHwndForPid(host.pid(), host_title, 60.0, "host");
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
  HWND connect_btn = FindButton(client_c, L"Connect");
  CHECK(connect_btn != nullptr);
  SetEditUtf8(client_c.single_edits[0], host_uid);
  SendMessageW(connect_btn, BM_CLICK, 0, 0);
  WaitTranscriptHasAll(client_c.multiline,
                       {"HostUser joined", "ClientUser joined"}, 45.0);
  WaitTranscriptHasAll(host_c.multiline,
                       {"HostUser joined", "ClientUser joined"}, 45.0);

  std::vector<CycleResult> results;
  bool all_ok = true;
  for (int cycle = 1; cycle <= options.cycles; ++cycle) {
    int const dwell_ms =
        options.dwell_ms + (cycle - 1) * options.dwell_step_ms;
    CycleResult cr{};
    cr.index = cycle;
    cr.dwell_ms = dwell_ms;
    std::string const offline_msg = "OFFLINE_DWELL_" + std::to_string(cycle);

    host_c = CollectControls(host.hwnd);
    client_c = CollectControls(client.hwnd);
    HWND client_send = FindButton(client_c, L"Send");
    CHECK(client_send != nullptr);

    DWORD const host_pid_before = host.pid();
    host.Close();
    guard.host = nullptr;
    CHECK(!PidExists(host_pid_before));
    std::this_thread::sleep_for(std::chrono::milliseconds{300});

    auto const t_commit_us = MonotonicUs();
    SetEditUtf8(client_c.single_edits[client_c.single_count - 1], offline_msg);
    SendMessageW(client_send, BM_CLICK, 0, 0);
    {
      auto const t = WaitTranscriptHasAll(client_c.multiline, {offline_msg}, 10.0);
      CHECK(t.find(offline_msg) != std::string::npos);
    }

    // Dwell: let the Client burn its offline-detect / stale-peer flush triggers
    // while the Host is still down.
    std::this_thread::sleep_for(std::chrono::milliseconds{dwell_ms});

    auto const host_trace = root / "trace" /
                            ("host-" + std::to_string(cycle) + ".trace");
    auto const t_start_us = MonotonicUs();
    host = StartProcessImpl(options.exe, host_args_for(host_trace),
                            L"run-h" + std::to_wstring(cycle) + L"-" + run_id_w,
                            root / "logs" /
                                ("host-" + std::to_string(cycle) + ".stdout.log"),
                            root / "logs" /
                                ("host-" + std::to_string(cycle) + ".stderr.log"),
                            true);
    guard.host = &host;
    host.hwnd = FindHwndForPid(host.pid(), host_title, 60.0, "host-restart");
    CHECK(host.hwnd != nullptr);
    host_c = CollectControls(host.hwnd);
    CHECK(host_c.multiline != nullptr);

    auto const host_tx =
        WaitTranscriptHasAll(host_c.multiline, {offline_msg}, 40.0);
    auto const t_delivered_us = MonotonicUs();
    cr.host_start_to_transcript_ms =
        static_cast<std::int64_t>((t_delivered_us - t_start_us) / 1000);
    cr.duplicate = CountOcc(host_tx, offline_msg) != 1;

    auto const write_us = TraceFirstUsAfter(
        client_trace_path, "layer=CLIENT_SYNC_TRANSPORT_WRITE", t_start_us);
    auto const applied_us =
        TraceFirstUsAfter(host_trace, "layer=HOST_SYNC_EVENT_APPLIED", 0);
    if (write_us) {
      cr.host_start_to_first_write_ms =
          static_cast<std::int64_t>((*write_us - t_start_us) / 1000);
    }
    if (write_us && applied_us && *applied_us >= *write_us) {
      cr.write_to_host_apply_ms =
          static_cast<std::int64_t>((*applied_us - *write_us) / 1000);
    }

    std::cout << "\n=== cycle " << cycle << " dwell_ms=" << dwell_ms
              << " host_start_to_transcript_ms="
              << cr.host_start_to_transcript_ms
              << " host_start_to_first_write_ms="
              << cr.host_start_to_first_write_ms
              << " write_to_host_apply_ms=" << cr.write_to_host_apply_ms
              << " duplicate=" << (cr.duplicate ? 1 : 0) << " ===\n";

    bool const over_budget = cr.host_start_to_transcript_ms < 0 ||
                             cr.host_start_to_transcript_ms > options.budget_ms;
    if (over_budget || cr.duplicate || options.cycles == 1) {
      auto timeline = ReadTimeline(client_trace_path, t_commit_us, "CLIENT");
      auto host_timeline = ReadTimeline(host_trace, 0, "HOST");
      timeline.insert(timeline.end(), host_timeline.begin(),
                      host_timeline.end());
      std::sort(timeline.begin(), timeline.end(),
                [](TraceLine const& a, TraceLine const& b) {
                  return a.us < b.us;
                });
      for (auto const& entry : timeline) {
        if (entry.us > t_delivered_us + 500000) {
          continue;
        }
        auto const rel_ms = static_cast<std::int64_t>(entry.us / 1000) -
                            static_cast<std::int64_t>(t_start_us / 1000);
        std::cout << "t_rel_ms=" << rel_ms << ' ' << entry.text << '\n';
      }
    }

    if (over_budget || cr.duplicate) {
      all_ok = false;
    }
    results.push_back(cr);
  }

  std::cout << "\n=== summary ===\n";
  for (auto const& cr : results) {
    std::cout << "cycle=" << cr.index << " dwell_ms=" << cr.dwell_ms
              << " host_start_to_transcript_ms=" << cr.host_start_to_transcript_ms
              << " host_start_to_first_write_ms="
              << cr.host_start_to_first_write_ms
              << " write_to_host_apply_ms=" << cr.write_to_host_apply_ms
              << " duplicate=" << (cr.duplicate ? 1 : 0) << '\n';
  }
  std::cout << "run_root=" << root.string() << '\n';

  client.Close();
  host.Close();
  guard.client = nullptr;
  guard.host = nullptr;
  if (!all_ok) {
    std::cerr << "PENDING_DWELL_RECOVERY_OVER_BUDGET\n";
    return 2;
  }
  std::cout << "win_room_pending_dwell_restart_test OK\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  std::string exe_utf8;
  for (int i = 1; i < argc; ++i) {
    std::string const arg = argv[i];
    if (arg == "--exe" && i + 1 < argc) {
      exe_utf8 = argv[++i];
    } else if (arg == "--dwell-ms" && i + 1 < argc) {
      options.dwell_ms = std::atoi(argv[++i]);
    } else if (arg == "--dwell-step-ms" && i + 1 < argc) {
      options.dwell_step_ms = std::atoi(argv[++i]);
    } else if (arg == "--cycles" && i + 1 < argc) {
      options.cycles = std::atoi(argv[++i]);
    } else if (arg == "--budget-ms" && i + 1 < argc) {
      options.budget_ms = std::atoi(argv[++i]);
    }
  }
  CHECK(!exe_utf8.empty());
  options.exe = Utf8ToWide(exe_utf8);
  CHECK(std::filesystem::exists(std::filesystem::path(options.exe)));
  return Run(options);
}
