// Windows UI presence monitor for Æther cloud connectivity characterization.
// LOCAL: ClientConnectivityPolicy::IsLocallyOnline
// REMOTE: Client::QueryPeerReceiveSchedule (no P2P / chat / streams).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#if defined(RegisterClass)
#  undef RegisterClass
#endif
#if defined(RegisterClassW)
#  undef RegisterClassW
#endif
#if defined(RegisterClassA)
#  undef RegisterClassA
#endif

#define AE_EXAMPLE_ETHERNET 1
#include "aether/all.h"
#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/receive_schedule.h"

#include "apptraverse/directory_domain_storage.h"

#ifndef APPTRAVERSE_AETHER_SHA
#  define APPTRAVERSE_AETHER_SHA "unknown"
#endif

namespace {

using SteadyClock = std::chrono::steady_clock;

std::string const kAetherShaFull{APPTRAVERSE_AETHER_SHA};
std::string const kAetherShaShort =
    kAetherShaFull.size() >= 12 ? kAetherShaFull.substr(0, 12) : kAetherShaFull;

constexpr char const* kAetherParentUid =
    "3ac93165-3d37-4970-87a6-fa4ee27744e4";
constexpr auto kLocalPollInterval = std::chrono::milliseconds{30};
constexpr auto kRemoteQueryTimeout = std::chrono::seconds{15};

constexpr UINT kTimerPump = 1;
constexpr UINT kTimerUiRefresh = 2;
constexpr UINT kTimerAutoExit = 3;

enum ControlId : int {
  kStaticTitle = 1000,
  kStaticLocalLabel,
  kStaticLocalStatus,
  kStaticRemoteLabel,
  kStaticRemoteStatus,
};

struct MonotonicClock {
  static std::int64_t Frequency() {
    static LARGE_INTEGER freq = [] {
      LARGE_INTEGER f{};
      QueryPerformanceFrequency(&f);
      return f;
    }();
    return freq.QuadPart;
  }

  static std::int64_t NowTicks() {
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
  }
};

class JsonlLog {
 public:
  explicit JsonlLog(std::filesystem::path path) : path_{std::move(path)} {
    if (!path_.parent_path().empty()) {
      std::filesystem::create_directories(path_.parent_path());
    }
    out_.open(path_, std::ios::out | std::ios::app);
  }

  void Write(std::string const& event, std::string const& label,
             std::vector<std::pair<std::string, std::string>> const& fields =
                 {}) {
    if (!out_) {
      return;
    }
    std::string line = "{\"event\":\"" + Escape(event) + "\",\"qpc\":" +
                       std::to_string(MonotonicClock::NowTicks()) +
                       ",\"qpc_frequency\":" +
                       std::to_string(MonotonicClock::Frequency()) +
                       ",\"label\":\"" + Escape(label) + "\"";
    for (auto const& [key, value] : fields) {
      line += ",\"" + Escape(key) + "\":";
      if (value == "true" || value == "false") {
        line += value;
      } else if (!value.empty() && value[0] >= '0' && value[0] <= '9' &&
                 value.find_first_not_of("0123456789-") == std::string::npos) {
        line += value;
      } else {
        line += "\"" + Escape(value) + "\"";
      }
    }
    line += "}\n";
    out_ << line;
    out_.flush();
  }

 private:
  static std::string Escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
      if (c == '\\' || c == '"') {
        out.push_back('\\');
      }
      out.push_back(c);
    }
    return out;
  }

  std::filesystem::path path_;
  std::ofstream out_;
};

struct Args {
  bool register_only{false};
  bool monitor{false};
  bool remote_queries_enabled{true};
  std::filesystem::path state_dir;
  std::filesystem::path id_out;
  std::string peer_id;
  std::string label;
  std::filesystem::path log_path;
  std::string client_name;
  int window_x{100};
  int window_y{100};
  int ping_ms{1000};
  int window_ms{1000};
  int query_period_ms{1000};
  int auto_exit_sec{0};
};

std::wstring Utf8ToWide(std::string const& text) {
  if (text.empty()) {
    return {};
  }
  int const size =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                          nullptr, 0);
  if (size <= 0) {
    return {};
  }
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      out.data(), size);
  return out;
}

std::string WideToUtf8(std::wstring_view text) {
  if (text.empty()) {
    return {};
  }
  int const size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                       static_cast<int>(text.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                      out.data(), size, nullptr, nullptr);
  return out;
}

Args ParseArgs(int argc, wchar_t** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::wstring const key = argv[i];
    auto next = [&]() -> std::wstring {
      if (i + 1 >= argc) {
        return {};
      }
      return argv[++i];
    };
    if (key == L"--register-only") {
      args.register_only = true;
    } else if (key == L"--monitor") {
      args.monitor = true;
    } else if (key == L"--state-dir") {
      args.state_dir = WideToUtf8(next());
    } else if (key == L"--id-out") {
      args.id_out = WideToUtf8(next());
    } else if (key == L"--peer-id") {
      args.peer_id = WideToUtf8(next());
    } else if (key == L"--label") {
      args.label = WideToUtf8(next());
    } else if (key == L"--log") {
      args.log_path = WideToUtf8(next());
    } else if (key == L"--client-name") {
      args.client_name = WideToUtf8(next());
    } else if (key == L"--window-x") {
      args.window_x = _wtoi(next().c_str());
    } else if (key == L"--window-y") {
      args.window_y = _wtoi(next().c_str());
    } else if (key == L"--ping-ms") {
      args.ping_ms = _wtoi(next().c_str());
    } else if (key == L"--window-ms") {
      args.window_ms = _wtoi(next().c_str());
    } else if (key == L"--query-period-ms") {
      args.query_period_ms = _wtoi(next().c_str());
    } else if (key == L"--no-remote-queries") {
      args.remote_queries_enabled = false;
    } else if (key == L"--auto-exit-sec") {
      args.auto_exit_sec = _wtoi(next().c_str());
    }
  }
  if (args.client_name.empty() && !args.label.empty()) {
    args.client_name = "presence-monitor-" + args.label;
  }
  return args;
}

ae::AetherAppContext MakeAetherAppContext(
    std::shared_ptr<std::filesystem::path> const& state_dir_holder) {
  ae::AetherAppContext context{[state_dir_holder] {
    return std::unique_ptr<ae::IDomainStorage>{
        std::make_unique<apptraverse::DirectoryDomainStorage>(
            *state_dir_holder)};
  }};
#if AE_DISTILLATION
  context = std::move(context).AddAdapterFactory(
      [](ae::AetherAppContext const& app_context) {
        return ae::EthernetAdapter::ptr::Create(
            ae::CreateWith{app_context.domain()}.with_id(
                ae::GlobalId::kEthernetAdapter),
            app_context.aether(), app_context.poller(),
            app_context.dns_resolver());
      });
#endif
  return context;
}

std::unique_ptr<ae::AetherApp> MakeApp(std::filesystem::path const& state_dir) {
  auto state_dir_holder = std::make_shared<std::filesystem::path>(state_dir);
  std::filesystem::create_directories(state_dir);
  return ae::AetherApp::Construct(MakeAetherAppContext(state_dir_holder));
}

char const* ScheduleStateName(ae::PeerScheduleState state) {
  switch (state) {
    case ae::PeerScheduleState::kExpected:
      return "Expected";
    case ae::PeerScheduleState::kMissedDeadline:
      return "MissedDeadline";
    case ae::PeerScheduleState::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

bool RemoteOnlineFromSchedule(ae::PeerScheduleState state) {
  return state == ae::PeerScheduleState::kExpected;
}

struct AetherSession {
  std::unique_ptr<ae::AetherApp> app;
  ae::Client::ptr client;
  ae::Subscription select_sub;
  std::string local_uid;
};

bool BootstrapClient(AetherSession& session, Args const& args, JsonlLog* log) {
  std::string const& label = args.label;
  std::string const& client_name = args.client_name;
  auto parent = ae::Uid::FromString(std::string{kAetherParentUid});
  auto& select =
      session.app->aether()->SelectClient(parent, client_name.c_str());
  bool ready = false;
  session.select_sub = select.result_event().Subscribe(
      [&](ae::Result<ae::Client::ptr, int> const& res) {
        if (!res) {
          if (log) {
            log->Write("SELECT_CLIENT_FAILED", label,
                       {{"error", std::to_string(res.error())}});
          }
          session.app->Exit(1);
          return;
        }
        session.client = res.value();
        ready = true;
      });
  session.app->WaitActions(select);
  if (!session.client) {
    return false;
  }

  auto const ping = std::chrono::milliseconds{args.ping_ms};
  auto const window = std::chrono::milliseconds{args.window_ms};
  auto const schedule_ok = session.client->SetReceiveSchedule(ae::ReceiveSchedule{
      .ping_interval = std::chrono::duration_cast<ae::Duration>(ping),
      .receive_window = std::chrono::duration_cast<ae::Duration>(window),
  });
  if (!schedule_ok) {
    if (log) {
      log->Write("SET_RECEIVE_SCHEDULE_FAILED", label,
                 {{"error", std::to_string(schedule_ok.error())}});
    }
    return false;
  }
  static_cast<void>(session.client->cloud_connection());
  session.app->aether().Save();

  session.local_uid = ae::Format("{}", session.client->uid());
  if (log) {
    log->Write("AETHER_UID_READY", label, {{"uid", session.local_uid}});
  }
  return true;
}

void PumpUntil(AetherSession& session, SteadyClock::time_point until) {
  while (SteadyClock::now() < until && !session.app->IsExited()) {
    auto now = ae::Now();
    auto next = session.app->Update(now);
    session.app->WaitUntil(
        std::min(next, now + std::chrono::milliseconds{50}));
  }
}

int RunRegisterOnly(Args const& args) {
  if (args.state_dir.empty() || args.id_out.empty()) {
    MessageBoxW(nullptr, L"Missing --state-dir or --id-out", L"Presence Monitor",
                MB_ICONERROR);
    return 2;
  }
  std::string const label = args.label.empty() ? "?" : args.label;
  JsonlLog log{args.log_path.empty()
                   ? args.state_dir / "register.jsonl"
                   : args.log_path};
  log.Write("APP_STARTED", label, {{"mode", "register-only"}});

  AetherSession session;
  session.app = MakeApp(args.state_dir);
  if (!BootstrapClient(session, args, &log)) {
    log.Write("APP_STOPPED", label, {{"exit_code", "3"}});
    return 3;
  }

  std::filesystem::create_directories(args.id_out.parent_path());
  {
    std::ofstream out{args.id_out, std::ios::out | std::ios::trunc};
    out << session.local_uid;
  }

  PumpUntil(session, SteadyClock::now() + std::chrono::seconds{2});
  session.app->aether().Save();
  log.Write("APP_STOPPED", label, {{"exit_code", "0"}});
  return 0;
}

struct MonitorState {
  Args args;
  std::unique_ptr<JsonlLog> log;
  AetherSession session;
  ae::Uid peer_uid;
  std::string peer_uid_text;

  std::optional<bool> local_online;
  std::string local_reason;
  std::int64_t local_last_transition_qpc{0};
  std::uint32_t local_offline_transitions{0};
  std::uint32_t local_online_transitions{0};

  std::optional<bool> remote_result_online;
  std::optional<bool> remote_display_online;
  std::int64_t remote_last_transition_qpc{0};
  std::uint32_t remote_offline_transitions{0};
  std::uint32_t remote_online_transitions{0};

  bool remote_inflight{false};
  std::string remote_last_error;
  std::string remote_last_state;
  SteadyClock::time_point remote_query_started{};
  SteadyClock::time_point remote_last_completed{};
  SteadyClock::time_point remote_last_successful_query{};
  std::optional<std::int64_t> remote_last_online_age_ms;
  std::optional<std::int64_t> remote_next_deadline_delta_ms;
  SteadyClock::time_point next_remote_query_at{};
  ae::Subscription remote_query_sub;

  HWND hwnd{nullptr};
  HFONT label_font{nullptr};
  HFONT status_font{nullptr};
};

void EnsureConsoleAttached() {
  static bool attached = [] {
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
      FILE* stdout_file = stdout;
      FILE* stderr_file = stderr;
      freopen_s(&stdout_file, "CONOUT$", "w", stdout);
      freopen_s(&stderr_file, "CONOUT$", "w", stderr);
      return true;
    }
    return false;
  }();
  static_cast<void>(attached);
}

void RemoteConsoleLog(std::string const& label, char const* message) {
  EnsureConsoleAttached();
  std::fprintf(stderr, "[%s] %s\n", label.c_str(), message);
  std::fflush(stderr);
}

bool LocalReadyForRemoteQuery(ae::LocalConnectivitySnapshot const& snap) {
  return snap.has_success && snap.online;
}

void RefreshUi(MonitorState* state);
void MaybeStartRemoteQuery(MonitorState* state);

std::int64_t DurationToMs(ae::Duration d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

template <typename Rep, typename Period>
std::int64_t ChronoToMs(std::chrono::duration<Rep, Period> d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

std::string LocalReason(ae::LocalConnectivitySnapshot const& snap) {
  if (!snap.has_success) {
    return "NO_SUCCESSFUL_RESPONSE";
  }
  if (snap.online) {
    if (snap.in_flight_grace_active &&
        snap.ping_interval.count() > 0 &&
        snap.age_since_last_success >= snap.ping_interval) {
      return "IN_FLIGHT_PING_GRACE";
    }
    return "RECENT_CLOUD_RESPONSE";
  }
  return "LAST_RESPONSE_TOO_OLD";
}

std::wstring UserPresenceText(bool known, bool online) {
  if (!known) {
    return L"...";
  }
  return online ? L"ONLINE" : L"OFFLINE";
}

std::wstring LocalUserStatusText(ae::LocalConnectivitySnapshot const& snap) {
  if (!snap.has_success) {
    return L"...";
  }
  return UserPresenceText(true, snap.online);
}

std::wstring RemoteUserStatusText(MonitorState const* state) {
  if (!state || !state->remote_display_online.has_value()) {
    return L"...";
  }
  return UserPresenceText(true, *state->remote_display_online);
}

void SetStaticText(HWND parent, int id, std::wstring const& text) {
  if (HWND ctrl = GetDlgItem(parent, id)) {
    SetWindowTextW(ctrl, text.c_str());
  }
}

std::wstring FormatMsAgo(SteadyClock::time_point then) {
  if (then.time_since_epoch().count() == 0) {
    return L"-";
  }
  auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      SteadyClock::now() - then)
                      .count();
  return std::to_wstring(ms) + L" ms ago";
}

std::wstring FormatQpcAge(std::int64_t since_qpc) {
  if (since_qpc <= 0) {
    return L"-";
  }
  auto const freq = MonotonicClock::Frequency();
  auto const elapsed_ms =
      (MonotonicClock::NowTicks() - since_qpc) * 1000 / freq;
  return std::to_wstring(elapsed_ms) + L" ms ago";
}

void RefreshUi(MonitorState* state) {
  if (!state || !state->hwnd || !state->session.client ||
      !state->session.client->connectivity_policy().is_valid()) {
    return;
  }
  auto const snap =
      state->session.client->connectivity_policy().Load()->InspectLocalConnectivity(
          ae::Now());

  SetStaticText(state->hwnd, kStaticLocalStatus, LocalUserStatusText(snap));
  SetStaticText(state->hwnd, kStaticRemoteStatus, RemoteUserStatusText(state));
}

void UpdateLocalOnline(MonitorState* state, ae::LocalConnectivitySnapshot const& snap) {
  bool const was_ready = state->local_online.has_value() && *state->local_online;
  bool const online = snap.online;
  std::string const reason = LocalReason(snap);
  if (state->local_online.has_value() && *state->local_online == online) {
    state->local_reason = reason;
    if (!was_ready && LocalReadyForRemoteQuery(snap)) {
      MaybeStartRemoteQuery(state);
    }
    return;
  }
  auto const qpc = MonotonicClock::NowTicks();
  if (state->local_online.has_value() && *state->local_online != online) {
    state->local_last_transition_qpc = qpc;
    if (online) {
      ++state->local_online_transitions;
    } else {
      ++state->local_offline_transitions;
    }
  }
  state->local_online = online;
  state->local_reason = reason;
  std::int64_t grace_ms = 0;
  if (snap.in_flight_grace_active && snap.pending_ping_deadline != ae::TimePoint{}) {
    grace_ms = ChronoToMs(snap.pending_ping_deadline - snap.now);
  }
  state->log->Write(
      "LOCAL_STATE", state->args.label,
      {{"aether_sha", kAetherShaFull},
       {"online", online ? "true" : "false"},
       {"reason", reason},
       {"has_success", snap.has_success ? "true" : "false"},
       {"last_success_age_ms", std::to_string(DurationToMs(snap.age_since_last_success))},
       {"ping_interval_ms", std::to_string(DurationToMs(snap.ping_interval))},
       {"pings_in_flight", std::to_string(snap.pings_in_flight)},
       {"grace_remaining_ms", std::to_string(grace_ms)}});
  if (!was_ready && LocalReadyForRemoteQuery(snap)) {
    MaybeStartRemoteQuery(state);
  }
}

void UpdateRemoteResult(MonitorState* state, bool online) {
  if (!state->remote_result_online.has_value() ||
      *state->remote_result_online != online) {
    auto const qpc = MonotonicClock::NowTicks();
    if (state->remote_result_online.has_value()) {
      state->remote_last_transition_qpc = qpc;
      if (online) {
        ++state->remote_online_transitions;
      } else {
        ++state->remote_offline_transitions;
      }
    }
    state->log->Write("REMOTE_STATE", state->args.label,
                      {{"aether_sha", kAetherShaFull},
                       {"online", online ? "true" : "false"},
                       {"peer_uid", state->peer_uid_text}});
  }
  state->remote_result_online = online;
  state->remote_display_online = online;
}

void PollLocal(MonitorState* state) {
  if (!state->session.client ||
      !state->session.client->connectivity_policy().is_valid()) {
    return;
  }
  auto const snap =
      state->session.client->connectivity_policy().Load()->InspectLocalConnectivity(
          ae::Now());
  UpdateLocalOnline(state, snap);
}

void StartRemoteQuery(MonitorState* state) {
  if (!state->args.remote_queries_enabled || !state->session.client ||
      state->remote_inflight || state->peer_uid_text.empty()) {
    return;
  }
  state->remote_inflight = true;
  state->remote_last_error.clear();
  state->remote_query_started = SteadyClock::now();
  {
    std::string line = "REMOTE_QUERY_START peer=" + state->peer_uid_text;
    RemoteConsoleLog(state->args.label, line.c_str());
  }
  state->log->Write("REMOTE_QUERY_SEND", state->args.label,
                    {{"peer_uid", state->peer_uid_text},
                     {"aether_sha", kAetherShaFull}});
  auto& action = state->session.client->QueryPeerReceiveSchedule(state->peer_uid);
  state->remote_query_sub = action.result_event().Subscribe(
      [state](ae::Result<ae::PeerReceiveSchedule, int> const& res) {
        state->remote_inflight = false;
        state->remote_last_completed = SteadyClock::now();
        auto const duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     state->remote_last_completed -
                                     state->remote_query_started)
                                     .count();
        state->next_remote_query_at =
            state->remote_last_completed +
            std::chrono::milliseconds{state->args.query_period_ms};
        if (res) {
          auto const& schedule = res.value();
          auto const schedule_state = schedule.state;
          state->remote_last_state = ScheduleStateName(schedule_state);
          state->remote_last_error.clear();
          state->remote_last_successful_query = state->remote_last_completed;
          auto const now_tp = ae::Now();
          if (schedule.last_online != ae::TimePoint{}) {
            state->remote_last_online_age_ms =
                ChronoToMs(now_tp - schedule.last_online);
          } else {
            state->remote_last_online_age_ms.reset();
          }
          if (schedule.next_ping_deadline.has_value()) {
            state->remote_next_deadline_delta_ms =
                ChronoToMs(*schedule.next_ping_deadline - now_tp);
          } else {
            state->remote_next_deadline_delta_ms.reset();
          }
          bool const online = RemoteOnlineFromSchedule(schedule_state);
          {
            std::string line = std::string{"REMOTE_QUERY_COMPLETE state="} +
                               state->remote_last_state;
            RemoteConsoleLog(state->args.label, line.c_str());
          }
          state->log->Write(
              "REMOTE_QUERY_RESULT", state->args.label,
              {{"aether_sha", kAetherShaFull},
               {"success", "true"},
               {"state", state->remote_last_state},
               {"peer_uid", state->peer_uid_text},
               {"query_duration_ms", std::to_string(duration_ms)},
               {"remote_state", state->remote_last_state},
               {"remote_last_online_age_ms",
                state->remote_last_online_age_ms.has_value()
                    ? std::to_string(*state->remote_last_online_age_ms)
                    : "-1"},
               {"next_deadline_delta_ms",
                state->remote_next_deadline_delta_ms.has_value()
                    ? std::to_string(*state->remote_next_deadline_delta_ms)
                    : "-1"}});
          UpdateRemoteResult(state, online);
          RemoteConsoleLog(state->args.label,
                           online ? "REMOTE_GUI_STATUS ONLINE"
                                  : "REMOTE_GUI_STATUS OFFLINE");
        } else {
          state->remote_last_error = std::to_string(res.error());
          {
            std::string line =
                std::string{"REMOTE_QUERY_ERROR error="} + state->remote_last_error;
            RemoteConsoleLog(state->args.label, line.c_str());
          }
          state->log->Write(
              "REMOTE_QUERY_RESULT", state->args.label,
              {{"aether_sha", kAetherShaFull},
               {"success", "false"},
               {"error", state->remote_last_error},
               {"peer_uid", state->peer_uid_text},
               {"query_duration_ms", std::to_string(duration_ms)}});
        }
        RefreshUi(state);
      });
}

void MaybeStartRemoteQuery(MonitorState* state) {
  if (!state->args.remote_queries_enabled) {
    return;
  }
  if (!state->session.client ||
      !state->session.client->connectivity_policy().is_valid()) {
    return;
  }
  auto const snap =
      state->session.client->connectivity_policy().Load()->InspectLocalConnectivity(
          ae::Now());
  if (!LocalReadyForRemoteQuery(snap)) {
    return;
  }
  if (state->remote_inflight) {
    if (state->remote_query_started.time_since_epoch().count() != 0 &&
        SteadyClock::now() - state->remote_query_started > kRemoteQueryTimeout) {
      RemoteConsoleLog(state->args.label, "REMOTE_QUERY_TIMEOUT resetting in-flight query");
      state->remote_inflight = false;
      state->remote_query_sub = ae::Subscription{};
      state->next_remote_query_at = SteadyClock::now();
    } else {
      return;
    }
  }
  if (state->remote_display_online.has_value()) {
    if (state->next_remote_query_at.time_since_epoch().count() != 0 &&
        SteadyClock::now() < state->next_remote_query_at) {
      return;
    }
  }
  StartRemoteQuery(state);
}

void PumpAether(MonitorState* state) {
  if (!state->session.app || state->session.app->IsExited()) {
    return;
  }
  auto now = ae::Now();
  auto next = state->session.app->Update(now);
  state->session.app->WaitUntil(
      std::min(next, now + kLocalPollInterval));
  PollLocal(state);
  MaybeStartRemoteQuery(state);
}

HWND CreateLabel(HWND parent, int id, int x, int y, int w, int h,
                 std::wstring const& text, HFONT font, DWORD style = 0) {
  HWND ctrl = CreateWindowExW(
      0, L"STATIC", text.c_str(),
      WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
      reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), GetModuleHandleW(nullptr),
      nullptr);
  SendMessageW(ctrl, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  return ctrl;
}

MonitorState* StateFromHwnd(HWND hwnd) {
  return reinterpret_cast<MonitorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  MonitorState* state = StateFromHwnd(hwnd);
  switch (msg) {
    case WM_CREATE: {
      auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      state = reinterpret_cast<MonitorState*>(create->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
      state->hwnd = hwnd;

      state->label_font = CreateFontW(
          14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
      state->status_font = CreateFontW(
          28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

      int y = 16;
      CreateLabel(hwnd, kStaticTitle, 20, y, 260, 24,
                  L"\u00C6ther Presence", state->label_font);
      y += 36;
      CreateLabel(hwnd, kStaticLocalLabel, 20, y, 260, 20, L"This client",
                  state->label_font);
      y += 22;
      CreateLabel(hwnd, kStaticLocalStatus, 20, y, 260, 36, L"...",
                  state->status_font);
      y += 52;
      CreateLabel(hwnd, kStaticRemoteLabel, 20, y, 260, 20, L"Other client",
                  state->label_font);
      y += 22;
      CreateLabel(hwnd, kStaticRemoteStatus, 20, y, 260, 36, L"...",
                  state->status_font);

      SetTimer(hwnd, kTimerPump,
               static_cast<UINT>(kLocalPollInterval.count()), nullptr);
      SetTimer(hwnd, kTimerUiRefresh, 500, nullptr);
      if (state->args.auto_exit_sec > 0) {
        SetTimer(hwnd, kTimerAutoExit,
                 static_cast<UINT>(state->args.auto_exit_sec * 1000), nullptr);
      }
      RefreshUi(state);
      return 0;
    }
    case WM_TIMER:
      if (wparam == kTimerPump && state) {
        PumpAether(state);
      } else if (wparam == kTimerUiRefresh && state) {
        RefreshUi(state);
      } else if (wparam == kTimerAutoExit && state) {
        if (state->log) {
          state->log->Write("AUTO_EXIT", state->args.label,
                            {{"aether_sha", kAetherShaFull},
                             {"auto_exit_sec",
                              std::to_string(state->args.auto_exit_sec)}});
        }
        DestroyWindow(hwnd);
      }
      return 0;
    case WM_DESTROY:
      if (state) {
        KillTimer(hwnd, kTimerPump);
        KillTimer(hwnd, kTimerUiRefresh);
        KillTimer(hwnd, kTimerAutoExit);
        if (state->log) {
          state->log->Write("APP_STOPPED", state->args.label,
                            {{"exit_code", "0"}});
        }
        if (state->session.app) {
          state->session.app->aether().Save();
        }
        if (state->label_font) {
          DeleteObject(state->label_font);
        }
        if (state->status_font) {
          DeleteObject(state->status_font);
        }
      }
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int RunMonitor(Args const& args) {
  if (args.state_dir.empty() || args.peer_id.empty() || args.label.empty() ||
      args.log_path.empty()) {
    MessageBoxW(nullptr,
                L"Missing --state-dir, --peer-id, --label, or --log",
                L"Presence Monitor", MB_ICONERROR);
    return 2;
  }

  MonitorState state;
  state.args = args;
  state.log = std::make_unique<JsonlLog>(args.log_path);
  state.log->Write("APP_STARTED", args.label,
                   {{"mode", "monitor"},
                    {"aether_sha", kAetherShaFull},
                    {"ping_ms", std::to_string(args.ping_ms)},
                    {"window_ms", std::to_string(args.window_ms)},
                    {"query_period_ms", std::to_string(args.query_period_ms)},
                    {"remote_queries",
                     args.remote_queries_enabled ? "true" : "false"}});
  state.peer_uid_text = args.peer_id;
  state.peer_uid = ae::Uid::FromString(args.peer_id);

  state.session.app = MakeApp(args.state_dir);
  if (!BootstrapClient(state.session, args, state.log.get())) {
    state.log->Write("APP_STOPPED", args.label, {{"exit_code", "3"}});
    return 3;
  }

  std::wstring const class_name = L"AetherPresenceMonitorWnd";
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = class_name.c_str();
  RegisterClassExW(&wc);

  std::wstring title = L"\u00C6ther Presence \u2014 " + Utf8ToWide(args.label);
  HWND hwnd = CreateWindowExW(
      0, class_name.c_str(), title.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                            WS_MINIMIZEBOX,
      args.window_x, args.window_y, 320, 240, nullptr, nullptr,
      GetModuleHandleW(nullptr), &state);
  if (!hwnd) {
    state.log->Write("APP_STOPPED", args.label, {{"exit_code", "4"}});
    return 4;
  }
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  state.next_remote_query_at = SteadyClock::time_point{};

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return static_cast<int>(msg.wParam);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  Args args = ParseArgs(argc, argv);
  LocalFree(argv);

  if (args.register_only) {
    return RunRegisterOnly(args);
  }
  if (args.monitor) {
    return RunMonitor(args);
  }
  MessageBoxW(nullptr,
              L"Usage:\n"
              L"  --register-only --state-dir DIR --id-out FILE [--label A|B]\n"
              L"  --monitor --state-dir DIR --peer-id UID --label A|B --log "
              L"PATH",
              L"Presence Monitor", MB_ICONINFORMATION);
  return 1;
}
