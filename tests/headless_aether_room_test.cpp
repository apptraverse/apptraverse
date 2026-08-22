// Headless Host + Client over the REAL Aether P2P transport, with no Win32 UI,
// no windows and no subprocesses. Both sides live in one process, each with its
// own AetherApp, Aether client identity, state directory and room state.
//
// Purpose: run the exact scenarios already covered by the in-memory transport
// test (headless_room_memory_transport_test) against real Aethernet, so the two
// numbers can be compared marker by marker. Anything present here but absent
// there is transport / session lifecycle, not chat business logic.
//
// Usage:
//   apptraverse_headless_aether_room_test.exe [--dwell-ms 9000] [--cycles 1]
//     [--budget-ms 1000] [--verbose]

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "aether/all.h"

#include "apptraverse/directory_domain_storage.h"

#include "aether_p2p_transport.h"
#include "aether_runtime.h"
#include "headless_room_runtime.h"

namespace apptraverse::testing {
namespace {

using apptraverse::examples::AetherP2pTransport;
using apptraverse::examples::ConstructAetherAppWithEthernet;
using apptraverse::examples::FormatAetherUid;
using apptraverse::examples::SelectPersistentAetherClient;

// ---------------------------------------------------------------------------
// Aether-backed transport endpoint
// ---------------------------------------------------------------------------
//
// One endpoint per side. It owns the AetherApp for that side and adapts
// AetherP2pTransport to IHeadlessTransport, so HeadlessRoomRuntime runs
// unchanged over real cloud P2P.
class AetherEndpoint : public IHeadlessTransport {
 public:
  AetherEndpoint(HeadlessTrace& trace, std::string side)
      : trace_{trace}, side_{std::move(side)} {}

  ~AetherEndpoint() override { Shutdown(); }

  // Brings up AetherApp and resolves the persistent client identity. Returns
  // false when the cloud refuses the client (offline runs).
  bool Init(std::filesystem::path const& aether_root,
            std::string const& client_name) {
    std::filesystem::create_directories(aether_root);
    trace_.Event(side_, "AETHER_APP_START");
    auto runtime = ConstructAetherAppWithEthernet([aether_root]() {
      return std::make_unique<DirectoryDomainStorage>(aether_root);
    });
    app_ = std::move(runtime.app);
    if (!app_) {
      return false;
    }
    trace_.Event(side_, "AETHER_APP_READY");
    client_ = SelectPersistentAetherClient(*app_, client_name);
    if (!client_) {
      return false;
    }
    uid_ = client_->uid();
    trace_.Event(side_, "AETHER_CLIENT_SELECTED", FormatAetherUid(uid_));
    // A new process cannot keep the previous lifetime's ping appointment.
    // Reset before anyone touches cloud_connection() so PingCloudServers
    // starts with an immediate slot instead of AETHER_OWN_PING_MISSED.
    {
      auto policy = client_->connectivity_policy();
      policy.Load();
      if (policy.is_loaded()) {
        policy->ResetRxTimings();
        trace_.Event(side_, "AETHER_RX_TIMINGS_RESET");
      }
    }

    transport_ = std::make_unique<AetherP2pTransport>();
    transport_->SetLogHandler([this](std::string line) { OnTransportLog(line); });
    transport_->SetSessionReadyHandler(
        [this](ae::Uid const& peer, char const*, std::uint64_t generation) {
          if (runtime_ != nullptr) {
            runtime_->OnSessionReady(peer, generation);
          }
        });
    transport_->SetReceiveHandler(
        [this](ae::Uid const& peer, std::vector<std::uint8_t> const& payload) {
          if (apptraverse::examples::TryHandleP2pProbePayload(*transport_, peer,
                                                              payload, {}, {})) {
            return;
          }
          if (runtime_ != nullptr) {
            runtime_->Deliver(peer, payload);
          }
        });
    transport_->Start(*app_, client_);
    ReportCloudState();
    cloud_sub_ = client_->cloud_connection().servers_update_event().Subscribe(
        [this]() { ReportCloudState(); });
    return true;
  }

  ae::Uid Uid() const { return uid_; }
  bool Ready() const { return transport_ != nullptr; }

  // One Aether service slice. Session-ready and inbound callbacks fire here.
  void Update() {
    if (!app_ || app_->IsExited()) {
      return;
    }
    (void)app_->Update(ae::Now());
    TracePingSchedule();
  }

  void TracePingSchedule() {
    auto policy = client_->connectivity_policy();
    policy.Load();
    if (!policy.is_loaded()) {
      return;
    }
    auto const status = policy->GetStatus();
    auto const now = ae::Now();
    auto const until_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            status.next_service_time - now)
            .count();
    // Transition-only: Aether's own next ping slot, not the remote's.
    // AuthorizedApi::ping(next_connect, rx_window) is local→server only;
    // there is no query for "did the other UID miss its ping".
    if (status.next_service_time == last_next_service_time_) {
      return;
    }
    last_next_service_time_ = status.next_service_time;
    auto const& timings = policy->rx_timings();
    auto const interval_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timings.front().conf.interval)
            .count();
    auto const window_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timings.front().conf.rx_window)
            .count();
    bool const missed_own_slot =
        status.next_service_time != ae::TimePoint{} &&
        status.next_service_time < now;
    trace_.Event(side_, missed_own_slot ? "AETHER_OWN_PING_MISSED"
                                        : "AETHER_NEXT_PING",
                 "until_ms=" + std::to_string(until_ms) +
                     " interval_ms=" + std::to_string(interval_ms) +
                     " rx_window_ms=" + std::to_string(window_ms) +
                     " can_suspend=" +
                     (status.can_suspend ? "1" : "0"));
  }

  void Shutdown() {
    // Order matters: drop our own subscriptions and peer sessions before the
    // AetherApp goes away, or late stream callbacks reach freed state.
    cloud_sub_ = ae::Subscription{};
    runtime_ = nullptr;
    if (transport_) {
      transport_->Stop();
      transport_.reset();
    }
    client_ = {};
    app_.reset();
  }

  // IHeadlessTransport: `from` is always this endpoint's own uid.
  void Register(HeadlessRoomRuntime* runtime) override { runtime_ = runtime; }
  void Unregister(HeadlessRoomRuntime* runtime) override {
    if (runtime_ == runtime) {
      runtime_ = nullptr;
    }
  }

  void Send(ae::Uid const&, ae::Uid const& to,
            std::vector<std::uint8_t> bytes) override {
    if (!transport_) {
      return;
    }
    trace_.Event(side_, "TRANSPORT_SEND", std::to_string(bytes.size()));
    transport_->Send(to, bytes);
  }

  void Connect(ae::Uid const&, ae::Uid const& to) override {
    if (!transport_) {
      return;
    }
    transport_->Connect(to);
  }

  void ReconnectSession(ae::Uid const&, ae::Uid const& to) override {
    if (!transport_) {
      return;
    }
    trace_.Event(side_, "TRANSPORT_RECONNECT", FormatAetherUid(to));
    transport_->Reconnect(to);
  }

 private:
  void ReportCloudState() {
    if (!client_) {
      return;
    }
    auto& cloud = client_->cloud_connection();
    std::size_t linked = 0;
    for (auto* server : cloud.selected_servers()) {
      if (server == nullptr) {
        continue;
      }
      auto* conn = server->client_connection();
      if (conn != nullptr &&
          conn->stream_info().link_state == ae::LinkState::kLinked) {
        ++linked;
      }
    }
    trace_.Event(side_, "AETHER_CLOUD_SERVERS",
                 "selected=" + std::to_string(cloud.selected_servers().size()) +
                     " linked=" + std::to_string(linked));
    if (linked > 0 && !cloud_ready_) {
      cloud_ready_ = true;
      trace_.Event(side_, "AETHER_CLOUD_READY",
                   "linked=" + std::to_string(linked));
    }
  }

  void OnTransportLog(std::string const& line) {
    static constexpr char const* kMarkers[] = {
        "P2P_SESSION_CREATE_BEGIN", "P2P_SESSION_CREATE_END",
        "P2P_SESSION_DESTROY",      "P2P_ATTACH_INCOMING_BEGIN",
        "P2P_ATTACH_INCOMING_END",  "P2P_SESSION_REPLACE_BEGIN",
        "P2P_SESSION_REPLACE_END",  "P2P_STREAM_STATE",
        "P2P_STREAM_WRITABLE",      "P2P_STREAM_DATA",
        "P2P_PAYLOAD_RECEIVED",     "P2P_SESSION_CALLBACK_STALE_DROPPED",
        "P2P_SESSION_LINK_WAIT",    "P2P_RECONNECT_SUPPRESSED",
    };
    for (auto const* marker : kMarkers) {
      if (line.rfind(marker, 0) == 0) {
        trace_.Event(side_, marker, line);
        return;
      }
    }
  }

  HeadlessTrace& trace_;
  std::string side_;
  std::unique_ptr<ae::AetherApp> app_;
  ae::Client::ptr client_;
  std::unique_ptr<AetherP2pTransport> transport_;
  ae::Subscription cloud_sub_;
  HeadlessRoomRuntime* runtime_{nullptr};
  ae::Uid uid_{};
  bool cloud_ready_{false};
  ae::TimePoint last_next_service_time_{};
};

// ---------------------------------------------------------------------------
// A party = one Aether endpoint + one room runtime, restartable in place
// ---------------------------------------------------------------------------

struct Party {
  std::string side;
  std::filesystem::path state_dir;
  std::string aether_client_name;
  std::unique_ptr<AetherEndpoint> endpoint;
  std::unique_ptr<HeadlessRoomRuntime> runtime;

  bool Up() const { return runtime != nullptr && runtime->IsRunning(); }
};

// Brings up Aether for a party and returns its uid without starting the room.
bool InitEndpoint(Party& party, HeadlessTrace& trace) {
  party.endpoint = std::make_unique<AetherEndpoint>(trace, party.side);
  return party.endpoint->Init(party.state_dir / "aether",
                             party.aether_client_name);
}

void StartRoom(Party& party, HeadlessTrace& trace) {
  party.runtime = std::make_unique<HeadlessRoomRuntime>(
      party.state_dir, *party.endpoint, trace, party.side);
  party.runtime->Start();
}

void StopParty(Party& party) {
  if (party.runtime) {
    party.runtime->Stop();
    party.runtime.reset();
  }
  if (party.endpoint) {
    party.endpoint->Shutdown();
    party.endpoint.reset();
  }
}

// ---------------------------------------------------------------------------
// Pump: one Aether slice + one business Tick per party, per iteration
// ---------------------------------------------------------------------------

template <typename DoneFn>
bool PumpAether(std::vector<Party*> const& parties, DoneFn done,
                std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    for (auto* party : parties) {
      if (party->endpoint) {
        party->endpoint->Update();
      }
      if (party->runtime) {
        party->runtime->Tick();
      }
    }
    if (done()) {
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}

void PumpAetherFor(std::vector<Party*> const& parties,
                   std::chrono::milliseconds duration) {
  (void)PumpAether(parties, [] { return false; }, duration);
}

// ---------------------------------------------------------------------------
// Trace reporting
// ---------------------------------------------------------------------------

struct Hop {
  std::string label;
  std::string side;
  std::string marker;
  std::int64_t at_us{0};
  std::int64_t delta_ms{0};
  bool found{false};
};

std::int64_t ToMs(std::int64_t us) { return us / 1000; }

std::vector<Hop> BuildChain(
    HeadlessTrace const& trace, std::int64_t from_us,
    std::vector<std::pair<std::string, std::string>> const& steps) {
  std::vector<Hop> out;
  std::int64_t cursor = from_us;
  for (auto const& [side, marker] : steps) {
    Hop hop{};
    hop.side = side;
    hop.marker = marker;
    hop.label = side + "/" + marker;
    auto const at = trace.FirstAfter(side, marker, cursor);
    if (at.has_value()) {
      hop.found = true;
      hop.at_us = *at;
      hop.delta_ms = ToMs(*at - cursor);
      cursor = *at;
    }
    out.push_back(hop);
  }
  return out;
}

void PrintChain(char const* title, std::vector<Hop> const& chain) {
  std::cout << "  " << title << '\n';
  for (auto const& hop : chain) {
    std::cout << "    " << hop.label << " ";
    if (hop.found) {
      std::cout << "+" << hop.delta_ms << " ms";
    } else {
      std::cout << "MISSING";
    }
    std::cout << '\n';
  }
}

void DumpTrace(HeadlessTrace const& trace, std::int64_t from_us) {
  std::cout << "  --- trace ---\n";
  for (auto const& e : trace.entries()) {
    if (e.timestamp_us < from_us) {
      continue;
    }
    std::cout << "    t_rel_ms=" << ToMs(e.timestamp_us - from_us) << " "
              << e.side << " " << e.marker;
    if (!e.detail.empty()) {
      std::cout << " | " << e.detail;
    }
    std::cout << '\n';
  }
}

// ---------------------------------------------------------------------------
// Scenario
// ---------------------------------------------------------------------------

struct Options {
  int dwell_ms{9000};
  int cycles{1};
  int budget_ms{1000};
  bool verbose{false};
};

struct CycleResult {
  bool delivered{false};
  std::int64_t restart_to_transcript_ms{0};
  std::int64_t restart_to_session_ready_ms{-1};
  std::int64_t restart_to_first_write_ms{-1};
  std::size_t duplicates{0};
};

std::string RunId() {
  std::random_device rd;
  std::mt19937_64 gen{rd()};
  auto const now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  return std::to_string(now) + "-" + std::to_string(gen() % 0xffffff);
}

int Run(Options const& options) {
  auto const run_id = RunId();
  auto const root =
      std::filesystem::temp_directory_path() / ("headless-aether-" + run_id);
  std::filesystem::create_directories(root);
  std::cout << "run_root=" << root.string() << '\n';

  HeadlessTrace trace;

  Party host{};
  host.side = "host";
  host.state_dir = root / "host";
  host.aether_client_name = "hdl-aether-host-" + run_id;

  Party client{};
  client.side = "client";
  client.state_dir = root / "client";
  client.aether_client_name = "hdl-aether-client-" + run_id;

  // --- Aether identities first: the room model must be distilled with the
  // real client uids, exactly like the product distill step does.
  std::cout << "step=host_aether_init\n";
  if (!InitEndpoint(host, trace)) {
    std::cerr << "FAILED to bring up host Aether client\n";
    return 1;
  }
  std::cout << "step=client_aether_init\n";
  if (!InitEndpoint(client, trace)) {
    std::cerr << "FAILED to bring up client Aether client\n";
    return 1;
  }
  std::cout << "step=aether_ready\n";
  std::cout << "host_uid=" << FormatAetherUid(host.endpoint->Uid()) << '\n';
  std::cout << "client_uid=" << FormatAetherUid(client.endpoint->Uid()) << '\n';

  std::cout << "step=host_distill\n";
  host.runtime = std::make_unique<HeadlessRoomRuntime>(
      host.state_dir, *host.endpoint, trace, host.side);
  host.runtime->Distill(ChatRoomRole::kHost, "HostUser",
                        host.endpoint->Uid());
  std::cout << "step=host_start\n";
  host.runtime->Start();

  std::cout << "step=client_distill\n";
  client.runtime = std::make_unique<HeadlessRoomRuntime>(
      client.state_dir, *client.endpoint, trace, client.side);
  client.runtime->Distill(ChatRoomRole::kClient, "ClientUser",
                          client.endpoint->Uid(), host.endpoint->Uid());
  std::cout << "step=client_start\n";
  client.runtime->Start();
  std::cout << "step=both_started\n";

  std::vector<Party*> parties{&host, &client};

  // --- Activation ---
  auto const activation_start = HeadlessTrace::NowUs();
  auto const active = PumpAether(
      parties,
      [&] {
        return host.runtime->Presenter().RoomStatus() == RoomUiStatus::kActive &&
               client.runtime->Presenter().RoomStatus() ==
                   RoomUiStatus::kActive &&
               client.runtime->Presenter().SendEnabled();
      },
      std::chrono::seconds{60});
  auto const activation_ms = ToMs(HeadlessTrace::NowUs() - activation_start);
  std::cout << "activation: " << (active ? "OK" : "TIMEOUT") << " "
            << activation_ms << " ms\n";
  if (!active) {
    DumpTrace(trace, activation_start);
    StopParty(host);
    StopParty(client);
    return 1;
  }
  std::cout << "  join_count host=" << host.runtime->Presenter().JoinCount()
            << " client=" << client.runtime->Presenter().JoinCount()
            << " revision host=" << host.runtime->AppliedRevision()
            << " client=" << client.runtime->AppliedRevision() << '\n';

  // --- Steady-state message latency in both directions ---
  {
    auto const t0 = HeadlessTrace::NowUs();
    (void)host.runtime->Send("AETHER_HOST_MESSAGE");
    auto const ok = PumpAether(
        parties,
        [&] {
          return client.runtime->Presenter().CountMessage(
                     "AETHER_HOST_MESSAGE") == 1;
        },
        std::chrono::seconds{20});
    std::cout << "host->client message: " << (ok ? "OK" : "TIMEOUT") << " "
              << ToMs(HeadlessTrace::NowUs() - t0) << " ms\n";
  }
  {
    auto const t0 = HeadlessTrace::NowUs();
    (void)client.runtime->Send("AETHER_CLIENT_MESSAGE");
    auto const ok = PumpAether(
        parties,
        [&] {
          return host.runtime->Presenter().CountMessage(
                     "AETHER_CLIENT_MESSAGE") == 1;
        },
        std::chrono::seconds{20});
    std::cout << "client->host message: " << (ok ? "OK" : "TIMEOUT") << " "
              << ToMs(HeadlessTrace::NowUs() - t0) << " ms\n";
  }

  // --- Restart cycles: host down, client sends, host returns ---
  std::vector<CycleResult> results;
  bool over_budget = false;
  for (int cycle = 1; cycle <= options.cycles; ++cycle) {
    auto const text = "OFFLINE_MESSAGE_" + std::to_string(cycle);
    std::cout << "=== cycle " << cycle << " (" << text << ") ===\n";

    StopParty(host);
    PumpAetherFor({&client}, std::chrono::milliseconds{200});

    auto const commit_us = HeadlessTrace::NowUs();
    if (!client.runtime->Send(text)) {
      std::cerr << "client refused to send while host was down\n";
      return 1;
    }
    PumpAetherFor({&client}, std::chrono::milliseconds{200});
    std::cout << "  client local=" << client.runtime->Presenter().CountMessage(text)
              << " pending=" << client.runtime->PendingPackets() << '\n';

    // Host stays down for the dwell window; only the client runs.
    PumpAetherFor({&client},
                  std::chrono::milliseconds{options.dwell_ms});

    // --- Host returns: new AetherApp from the same state directory ---
    auto const restart_us = HeadlessTrace::NowUs();
    trace.Event("host", "RESTART_BEGIN");
    if (!InitEndpoint(host, trace)) {
      std::cerr << "FAILED to restart host Aether client\n";
      return 1;
    }
    StartRoom(host, trace);
    trace.Event("host", "RESTART_END");

    auto const delivered = PumpAether(
        parties,
        [&] { return host.runtime->Presenter().CountMessage(text) >= 1; },
        std::chrono::seconds{40});
    auto const delivered_us = HeadlessTrace::NowUs();

    CycleResult result{};
    result.delivered = delivered;
    result.restart_to_transcript_ms = ToMs(delivered_us - restart_us);
    result.duplicates =
        host.runtime->Presenter().CountMessage(text) > 1
            ? host.runtime->Presenter().CountMessage(text) - 1
            : 0;
    if (auto at = trace.FirstAfter("host", "P2P_STREAM_WRITABLE", restart_us)) {
      result.restart_to_session_ready_ms = ToMs(*at - restart_us);
    }
    if (auto at = trace.FirstAfter("client", "SYNC_WRITE", restart_us)) {
      result.restart_to_first_write_ms = ToMs(*at - restart_us);
    }
    results.push_back(result);

    std::cout << "  delivered=" << (delivered ? "yes" : "NO")
              << " restart_to_transcript_ms=" << result.restart_to_transcript_ms
              << " restart_to_host_writable_ms="
              << result.restart_to_session_ready_ms
              << " restart_to_client_write_ms="
              << result.restart_to_first_write_ms
              << " duplicates=" << result.duplicates << '\n';

    auto const commit_chain = BuildChain(
        trace, commit_us,
        {{"client", "EVENT_COMMITTED"}, {"client", "PENDING_ADDED"}});
    PrintChain("commit -> pending", commit_chain);

    auto const recovery_chain =
        BuildChain(trace, restart_us,
                   {{"host", "AETHER_RX_TIMINGS_RESET"},
                    {"host", "AETHER_CLOUD_READY"},
                    {"host", "P2P_STREAM_WRITABLE"},
                    {"host", "STARTUP_NOTIFY"},
                    {"client", "PEER_REJOINED"},
                    {"client", "SESSION_READY"},
                    {"client", "SYNC_WRITE"},
                    {"host", "P2P_PAYLOAD_RECEIVED"},
                    {"host", "SYNC_RECEIVE"},
                    {"host", "SYNC_APPLY"},
                    {"host", "SYNC_PRESENTATION"}});
    PrintChain("restart -> presentation", recovery_chain);

    if (!delivered || result.duplicates > 0 ||
        result.restart_to_transcript_ms > options.budget_ms) {
      over_budget = true;
      DumpTrace(trace, restart_us);
    } else if (options.verbose) {
      DumpTrace(trace, restart_us);
    }
  }

  std::cout << "=== summary ===\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    std::cout << "cycle=" << (i + 1)
              << " delivered=" << (results[i].delivered ? 1 : 0)
              << " restart_to_transcript_ms="
              << results[i].restart_to_transcript_ms
              << " duplicates=" << results[i].duplicates << '\n';
  }
  std::cout << "host_join_count=" << host.runtime->Presenter().JoinCount()
            << " client_join_count=" << client.runtime->Presenter().JoinCount()
            << " host_revision=" << host.runtime->AppliedRevision()
            << " client_revision=" << client.runtime->AppliedRevision()
            << " client_send_enabled="
            << (client.runtime->Presenter().SendEnabled() ? 1 : 0) << '\n';

  StopParty(host);
  StopParty(client);

  if (over_budget) {
    std::cout << "HEADLESS_AETHER_RECOVERY_OVER_BUDGET\n";
    return 2;
  }
  std::cout << "HEADLESS_AETHER_RECOVERY_OK\n";
  return 0;
}

}  // namespace
}  // namespace apptraverse::testing

int main(int argc, char** argv) {
  // Unbuffered: a crash inside Aether must not swallow the progress log.
  std::cout << std::unitbuf;
  apptraverse::testing::Options options{};
  for (int i = 1; i < argc; ++i) {
    std::string const arg = argv[i];
    if (arg == "--dwell-ms" && i + 1 < argc) {
      options.dwell_ms = std::atoi(argv[++i]);
    } else if (arg == "--cycles" && i + 1 < argc) {
      options.cycles = std::atoi(argv[++i]);
    } else if (arg == "--budget-ms" && i + 1 < argc) {
      options.budget_ms = std::atoi(argv[++i]);
    } else if (arg == "--verbose") {
      options.verbose = true;
    }
  }
  return apptraverse::testing::Run(options);
}
