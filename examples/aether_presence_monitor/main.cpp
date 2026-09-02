// Windows UI presence monitor for Æther cloud connectivity characterization.
// LOCAL: ClientConnectivityPolicy::IsLocallyOnline
// REMOTE: Client::QueryPeerReceiveSchedule (no P2P / chat / streams).

#include <chrono>
#include <cstdint>
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
#include "examples/aether_presence_monitor/presence_remote_state.h"

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

constexpr UINT kTimerPump = 1;
constexpr UINT kTimerUiRefresh = 2;
constexpr UINT kTimerAutoExit = 3;

enum ControlId : int {
  kStaticAetherSha = 1000,
  kStaticLocalUid,
  kStaticLocalStatus,
  kStaticLocalResponseAge,
  kStaticLocalPingInterval,
  kStaticLocalThreshold,
  kStaticLocalInFlight,
  kStaticLocalGrace,
  kStaticLocalReason,
  kStaticLocalLastTransition,
  kStaticRemoteUid,
  kStaticRemoteStatus,
  kStaticRemoteQuery,
  kStaticRemoteLastQuery,
  kStaticRemoteLastSuccessQuery,
  kStaticRemoteLastOnline,
  kStaticRemoteDeadline,
  kStaticRemoteLastError,
  kStaticRemoteLastTransition,
  kStaticSchedule,
  kStaticCounters,
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
  int peer_ping_ms{1000};
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
    } else if (key == L"--peer-ping-ms") {
      args.peer_ping_ms = _wtoi(next().c_str());
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
  if (args.peer_ping_ms <= 0) {
    args.peer_ping_ms = args.ping_ms;
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

char const* LocalStateName(ae::LocalConnectivityState state) {
  switch (state) {
    case ae::LocalConnectivityState::kWaitingFirstResponse:
      return "WAITING_FIRST_RESPONSE";
    case ae::LocalConnectivityState::kOnline:
      return "ONLINE";
    case ae::LocalConnectivityState::kSuspect:
      return "SUSPECT";
    case ae::LocalConnectivityState::kOffline:
      return "OFFLINE";
  }
  return "WAITING_FIRST_RESPONSE";
}

char const* LocalReasonName(ae::LocalConnectivityReason reason) {
  switch (reason) {
    case ae::LocalConnectivityReason::kNoAuthenticatedResponse:
      return "NO_AUTHENTICATED_RESPONSE";
    case ae::LocalConnectivityReason::kRecentCloudResponse:
      return "RECENT_CLOUD_RESPONSE";
    case ae::LocalConnectivityReason::kSuspectAge:
      return "SUSPECT_AGE";
    case ae::LocalConnectivityReason::kOfflineAge:
      return "OFFLINE_AGE";
    case ae::LocalConnectivityReason::kPlannedPingGrace:
      return "PLANNED_PING_GRACE";
    case ae::LocalConnectivityReason::kInFlightPingGrace:
      return "IN_FLIGHT_PING_GRACE";
  }
  return "NO_AUTHENTICATED_RESPONSE";
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

  std::optional<ae::LocalConnectivityState> local_state;
  std::string local_reason;
  std::int64_t local_last_transition_qpc{0};
  std::uint32_t local_offline_transitions{0};
  std::uint32_t local_online_transitions{0};
  std::uint64_t false_local_offline_ms{0};
  std::uint64_t false_remote_offline_ms{0};
  std::uint64_t suspect_total_ms{0};
  ae::TimePoint suspect_started{};

  presence_monitor::RemotePresenceTracker remote_tracker{ae::Duration{}};
  std::optional<presence_monitor::RemoteConnectivityState> remote_derived;
  std::int64_t remote_last_transition_qpc{0};
  std::uint32_t remote_offline_transitions{0};
  std::uint32_t remote_online_transitions{0};

  std::string remote_last_error;
  ae::TimePoint next_remote_query_at{};
  ae::Subscription remote_query_sub;

  HWND hwnd{nullptr};
  HFONT ui_font{nullptr};
};

std::int64_t DurationToMs(ae::Duration d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

template <typename Rep, typename Period>
std::int64_t ChronoToMs(std::chrono::duration<Rep, Period> d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

std::string LocalReason(ae::LocalConnectivitySnapshot const& snap) {
  return LocalReasonName(snap.reason);
}

std::wstring LocalStatusText(ae::LocalConnectivitySnapshot const& snap) {
  return Utf8ToWide(LocalStateName(snap.state));
}

ae::Duration AgeDuration(ae::TimePoint now, ae::TimePoint then) {
  if (then == ae::TimePoint{} || now <= then) {
    return ae::Duration{};
  }
  return std::chrono::duration_cast<ae::Duration>(now - then);
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

std::wstring FormatAeAge(ae::TimePoint then) {
  if (then == ae::TimePoint{}) {
    return L"-";
  }
  auto const age = AgeDuration(ae::Now(), then);
  return std::to_wstring(DurationToMs(age)) + L" ms ago";
}

void RefreshUi(MonitorState* state) {
  if (!state || !state->hwnd || !state->session.client ||
      !state->session.client->connectivity_policy().is_valid()) {
    return;
  }
  auto const snap =
      state->session.client->connectivity_policy().Load()->InspectLocalConnectivity(
          ae::Now());

  SetStaticText(state->hwnd, kStaticAetherSha,
                Utf8ToWide("Aether SHA: " + kAetherShaShort));
  SetStaticText(state->hwnd, kStaticLocalUid,
                Utf8ToWide("UID: " + state->session.local_uid));
  SetStaticText(state->hwnd, kStaticLocalStatus,
                L"Status: " + LocalStatusText(snap));
  SetStaticText(
      state->hwnd, kStaticLocalResponseAge,
      L"Any cloud response: " +
          (snap.has_any_cloud_response
               ? std::to_wstring(
                     DurationToMs(snap.age_since_last_any_cloud_response)) +
                     L" ms ago"
               : L"-"));
  SetStaticText(state->hwnd, kStaticLocalPingInterval,
                L"Ping interval: " +
                    std::to_wstring(DurationToMs(snap.ping_interval)) + L" ms");
  std::wstring threshold = L"Until ONLINE threshold: -";
  if (snap.has_any_cloud_response && snap.online_until != ae::TimePoint{} &&
      snap.now < snap.online_until) {
    threshold = L"Until ONLINE threshold: " +
                std::to_wstring(ChronoToMs(snap.online_until - snap.now)) +
                L" ms";
  }
  SetStaticText(state->hwnd, kStaticLocalThreshold, threshold);
  SetStaticText(
      state->hwnd, kStaticLocalInFlight,
      L"Planned/in-flight Ping: " +
          std::to_wstring(snap.planned_ping_count) + L"/" +
          std::to_wstring(snap.pings_in_flight));
  std::wstring grace = L"Grace deadline remaining: -";
  ae::TimePoint grace_deadline = snap.nearest_response_deadline;
  if (grace_deadline == ae::TimePoint{}) {
    grace_deadline = snap.nearest_dispatch_deadline;
  }
  if (grace_deadline != ae::TimePoint{} && snap.now < grace_deadline) {
    grace = L"Grace deadline remaining: " +
            std::to_wstring(ChronoToMs(grace_deadline - snap.now)) + L" ms";
  }
  SetStaticText(state->hwnd, kStaticLocalGrace, grace);
  SetStaticText(state->hwnd, kStaticLocalReason,
                Utf8ToWide("Reason: " + LocalReason(snap)));
  SetStaticText(state->hwnd, kStaticLocalLastTransition,
                L"Last transition: " + FormatQpcAge(state->local_last_transition_qpc));

  SetStaticText(state->hwnd, kStaticRemoteUid,
                Utf8ToWide("UID: " + state->peer_uid_text));
  auto const& remote = state->remote_tracker.snapshot();
  auto const now_tp = ae::Now();
  SetStaticText(state->hwnd, kStaticRemoteStatus,
                Utf8ToWide(std::string{"Remote derived: "} +
                           presence_monitor::RemoteStateName(remote.derived)));
  SetStaticText(state->hwnd, kStaticRemoteQuery,
                remote.query_phase == presence_monitor::QueryPhase::kInFlight
                    ? L"Query: IN_FLIGHT"
                    : L"Query: IDLE");
  SetStaticText(state->hwnd, kStaticRemoteLastQuery,
                L"Last query completed: " + FormatAeAge(remote.query_completed_at));
  SetStaticText(state->hwnd, kStaticRemoteLastSuccessQuery,
                L"Last successful query: " +
                    FormatAeAge(remote.last_successful_query_at));
  SetStaticText(state->hwnd, kStaticRemoteLastOnline,
                L"Remote last_online: " + FormatAeAge(remote.last_online));
  std::wstring deadline = L"Remote next deadline: -";
  if (remote.next_ping_deadline != ae::TimePoint{}) {
    auto const delta = remote.next_ping_deadline - now_tp;
    deadline = L"Remote next deadline: " +
               std::to_wstring(ChronoToMs(delta)) + L" ms";
  }
  SetStaticText(state->hwnd, kStaticRemoteDeadline, deadline);
  SetStaticText(state->hwnd, kStaticRemoteLastError,
                Utf8ToWide("Last query error: " +
                           (remote.last_query_error.has_value()
                                ? std::to_string(*remote.last_query_error)
                                : "-")));
  SetStaticText(state->hwnd, kStaticRemoteLastTransition,
                L"Last transition: " + FormatQpcAge(state->remote_last_transition_qpc));

  SetStaticText(state->hwnd, kStaticSchedule,
                Utf8ToWide("Ping interval: " + std::to_string(state->args.ping_ms) +
                           " ms\r\nReceive window: " +
                           std::to_string(state->args.window_ms) +
                           " ms\r\nPeer ping (configured): " +
                           std::to_string(state->args.peer_ping_ms) +
                           " ms\r\nQuery period: " +
                           std::to_string(state->args.query_period_ms) + " ms"));

  std::wstring counters =
      L"Local Offline transitions: " +
      std::to_wstring(state->local_offline_transitions) +
      L"\r\nLocal Online transitions: " +
      std::to_wstring(state->local_online_transitions) +
      L"\r\nRemote Offline transitions: " +
      std::to_wstring(state->remote_offline_transitions) +
      L"\r\nRemote Online transitions: " +
      std::to_wstring(state->remote_online_transitions);
  SetStaticText(state->hwnd, kStaticCounters, counters);
}

void UpdateLocalOnline(MonitorState* state, ae::LocalConnectivitySnapshot const& snap) {
  auto const current = snap.state;
  std::string const reason = LocalReason(snap);
  if (state->local_state.has_value() && *state->local_state == current) {
    state->local_reason = reason;
    return;
  }
  auto const qpc = MonotonicClock::NowTicks();
  if (state->local_state.has_value() && *state->local_state != current) {
    state->local_last_transition_qpc = qpc;
    if (current == ae::LocalConnectivityState::kOffline &&
        *state->local_state != ae::LocalConnectivityState::kOffline) {
      ++state->local_offline_transitions;
    }
    if ((current == ae::LocalConnectivityState::kOnline ||
         current == ae::LocalConnectivityState::kSuspect) &&
        *state->local_state == ae::LocalConnectivityState::kOffline) {
      ++state->local_online_transitions;
    }
  }
  state->local_state = current;
  state->local_reason = reason;
  state->log->Write(
      "LOCAL_STATE", state->args.label,
      {{"aether_sha", kAetherShaFull},
       {"state", LocalStateName(current)},
       {"reason", reason},
       {"has_any_cloud_response", snap.has_any_cloud_response ? "true" : "false"},
       {"any_cloud_age_ms",
        std::to_string(DurationToMs(snap.age_since_last_any_cloud_response))},
       {"ping_age_ms",
        std::to_string(DurationToMs(snap.age_since_last_ping_response))},
       {"ping_interval_ms", std::to_string(DurationToMs(snap.ping_interval))},
       {"planned_ping_count", std::to_string(snap.planned_ping_count)},
       {"pings_in_flight", std::to_string(snap.pings_in_flight)},
       {"peer_ping_ms", std::to_string(state->args.peer_ping_ms)}});
}

void UpdateRemoteDerived(MonitorState* state) {
  auto const derived = state->remote_tracker.snapshot().derived;
  if (state->remote_derived.has_value() && *state->remote_derived == derived) {
    return;
  }
  auto const qpc = MonotonicClock::NowTicks();
  if (state->remote_derived.has_value()) {
    state->remote_last_transition_qpc = qpc;
    if (derived == presence_monitor::RemoteConnectivityState::kOffline) {
      ++state->remote_offline_transitions;
    }
    if (derived == presence_monitor::RemoteConnectivityState::kOnline) {
      ++state->remote_online_transitions;
    }
  }
  state->remote_derived = derived;
  auto const& remote = state->remote_tracker.snapshot();
  state->log->Write(
      "REMOTE_STATE", state->args.label,
      {{"aether_sha", kAetherShaFull},
       {"derived", presence_monitor::RemoteStateName(derived)},
       {"raw", ScheduleStateName(remote.raw)},
       {"peer_uid", state->peer_uid_text},
       {"peer_ping_ms", std::to_string(state->args.peer_ping_ms)}});
}

bool CanStartRemoteQuery(MonitorState* state,
                         ae::LocalConnectivitySnapshot const& snap) {
  if (!state->args.remote_queries_enabled) {
    return false;
  }
  if (state->remote_tracker.snapshot().query_phase ==
      presence_monitor::QueryPhase::kInFlight) {
    return false;
  }
  if (snap.state == ae::LocalConnectivityState::kWaitingFirstResponse) {
    return false;
  }
  if (snap.planned_ping_count > 0 || snap.pings_in_flight > 0) {
    return false;
  }
  if (snap.nearest_dispatch_deadline != ae::TimePoint{} &&
      snap.now < snap.nearest_dispatch_deadline) {
    return false;
  }
  if (snap.has_any_cloud_response &&
      snap.age_since_last_any_cloud_response <
          std::chrono::duration_cast<ae::Duration>(std::chrono::milliseconds{50})) {
    return false;
  }
  return true;
}

void StartRemoteQuery(MonitorState* state) {
  if (!state->session.client || state->peer_uid_text.empty()) {
    return;
  }
  auto const query_started = ae::Now();
  state->remote_tracker.BeginQuery(query_started);
  state->log->Write("REMOTE_QUERY_SEND", state->args.label,
                    {{"peer_uid", state->peer_uid_text},
                     {"aether_sha", kAetherShaFull}});
  auto& action = state->session.client->QueryPeerReceiveSchedule(state->peer_uid);
  state->remote_query_sub = action.result_event().Subscribe(
      [state, query_started](ae::Result<ae::PeerReceiveSchedule, int> const& res) {
        auto const completed = ae::Now();
        state->next_remote_query_at =
            completed + std::chrono::milliseconds{state->args.query_period_ms};
        if (res) {
          auto const& schedule = res.value();
          presence_monitor::RemoteTimingSnapshot timing{};
          timing.last_online = schedule.last_online;
          timing.raw_state = schedule.state;
          if (schedule.next_ping_deadline.has_value()) {
            timing.next_ping_deadline = *schedule.next_ping_deadline;
          }
          state->remote_tracker.CompleteQuerySuccess(completed, timing);
          UpdateRemoteDerived(state);
          state->log->Write(
              "REMOTE_QUERY_RESULT", state->args.label,
              {{"aether_sha", kAetherShaFull},
               {"success", "true"},
               {"raw", ScheduleStateName(schedule.state)},
               {"derived",
                presence_monitor::RemoteStateName(
                    state->remote_tracker.snapshot().derived)},
               {"peer_uid", state->peer_uid_text},
               {"peer_ping_ms", std::to_string(state->args.peer_ping_ms)}});
        } else {
          state->remote_tracker.CompleteQueryError(completed, res.error());
          UpdateRemoteDerived(state);
          state->log->Write(
              "REMOTE_QUERY_RESULT", state->args.label,
              {{"aether_sha", kAetherShaFull},
               {"success", "false"},
               {"error", std::to_string(res.error())},
               {"derived", "UNKNOWN"},
               {"peer_uid", state->peer_uid_text}});
        }
      });
}

void MaybeStartRemoteQuery(MonitorState* state) {
  if (!state->session.client ||
      !state->session.client->connectivity_policy().is_valid()) {
    return;
  }
  auto const snap =
      state->session.client->connectivity_policy().Load()->InspectLocalConnectivity(
          ae::Now());
  if (state->next_remote_query_at != ae::TimePoint{} &&
      ae::Now() < state->next_remote_query_at) {
    return;
  }
  if (!CanStartRemoteQuery(state, snap)) {
    return;
  }
  StartRemoteQuery(state);
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
                 std::wstring const& text, HFONT font) {
  HWND ctrl = CreateWindowExW(
      0, L"STATIC", text.c_str(), WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
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

      state->ui_font = CreateFontW(
          16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

      int y = 8;
      CreateLabel(hwnd, kStaticAetherSha, 12, y, 460, 20, L"Aether SHA:", state->ui_font);
      y += 22;
      CreateLabel(hwnd, 0, 12, y, 460, 22, L"Local client", state->ui_font);
      y += 24;
      CreateLabel(hwnd, kStaticLocalUid, 24, y, 460, 20, L"UID:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalStatus, 24, y, 460, 20, L"Status:",
                  state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalResponseAge, 24, y, 460, 20,
                  L"Last cloud response:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalPingInterval, 24, y, 460, 20,
                  L"Ping interval:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalThreshold, 24, y, 460, 20,
                  L"Recent threshold remaining:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalInFlight, 24, y, 460, 20,
                  L"Pings in flight:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalGrace, 24, y, 460, 20,
                  L"Ping grace remaining:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalReason, 24, y, 460, 20, L"Reason:",
                  state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticLocalLastTransition, 24, y, 460, 20,
                  L"Last transition:", state->ui_font);
      y += 28;
      CreateLabel(hwnd, 0, 12, y, 460, 22, L"Remote client", state->ui_font);
      y += 24;
      CreateLabel(hwnd, kStaticRemoteUid, 24, y, 460, 20, L"UID:",
                  state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteStatus, 24, y, 460, 20, L"Remote status:",
                  state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteQuery, 24, y, 460, 20, L"Query:",
                  state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteLastQuery, 24, y, 460, 20,
                  L"Last query completed:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteLastSuccessQuery, 24, y, 460, 20,
                  L"Last successful query:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteLastOnline, 24, y, 460, 20,
                  L"Remote last_online:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteDeadline, 24, y, 460, 20,
                  L"Remote next ping deadline:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteLastError, 24, y, 460, 20,
                  L"Last query error:", state->ui_font);
      y += 20;
      CreateLabel(hwnd, kStaticRemoteLastTransition, 24, y, 460, 20,
                  L"Last transition:", state->ui_font);
      y += 28;
      CreateLabel(hwnd, kStaticSchedule, 12, y, 460, 56, L"", state->ui_font);
      y += 60;
      CreateLabel(hwnd, kStaticCounters, 12, y, 460, 80, L"", state->ui_font);

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
        if (state->ui_font) {
          DeleteObject(state->ui_font);
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
  state.remote_tracker.SetPeerPingInterval(
      std::chrono::duration_cast<ae::Duration>(
          std::chrono::milliseconds{args.peer_ping_ms}));

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

  std::wstring title = L"\u00C6ther Presence Monitor \u2014 " +
                       Utf8ToWide(args.label) + L" [" +
                       Utf8ToWide(kAetherShaShort) + L"]";
  HWND hwnd = CreateWindowExW(
      0, class_name.c_str(), title.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                            WS_MINIMIZEBOX,
      args.window_x, args.window_y, 520, 720, nullptr, nullptr,
      GetModuleHandleW(nullptr), &state);
  if (!hwnd) {
    state.log->Write("APP_STOPPED", args.label, {{"exit_code", "4"}});
    return 4;
  }
  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);

  state.next_remote_query_at = ae::TimePoint{};

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
