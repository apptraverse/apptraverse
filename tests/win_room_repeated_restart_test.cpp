// Repeated Host/Client asymmetric restart reliability harness (real Win32 UI).
// Isolation: unique run_id, PID-bound HWND, RAII process cleanup, RUN_SERIAL.
//
// Usage:
//   apptraverse_win_room_repeated_restart_test.exe --exe <win32_single_client_chat>

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
  return std::string("repeated-restart-") + std::to_string(ms) + "-" +
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


struct CycleTiming {
  std::int64_t recovery_ms{0};
  std::string label;
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

int CountTrace(std::filesystem::path const& path, std::string const& needle) {
  std::ifstream in(path.string());
  if (!in) {
    return 0;
  }
  int n = 0;
  std::string line;
  while (std::getline(in, line)) {
    if (line.find(needle) != std::string::npos) {
      ++n;
    }
  }
  return n;
}

std::size_t CountOcc(std::string const& hay, std::string const& needle) {
  std::size_t n = 0;
  for (std::size_t pos = 0; (pos = hay.find(needle, pos)) != std::string::npos;
       pos += needle.size()) {
    ++n;
  }
  return n;
}

int RunRepeated(std::wstring const& exe) {
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
  auto const host_title = L"Repeat Host " + run_id_w;
  auto const client_title = L"Repeat Client " + run_id_w;
  auto const host_aether = L"aether-host-" + run_id_w;
  auto const client_aether = L"aether-client-" + run_id_w;

  auto make_host_args = [&](std::filesystem::path const& trace) {
    return L"--event-driven-runtime --role host --title \"" + host_title +
           L"\" --name HostUser --state-dir \"" + host_state +
           L"\" --aether-client-name \"" + host_aether + L"\" --room-trace \"" +
           trace.wstring() + L"\"";
  };
  auto make_client_args = [&](std::filesystem::path const& trace,
                              std::wstring const& host_uid_opt) {
    std::wstring args =
        L"--event-driven-runtime --role client --title \"" + client_title +
        L"\" --name ClientUser --state-dir \"" + client_state +
        L"\" --aether-client-name \"" + client_aether + L"\" --room-trace \"" +
        trace.wstring() + L"\"";
    if (!host_uid_opt.empty()) {
      args += L" --host-uid " + host_uid_opt;
    }
    return args;
  };

  auto const host_args0 = make_host_args(root / "trace" / "host0.trace");
  auto const client_args0 = make_client_args(root / "trace" / "client0.trace", L"");

  {
    auto d = StartProcess(exe, host_args0 + L" --distill", L"dh-" + run_id_w,
                          root / "logs" / "hd.out", root / "logs" / "hd.err");
    WaitDistillExit(d);
  }
  {
    auto d = StartProcess(exe, client_args0 + L" --distill", L"dc-" + run_id_w,
                          root / "logs" / "cd.out", root / "logs" / "cd.err");
    WaitDistillExit(d);
  }

  LiveProc host = StartProcessVisible(exe, host_args0, L"h0-" + run_id_w,
                                      root / "logs" / "h0.out",
                                      root / "logs" / "h0.err");
  LiveProc client = StartProcessVisible(exe, client_args0, L"c0-" + run_id_w,
                                        root / "logs" / "c0.out",
                                        root / "logs" / "c0.err");
  struct Guard {
    LiveProc* host{nullptr};
    LiveProc* client{nullptr};
    ~Guard() {
      if (client) client->Close();
      if (host) host->Close();
    }
  } guard{&host, &client};

  host.hwnd = FindHwndForPid(host.pid(), host_title, 60.0, "host");
  client.hwnd = FindHwndForPid(client.pid(), client_title, 60.0, "client");
  CHECK(host.hwnd && client.hwnd);
  auto host_c = CollectControls(host.hwnd);
  auto client_c = CollectControls(client.hwnd);
  std::string const host_uid = GetEditUtf8(host_c.single_edits[0]);
  CHECK(host_uid.find('-') != std::string::npos);
  WaitTranscriptHasAll(host_c.multiline, {"HostUser joined"}, 30.0);

  HWND host_send = FindButton(host_c, L"Send");
  HWND client_send = FindButton(client_c, L"Send");
  HWND connect_btn = FindButton(client_c, L"Connect");
  CHECK(host_send && client_send && connect_btn);
  SetEditUtf8(client_c.single_edits[0], host_uid);
  SendMessageW(connect_btn, BM_CLICK, 0, 0);
  WaitTranscriptHasAll(client_c.multiline,
                       {"HostUser joined", "ClientUser joined"}, 45.0);
  WaitTranscriptHasAll(host_c.multiline,
                       {"HostUser joined", "ClientUser joined"}, 45.0);
  std::cout << "activation_ok run_id=" << run_id << '\n';

  std::vector<CycleTiming> timings;

  for (int cycle = 1; cycle <= 3; ++cycle) {
    std::cout << "===== CYCLE " << cycle << " A host-restart =====\n";
    DWORD const old_host_pid = host.pid();
    host.Close();
    guard.host = nullptr;
    CHECK(!PidExists(old_host_pid));
    std::this_thread::sleep_for(std::chrono::milliseconds{400});

    client_c = CollectControls(client.hwnd);
    client_send = FindButton(client_c, L"Send");
    std::string const ca = "CLIENT_OFFLINE_A_" + std::to_string(cycle);
    std::string const cb = "CLIENT_OFFLINE_B_" + std::to_string(cycle);
    SetEditUtf8(client_c.single_edits[client_c.single_count - 1], ca);
    SendMessageW(client_send, BM_CLICK, 0, 0);
    SetEditUtf8(client_c.single_edits[client_c.single_count - 1], cb);
    SendMessageW(client_send, BM_CLICK, 0, 0);
    WaitTranscriptHasAll(client_c.multiline, {ca, cb}, 10.0);

    auto const htrace = root / "trace" / ("hostA" + std::to_string(cycle) + ".trace");
    auto const t0 = std::chrono::steady_clock::now();
    host = StartProcessVisible(exe, make_host_args(htrace),
                               L"hA" + std::to_wstring(cycle) + L"-" + run_id_w,
                               root / "logs" / ("hA" + std::to_string(cycle) + ".out"),
                               root / "logs" / ("hA" + std::to_string(cycle) + ".err"));
    guard.host = &host;
    host.hwnd = FindHwndForPid(host.pid(), host_title, 60.0, "hostA");
    CHECK(host.hwnd);
    host_c = CollectControls(host.hwnd);
    auto const htx = WaitTranscriptHasAll(host_c.multiline, {ca, cb}, 20.0);
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    timings.push_back(CycleTiming{ms, "host_restart_cycle_" + std::to_string(cycle)});
    std::cout << "host_restart_cycle_" << cycle << "_ms=" << ms << '\n';
    CHECK(CountOcc(htx, ca) == 1);
    CHECK(CountOcc(htx, cb) == 1);
    CHECK(!TraceContains(htrace, "HOST_LOCAL_SEND_CLICK"));
    CHECK(ms <= 2500);
    auto const flush_n = CountTrace(htrace, "SYNC_RECONNECT_FLUSH peer=");
    auto const suppressed =
        CountTrace(htrace, "SYNC_RECONNECT_FLUSH_SUPPRESSED");
    std::cout << "host_restart_cycle_" << cycle
              << "_reconnect_flush_lines=" << flush_n
              << " suppressed=" << suppressed << '\n';
    // Receiving side is Host; flush markers are on Client. Check client trace
    // growth is not required here — recovery latency is the gate.

    std::cout << "===== CYCLE " << cycle << " B client-restart =====\n";
    DWORD const old_client_pid = client.pid();
    client.Close();
    guard.client = nullptr;
    CHECK(!PidExists(old_client_pid));
    std::this_thread::sleep_for(std::chrono::milliseconds{400});

    host_c = CollectControls(host.hwnd);
    host_send = FindButton(host_c, L"Send");
    std::string const ha = "HOST_OFFLINE_A_" + std::to_string(cycle);
    std::string const hb = "HOST_OFFLINE_B_" + std::to_string(cycle);
    SetEditUtf8(host_c.single_edits[host_c.single_count - 1], ha);
    SendMessageW(host_send, BM_CLICK, 0, 0);
    SetEditUtf8(host_c.single_edits[host_c.single_count - 1], hb);
    SendMessageW(host_send, BM_CLICK, 0, 0);
    WaitTranscriptHasAll(host_c.multiline, {ha, hb}, 10.0);

    auto const ctrace =
        root / "trace" / ("clientB" + std::to_string(cycle) + ".trace");
    auto const t1 = std::chrono::steady_clock::now();
    client = StartProcessVisible(
        exe, make_client_args(ctrace, Utf8ToWide(host_uid)),
        L"cB" + std::to_wstring(cycle) + L"-" + run_id_w,
        root / "logs" / ("cB" + std::to_string(cycle) + ".out"),
        root / "logs" / ("cB" + std::to_string(cycle) + ".err"));
    guard.client = &client;
    client.hwnd = FindHwndForPid(client.pid(), client_title, 60.0, "clientB");
    CHECK(client.hwnd);
    client_c = CollectControls(client.hwnd);
    auto const ctx = WaitTranscriptHasAll(client_c.multiline, {ha, hb}, 20.0);
    auto const cms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t1)
                         .count();
    timings.push_back(
        CycleTiming{cms, "client_restart_cycle_" + std::to_string(cycle)});
    std::cout << "client_restart_cycle_" << cycle << "_ms=" << cms << '\n';
    CHECK(CountOcc(ctx, ha) == 1);
    CHECK(CountOcc(ctx, hb) == 1);
    CHECK(cms <= 2500);
    auto const cflush = CountTrace(ctrace, "SYNC_RECONNECT_FLUSH peer=");
    auto const csup =
        CountTrace(ctrace, "already_flushed_generation");
    std::cout << "client_restart_cycle_" << cycle
              << "_reconnect_flush=" << cflush
              << " suppressed_already=" << csup << '\n';
    // At most a small number of flush events (session create + possible
    // replace); must not storm.
    CHECK(cflush <= 4);
  }

  std::cout << "===== SIMULTANEOUS RESTART =====\n";
  host.Close();
  client.Close();
  guard.host = nullptr;
  guard.client = nullptr;
  std::this_thread::sleep_for(std::chrono::milliseconds{500});

  auto const hs = root / "trace" / "host_sim.trace";
  auto const cs = root / "trace" / "client_sim.trace";
  host = StartProcessVisible(exe, make_host_args(hs), L"hs-" + run_id_w,
                             root / "logs" / "hs.out", root / "logs" / "hs.err");
  client = StartProcessVisible(
      exe, make_client_args(cs, Utf8ToWide(host_uid)), L"cs-" + run_id_w,
      root / "logs" / "cs.out", root / "logs" / "cs.err");
  guard.host = &host;
  guard.client = &client;
  host.hwnd = FindHwndForPid(host.pid(), host_title, 60.0, "host_sim");
  client.hwnd = FindHwndForPid(client.pid(), client_title, 60.0, "client_sim");
  CHECK(host.hwnd && client.hwnd);
  // Auto reconnect via --host-uid on Client; wait for Active transcript.
  host_c = CollectControls(host.hwnd);
  client_c = CollectControls(client.hwnd);
  WaitTranscriptHasAll(client_c.multiline, {"HostUser joined", "ClientUser joined"},
                       45.0);
  WaitTranscriptHasAll(host_c.multiline, {"HostUser joined", "ClientUser joined"},
                       45.0);

  host_send = FindButton(host_c, L"Send");
  client_send = FindButton(client_c, L"Send");
  CHECK(host_send && client_send);
  // Wait until Client Send is enabled (Active after own Join / reconnect).
  {
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{30};
    while (std::chrono::steady_clock::now() < deadline) {
      if (IsWindowEnabled(client_send)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    CHECK(IsWindowEnabled(client_send));
  }

  SetEditUtf8(host_c.single_edits[host_c.single_count - 1], "SIM_HOST_MSG");
  SendMessageW(host_send, BM_CLICK, 0, 0);
  SetEditUtf8(client_c.single_edits[client_c.single_count - 1], "SIM_CLIENT_MSG");
  SendMessageW(client_send, BM_CLICK, 0, 0);
  auto const hsim =
      WaitTranscriptHasAll(host_c.multiline, {"SIM_HOST_MSG", "SIM_CLIENT_MSG"},
                           30.0);
  auto const csim = WaitTranscriptHasAll(
      client_c.multiline, {"SIM_HOST_MSG", "SIM_CLIENT_MSG"}, 30.0);
  CHECK(CountOcc(hsim, "SIM_HOST_MSG") == 1);
  CHECK(CountOcc(hsim, "SIM_CLIENT_MSG") == 1);
  CHECK(CountOcc(csim, "SIM_HOST_MSG") == 1);
  CHECK(CountOcc(csim, "SIM_CLIENT_MSG") == 1);
  std::cout << "simultaneous_restart_ok=1\n";
  std::cout << "stale_callback_dropped="
            << (CountTrace(hs, "P2P_SESSION_CALLBACK_STALE_DROPPED") +
                CountTrace(cs, "P2P_SESSION_CALLBACK_STALE_DROPPED"))
            << '\n';

  client.Close();
  host.Close();
  guard.client = nullptr;
  guard.host = nullptr;

  for (auto const& t : timings) {
    std::cout << t.label << "=" << t.recovery_ms << '\n';
  }
  std::cout << "win_room_repeated_restart_test OK\n";
  return 0;
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
  return RunRepeated(exe);
}
