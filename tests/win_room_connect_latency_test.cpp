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
  return std::string("connect-latency-") + std::to_string(ms) + "-" +
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
  std::int64_t connect_ms{0};
  bool ok{false};
};

RunResult RunOnce(std::wstring const& exe, int run_index) {
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
  std::filesystem::create_directories(root / "inbox", ec);
  CHECK(!ec);

  auto const host_state = (root / "host" / "state").wstring();
  auto const client_state = (root / "client" / "state").wstring();
  auto const host_title = L"Latency Host " + run_id_w;
  auto const client_title = L"Latency Client " + run_id_w;
  auto const host_aether = L"aether-host-" + run_id_w;
  auto const client_aether = L"aether-client-" + run_id_w;
  auto const host_trace = (root / "trace" / "host.trace").wstring();
  auto const client_trace = (root / "trace" / "client.trace").wstring();
  auto const host_inbox = (root / "inbox" / "host").wstring();
  auto const client_inbox = (root / "inbox" / "client").wstring();
  std::filesystem::create_directories(root / "inbox" / "host", ec);
  std::filesystem::create_directories(root / "inbox" / "client", ec);

  std::string const before_link = "BEFORE_LINK_" + result.run_id;

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

  // Touch inbox path markers (test-only isolation tokens).
  {
    std::ofstream((root / "inbox" / "host" / "marker.txt").string())
        << (root / "inbox" / "host").string() << '\n';
    std::ofstream((root / "inbox" / "client" / "marker.txt").string())
        << (root / "inbox" / "client").string() << '\n';
  }

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

  // RAII: host/client Close() on any exit path from this scope.
  struct Guard {
    LiveProc& host;
    LiveProc& client;
    ~Guard() {
      // Client first, then Host (acceptance order).
      client.Close();
      host.Close();
    }
  } guard{host, client};

  DWORD const host_pid = host.pid();
  DWORD const client_pid = client.pid();
  CHECK(host_pid != 0);
  CHECK(client_pid != 0);
  CHECK(host_pid != client_pid);

  host.hwnd = FindHwndForPid(host_pid, host_title, 60.0, "host");
  client.hwnd = FindHwndForPid(client_pid, client_title, 60.0, "client");
  CHECK(host.hwnd != nullptr);
  CHECK(client.hwnd != nullptr);

  // Verify PID ownership of resolved HWND.
  DWORD hp = 0;
  DWORD cp = 0;
  GetWindowThreadProcessId(host.hwnd, &hp);
  GetWindowThreadProcessId(client.hwnd, &cp);
  CHECK(hp == host_pid);
  CHECK(cp == client_pid);

  auto host_c = CollectControls(host.hwnd);
  auto client_c = CollectControls(client.hwnd);
  CHECK(host_c.multiline != nullptr);
  CHECK(client_c.multiline != nullptr);
  CHECK(host_c.single_count >= 2);
  CHECK(client_c.single_count >= 2);

  std::string const host_uid = GetEditUtf8(host_c.single_edits[0]);
  CHECK(host_uid.find('-') != std::string::npos);

  {
    auto t = WaitTranscriptHasAll(host_c.multiline, {"HostUser joined"}, 30.0);
    CHECK(t.find("HostUser joined") != std::string::npos);
  }
  HWND host_send = FindButton(host_c, L"Send");
  CHECK(host_send != nullptr);
  SetEditUtf8(host_c.single_edits[host_c.single_count - 1], before_link);
  SendMessageW(host_send, BM_CLICK, 0, 0);
  {
    auto t = WaitTranscriptHasAll(host_c.multiline, {before_link}, 30.0);
    CHECK(t.find(before_link) != std::string::npos);
  }

  {
    auto t = GetEditUtf8(client_c.multiline);
    CHECK(t.find(before_link) == std::string::npos);
    CHECK(t.find("HostUser joined") == std::string::npos);
    CHECK(t.find("BEFORE_LINK_") == std::string::npos);
  }

  SetEditUtf8(client_c.single_edits[0], host_uid);
  HWND connect_btn = FindButton(client_c, L"Connect");
  CHECK(connect_btn != nullptr);

  auto const t0 = std::chrono::steady_clock::now();
  SendMessageW(connect_btn, BM_CLICK, 0, 0);

  auto const transcript = WaitTranscriptHasAll(
      client_c.multiline,
      {"HostUser joined", "ClientUser joined", before_link}, 30.0);
  result.connect_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - t0)
                          .count();

  std::cout << "run" << run_index << "_id=" << result.run_id << '\n';
  std::cout << "run" << run_index
            << "_connect_to_transcript_ms=" << result.connect_ms << '\n';
  std::cout << "run" << run_index << "_host_pid=" << host_pid
            << " client_pid=" << client_pid << '\n';
  std::cout << "run" << run_index << "_client_transcript=\n"
            << transcript << '\n';

  CHECK(transcript.find("HostUser joined") != std::string::npos);
  CHECK(transcript.find("ClientUser joined") != std::string::npos);
  CHECK(transcript.find(before_link) != std::string::npos);

  auto print_client_trace_timeline = [&]() {
    std::ifstream in((root / "trace" / "client.trace").string());
    if (!in) {
      std::cerr << "timeline: client.trace missing\n";
      return;
    }
    std::vector<std::pair<std::uint64_t, std::string>> events;
    std::string line;
    while (std::getline(in, line)) {
      auto sp = line.find(' ');
      if (sp == std::string::npos) {
        continue;
      }
      std::uint64_t t = 0;
      try {
        t = static_cast<std::uint64_t>(std::stoull(line.substr(0, sp)));
      } catch (...) {
        continue;
      }
      auto layer_pos = line.find("layer=");
      if (layer_pos == std::string::npos) {
        continue;
      }
      auto layer_end = line.find(' ', layer_pos + 6);
      auto layer = line.substr(layer_pos + 6,
                               layer_end == std::string::npos
                                   ? std::string::npos
                                   : layer_end - (layer_pos + 6));
      events.emplace_back(t, layer);
    }
    std::cerr << "CLIENT_STAGE_TIMELINE run_id=" << result.run_id << '\n';
    std::uint64_t prev = 0;
    for (auto const& [t, layer] : events) {
      auto delta_ms = prev == 0 ? 0 : static_cast<int>((t - prev) / 1000ULL);
      std::cerr << "  " << layer << " delta_ms=" << delta_ms << '\n';
      prev = t;
    }
  };

  std::string const after_host = "AFTER_LINK_HOST_" + result.run_id;
  std::string const after_client = "AFTER_LINK_CLIENT_" + result.run_id;
  HWND client_send = FindButton(client_c, L"Send");
  CHECK(client_send != nullptr);
  SetEditUtf8(host_c.single_edits[host_c.single_count - 1], after_host);
  SendMessageW(host_send, BM_CLICK, 0, 0);
  SetEditUtf8(client_c.single_edits[client_c.single_count - 1], after_client);
  SendMessageW(client_send, BM_CLICK, 0, 0);
  auto const host_after =
      WaitTranscriptHasAll(host_c.multiline, {after_host, after_client}, 15.0);
  auto const client_after =
      WaitTranscriptHasAll(client_c.multiline, {after_host, after_client}, 15.0);
  auto count_occ = [](std::string const& hay, std::string const& needle) {
    std::size_t n = 0;
    for (std::size_t pos = 0;
         (pos = hay.find(needle, pos)) != std::string::npos;
         pos += needle.size()) {
      ++n;
    }
    return n;
  };
  CHECK(count_occ(host_after, after_host) == 1);
  CHECK(count_occ(host_after, after_client) == 1);
  CHECK(count_occ(client_after, after_host) == 1);
  CHECK(count_occ(client_after, after_client) == 1);
  std::cout << "run" << run_index << "_after_link_ok=1\n";

  // Ordered cleanup: Client then Host; verify launched PIDs are gone.
  client.Close();
  host.Close();
  if (PidExists(host_pid) || PidExists(client_pid)) {
    std::cerr << "FAIL leftover launched pid host=" << host_pid
              << " alive=" << PidExists(host_pid) << " client=" << client_pid
              << " alive=" << PidExists(client_pid) << '\n';
    std::exit(1);
  }

  if (result.connect_ms > 600) {
    std::cerr << "CONNECT_LATENCY_OVER_BUDGET_MS=" << result.connect_ms
              << " run_id=" << result.run_id << '\n';
    print_client_trace_timeline();
    result.ok = false;
    return result;
  }

  result.ok = true;
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  std::string exe_utf8;
  int runs = 3;
  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "--exe" && i + 1 < argc) {
      exe_utf8 = argv[++i];
    } else if (std::string(argv[i]) == "--runs" && i + 1 < argc) {
      runs = std::atoi(argv[++i]);
    }
  }
  CHECK(!exe_utf8.empty());
  CHECK(runs >= 1);
  auto const exe = Utf8ToWide(exe_utf8);
  CHECK(std::filesystem::exists(std::filesystem::path(exe)));

  std::vector<std::int64_t> samples;
  samples.reserve(static_cast<std::size_t>(runs));
  int stale_window = 0;
  int leftover = 0;

  for (int i = 1; i <= runs; ++i) {
    auto const result = RunOnce(exe, i);
    samples.push_back(result.connect_ms);
    if (!result.ok) {
      std::cerr << "isolated_run_over_budget index=" << i
                << " ms=" << result.connect_ms << " run_id=" << result.run_id
                << '\n';
      // Product code must not be changed in this pass — stop after report.
      std::cout << "stale_window_selections=" << stale_window << '\n';
      std::cout << "leftover_launched_processes=" << leftover << '\n';
      std::cout << "wrong_pid_hwnd_selections=0\n";
      return 2;
    }
    // Brief settle so desktop session is clean before next UI automation run.
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
  }

  auto sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  auto const median = sorted[sorted.size() / 2];
  auto const max_v = sorted.back();

  for (int i = 0; i < runs; ++i) {
    std::cout << "run" << (i + 1) << "=" << samples[static_cast<std::size_t>(i)]
              << '\n';
  }
  std::cout << "median=" << median << '\n';
  std::cout << "max=" << max_v << '\n';
  std::cout << "stale_window_selections=" << stale_window << '\n';
  std::cout << "leftover_launched_processes=" << leftover << '\n';
  std::cout << "wrong_pid_hwnd_selections=0\n";
  std::cout << "win_room_connect_latency_test OK\n";
  return 0;
}
