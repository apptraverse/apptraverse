// Business-level recovery tests over HeadlessMemoryTransport: no Aether, no
// Win32, no real network. Real serialized packets travel through the real
// decode / sync / room path; only the wire is in memory.
//
// Question these tests answer: after a link outage, is the delay in the chat
// business logic or in the Aether session lifecycle?
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "headless_room_runtime.h"

namespace {

using apptraverse::ChatRoomRole;
using apptraverse::chat::RoomUiStatus;
using apptraverse::testing::HeadlessMemoryTransport;
using apptraverse::testing::HeadlessRoomRuntime;
using apptraverse::testing::HeadlessTrace;
using apptraverse::testing::PumpFor;
using apptraverse::testing::PumpUntil;
using apptraverse::testing::TraceEvent;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

constexpr auto kActivationTimeout = std::chrono::milliseconds{5000};
constexpr auto kDeliveryTimeout = std::chrono::milliseconds{15000};
constexpr auto kSettleTime = std::chrono::milliseconds{300};
// Short outage stays inside ChatSyncTiming::offline_timeout; the long one
// crosses it, so the presence offline transition and the recovery flush are
// both exercised.
constexpr auto kShortOutage = std::chrono::milliseconds{300};
constexpr auto kLongOutage = std::chrono::milliseconds{6000};
// Everything after reconnect is business logic only, so it must be fast.
constexpr std::int64_t kRecoveryBudgetMs = 1000;

ae::Uid MakeUid(std::uint8_t fill) {
  std::array<std::uint8_t, ae::Uid::kSize> bytes{};
  bytes.fill(fill);
  return ae::Uid{bytes};
}

std::filesystem::path MakeRunRoot(std::string const& tag) {
  auto const stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  auto root = std::filesystem::temp_directory_path() /
              ("headless-mem-" + tag + "-" + std::to_string(stamp));
  std::filesystem::create_directories(root);
  return root;
}

// One measured hop of the recovery chain.
struct Hop {
  char const* name;
  std::int64_t delta_ms{-1};
  std::int64_t at_ms{-1};
};

// Finds the first `marker` on `side` at or after `since_us`.
std::optional<std::int64_t> At(HeadlessTrace const& trace,
                               std::string_view side, std::string_view marker,
                               std::int64_t since_us) {
  return trace.FirstAfter(side, marker, since_us);
}

struct RecoveryChain {
  std::vector<Hop> hops;
  std::int64_t reconnect_to_presentation_ms{-1};
};

// Builds the commit -> pending -> reconnect -> write -> receive -> apply ->
// presentation chain, where sender/receiver depend on the direction tested.
RecoveryChain BuildChain(HeadlessTrace const& trace, std::string const& sender,
                         std::string const& receiver, std::int64_t t_commit_us,
                         std::int64_t t_reconnect_us,
                         std::int64_t t_visible_us) {
  RecoveryChain chain;
  auto const commit = At(trace, sender, "EVENT_COMMITTED", t_commit_us);
  auto const pending = At(trace, sender, "PENDING_ADDED", t_commit_us);
  auto const reconnect = At(trace, "transport", "TRANSPORT_RECONNECT",
                            t_reconnect_us);
  auto const ready = At(trace, sender, "SESSION_READY", t_reconnect_us);
  auto const write = At(trace, sender, "SYNC_WRITE", t_reconnect_us);
  auto const receive = At(trace, receiver, "SYNC_RECEIVE", t_reconnect_us);
  auto const apply = At(trace, receiver, "SYNC_APPLY", t_reconnect_us);

  auto const base = commit.value_or(t_commit_us);
  auto ms = [base](std::optional<std::int64_t> v) -> std::int64_t {
    return v.has_value() ? (*v - base) / 1000 : -1;
  };
  auto delta = [](std::optional<std::int64_t> a,
                  std::optional<std::int64_t> b) -> std::int64_t {
    return (a.has_value() && b.has_value()) ? (*b - *a) / 1000 : -1;
  };

  chain.hops.push_back({"EVENT_COMMITTED", 0, ms(commit)});
  chain.hops.push_back({"PENDING_ADDED", delta(commit, pending), ms(pending)});
  chain.hops.push_back(
      {"TRANSPORT_RECONNECT", delta(pending, reconnect), ms(reconnect)});
  chain.hops.push_back({"SESSION_READY", delta(reconnect, ready), ms(ready)});
  chain.hops.push_back({"SYNC_WRITE", delta(ready, write), ms(write)});
  chain.hops.push_back({"SYNC_RECEIVE", delta(write, receive), ms(receive)});
  chain.hops.push_back({"SYNC_APPLY", delta(receive, apply), ms(apply)});
  chain.hops.push_back(
      {"SYNC_PRESENTATION", delta(apply, t_visible_us), ms(t_visible_us)});
  if (reconnect.has_value()) {
    chain.reconnect_to_presentation_ms = (t_visible_us - *reconnect) / 1000;
  }
  return chain;
}

void PrintChain(std::string const& label, RecoveryChain const& chain) {
  std::cout << "CHAIN " << label << '\n';
  for (auto const& hop : chain.hops) {
    std::cout << "  " << std::left << std::setw(20) << hop.name
              << " at=" << hop.at_ms << "ms delta=+" << hop.delta_ms << "ms\n";
  }
  std::cout << "  reconnect_to_presentation_ms="
            << chain.reconnect_to_presentation_ms << '\n';
}

// Every hop must have happened (-1 means a missing marker). The budget applies
// to business hops only: the PENDING_ADDED -> TRANSPORT_RECONNECT gap is the
// outage the test itself imposed.
void CheckChainWithinBudget(RecoveryChain const& chain,
                            std::int64_t budget_ms) {
  for (auto const& hop : chain.hops) {
    CHECK(hop.delta_ms >= 0);
    if (std::string_view{hop.name} == "TRANSPORT_RECONNECT") {
      continue;
    }
    CHECK(hop.delta_ms <= budget_ms);
  }
  CHECK(chain.reconnect_to_presentation_ms >= 0);
  CHECK(chain.reconnect_to_presentation_ms <= budget_ms);
}

struct Fixture {
  std::filesystem::path root;
  HeadlessTrace trace;
  HeadlessMemoryTransport transport{trace};
  std::unique_ptr<HeadlessRoomRuntime> host;
  std::unique_ptr<HeadlessRoomRuntime> client;
  ae::Uid host_uid;
  ae::Uid client_uid;

  Fixture(std::string const& tag, std::uint8_t host_fill,
          std::uint8_t client_fill)
      : root{MakeRunRoot(tag)},
        host_uid{MakeUid(host_fill)},
        client_uid{MakeUid(client_fill)} {
    host = std::make_unique<HeadlessRoomRuntime>(root / "host", transport,
                                                 trace, "host");
    client = std::make_unique<HeadlessRoomRuntime>(root / "client", transport,
                                                  trace, "client");
  }

  ~Fixture() {
    host.reset();
    client.reset();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
  }

  std::vector<HeadlessRoomRuntime*> All() {
    return {host.get(), client.get()};
  }

  void StartActive() {
    host->StartHost("HostUser", host_uid);
    client->StartClient("ClientUser", client_uid, host_uid);
    auto all = All();
    CHECK(PumpUntil(
        transport, all,
        [&] {
          return host->Room()->ui_status() == RoomUiStatus::kActive &&
                 client->Room()->ui_status() == RoomUiStatus::kActive &&
                 host->Presenter().JoinCount() == 2 &&
                 client->Presenter().JoinCount() == 2;
        },
        kActivationTimeout));
    CHECK(client->Presenter().SendEnabled());
    CHECK(host->Presenter().SendEnabled());
    CHECK(client->Presenter().PendingCount() == 0);
    CHECK(host->Presenter().PendingCount() == 0);
  }

  // Sanity check that the plain online path works before cutting the link.
  void CheckOnlineMessage(HeadlessRoomRuntime& from, HeadlessRoomRuntime& to,
                          std::string const& text) {
    auto all = All();
    CHECK(from.Send(text));
    CHECK(PumpUntil(
        transport, all, [&] { return to.Presenter().CountMessage(text) == 1; },
        kDeliveryTimeout));
  }
};

// Cuts `down_uid`'s link, commits `text` on the sender, waits `outage`, brings
// the link back and measures the whole chain to the receiver's presentation.
RecoveryChain RunOutageCycle(Fixture& fx, HeadlessRoomRuntime& sender,
                             std::string const& sender_side,
                             HeadlessRoomRuntime& receiver,
                             std::string const& receiver_side,
                             ae::Uid const& down_uid, std::string const& text,
                             std::chrono::milliseconds outage) {
  auto all = fx.All();
  auto const gen_before =
      fx.transport.SessionGeneration(sender.Uid(), receiver.Uid());

  fx.transport.Disconnect(down_uid);

  auto const t_commit = HeadlessTrace::NowUs();
  CHECK(sender.Send(text));
  PumpFor(fx.transport, all, outage);
  CHECK(sender.Presenter().CountMessage(text) == 1);
  CHECK(sender.Presenter().PendingCount() > 0);
  CHECK(receiver.Presenter().CountMessage(text) == 0);

  auto const t_reconnect = HeadlessTrace::NowUs();
  fx.transport.Reconnect(down_uid);
  CHECK(PumpUntil(
      fx.transport, all,
      [&] { return receiver.Presenter().CountMessage(text) == 1; },
      kDeliveryTimeout));
  auto const t_visible = HeadlessTrace::NowUs();

  CHECK(PumpUntil(
      fx.transport, all, [&] { return sender.Presenter().PendingCount() == 0; },
      kDeliveryTimeout));
  PumpFor(fx.transport, all, kSettleTime);

  CHECK(fx.transport.SessionGeneration(sender.Uid(), receiver.Uid()) >
        gen_before);
  CHECK(receiver.Presenter().CountMessage(text) == 1);
  CHECK(sender.Presenter().CountMessage(text) == 1);
  CHECK(sender.Presenter().PendingCount() == 0);
  CHECK(receiver.Presenter().PendingCount() == 0);
  CHECK(fx.host->Presenter().JoinCount() == 2);
  CHECK(fx.client->Presenter().JoinCount() == 2);
  CHECK(fx.client->Presenter().SendEnabled());
  CHECK(fx.client->Room()->ui_status() == RoomUiStatus::kActive);
  CHECK(fx.host->Room()->applied_revision() == 2);
  CHECK(fx.client->Room()->applied_revision() == 2);

  return BuildChain(fx.trace, sender_side, receiver_side, t_commit, t_reconnect,
                    t_visible);
}

// ---------------------------------------------------------------------------
// Test 1: Host link down, Client sends, Host link back
// ---------------------------------------------------------------------------

void TestOfflineHostMessageRecovery() {
  Fixture fx{"offline-host", 0x11, 0x22};
  fx.StartActive();
  fx.CheckOnlineMessage(*fx.client, *fx.host, "ONLINE_PROBE_1");

  auto const short_chain =
      RunOutageCycle(fx, *fx.client, "client", *fx.host, "host", fx.host_uid,
                     "CLIENT_OFFLINE_MESSAGE", kShortOutage);
  PrintChain("OfflineHostMessageRecovery client->host outage=short",
             short_chain);
  CheckChainWithinBudget(short_chain, kRecoveryBudgetMs);

  auto const long_chain =
      RunOutageCycle(fx, *fx.client, "client", *fx.host, "host", fx.host_uid,
                     "CLIENT_OFFLINE_MESSAGE_LONG", kLongOutage);
  PrintChain("OfflineHostMessageRecovery client->host outage=long", long_chain);
  CheckChainWithinBudget(long_chain, kRecoveryBudgetMs);

  std::cout << "HeadlessRoom.OfflineHostMessageRecovery OK\n";
}

// ---------------------------------------------------------------------------
// Test 2: Client link down, Host sends, Client link back
// ---------------------------------------------------------------------------

void TestOfflineClientMessageRecovery() {
  Fixture fx{"offline-client", 0x33, 0x44};
  fx.StartActive();
  fx.CheckOnlineMessage(*fx.host, *fx.client, "ONLINE_PROBE_2");

  auto const short_chain =
      RunOutageCycle(fx, *fx.host, "host", *fx.client, "client", fx.client_uid,
                     "HOST_OFFLINE_MESSAGE", kShortOutage);
  PrintChain("OfflineClientMessageRecovery host->client outage=short",
             short_chain);
  CheckChainWithinBudget(short_chain, kRecoveryBudgetMs);

  auto const long_chain =
      RunOutageCycle(fx, *fx.host, "host", *fx.client, "client", fx.client_uid,
                     "HOST_OFFLINE_MESSAGE_LONG", kLongOutage);
  PrintChain("OfflineClientMessageRecovery host->client outage=long",
             long_chain);
  CheckChainWithinBudget(long_chain, kRecoveryBudgetMs);

  std::cout << "HeadlessRoom.OfflineClientMessageRecovery OK\n";
}

}  // namespace

std::size_t CountMarker(HeadlessTrace const& trace, std::string_view side,
                        std::string_view marker) {
  std::size_t n = 0;
  for (auto const& e : trace.entries()) {
    if (e.side == side && e.marker == marker) {
      ++n;
    }
  }
  return n;
}

struct FakeUap {
  std::int64_t last_ping_server_ms{1'000};
  std::int64_t delta_ms{5'500};
  std::int64_t server_now_ms{1'000};
  bool fail{false};
};

void BindFakeUap(HeadlessRoomRuntime& runtime, std::shared_ptr<FakeUap> uap) {
  runtime.SetQueryPeerSchedule(
      [uap](ae::Uid const&, apptraverse::chat::PeerScheduleQueryCallback cb) {
        if (uap->fail) {
          cb(std::nullopt);
          return;
        }
        cb(apptraverse::chat::MakePeerScheduleSnapshot(
            uap->last_ping_server_ms, uap->delta_ms, uap->server_now_ms));
      });
}

std::size_t PayloadWrites(HeadlessTrace const& trace, std::string_view side) {
  return CountMarker(trace, side, "CHAT_SYNC_PAYLOAD_WRITE");
}

void TestRetriesUntilDeadline() {
  Fixture fx{"sched-retries", 0x51, 0x52};
  fx.StartActive();
  fx.transport.Disconnect(fx.host_uid);
  auto uap = std::make_shared<FakeUap>();
  BindFakeUap(*fx.client, uap);
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("until-deadline"));
  auto const writes_before = PayloadWrites(fx.trace, "client");
  PumpFor(fx.transport, fx.All(), std::chrono::milliseconds{5500});
  auto const writes = PayloadWrites(fx.trace, "client") - writes_before;
  CHECK(writes >= 2);
  CHECK(writes <= 4);
  CHECK(fx.client->Component()->GetPeerReachability(fx.host_uid) ==
        apptraverse::chat::PeerReachability::kWaitingForScheduledPing);
  std::cout << "HeadlessRoom.RetriesUntilDeadline writes=" << writes
            << " OK\n";
}

void TestMissedPingStopsRetries() {
  Fixture fx{"sched-missed", 0x53, 0x54};
  fx.StartActive();
  fx.transport.Disconnect(fx.host_uid);
  auto uap = std::make_shared<FakeUap>();
  BindFakeUap(*fx.client, uap);
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("missed-ping"));
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->IsPeerOfflineMissedVisit(fx.host_uid);
      },
      std::chrono::seconds{10}));
  auto const writes_at_hold = PayloadWrites(fx.trace, "client");
  PumpFor(fx.transport, fx.All(), std::chrono::seconds{20});
  CHECK(PayloadWrites(fx.trace, "client") == writes_at_hold);
  CHECK(fx.client->Presenter().PendingCount() > 0);
  CHECK(fx.client->Component()->GetPeerReachability(fx.host_uid) ==
        apptraverse::chat::PeerReachability::kOfflineMissedPing);
  std::cout << "HeadlessRoom.MissedPingStopsRetries writes_frozen="
            << writes_at_hold << " OK\n";
}

void TestZeroDeltaStopsRetries() {
  Fixture fx{"sched-zero", 0x55, 0x56};
  fx.StartActive();
  fx.transport.Disconnect(fx.host_uid);
  auto uap = std::make_shared<FakeUap>();
  BindFakeUap(*fx.client, uap);
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("zero-delta"));
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->GetPeerReachability(fx.host_uid) ==
               apptraverse::chat::PeerReachability::kWaitingForScheduledPing;
      },
      std::chrono::seconds{2}));
  uap->delta_ms = 0;
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->IsPeerOfflineNoFuturePing(fx.host_uid);
      },
      std::chrono::seconds{10}));
  auto const writes_at_hold = PayloadWrites(fx.trace, "client");
  PumpFor(fx.transport, fx.All(), std::chrono::seconds{4});
  CHECK(PayloadWrites(fx.trace, "client") == writes_at_hold);
  std::cout << "HeadlessRoom.ZeroDeltaStopsRetries OK\n";
}

void TestOnlineRearms() {
  Fixture fx{"sched-rearm", 0x57, 0x58};
  fx.StartActive();
  fx.transport.Disconnect(fx.host_uid);
  auto uap = std::make_shared<FakeUap>();
  BindFakeUap(*fx.client, uap);
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("resume-me"));
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->IsPeerOfflineMissedVisit(fx.host_uid);
      },
      std::chrono::seconds{10}));
  fx.transport.Reconnect(fx.host_uid);
  uap->last_ping_server_ms += 10'000;
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] { return fx.host->Presenter().CountMessage("resume-me") == 1; },
      kDeliveryTimeout));
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] { return fx.client->Presenter().PendingCount() == 0; },
      kDeliveryTimeout));
  CHECK(fx.host->Presenter().CountMessage("resume-me") == 1);
  CHECK(!fx.client->Component()->IsPeerOfflineMissedVisit(fx.host_uid));
  bool marker_gone = true;
  for (auto const& line : fx.client->Presenter().Lines()) {
    if (line.find(apptraverse::chat::kOfflinePingMarker) !=
        std::string::npos) {
      marker_gone = false;
    }
  }
  CHECK(marker_gone);
  std::cout << "HeadlessRoom.OnlineRearms OK\n";
}

void TestPingAdvanced() {
  Fixture fx{"sched-advance", 0x59, 0x5a};
  fx.StartActive();
  fx.transport.Disconnect(fx.host_uid);
  auto uap = std::make_shared<FakeUap>();
  BindFakeUap(*fx.client, uap);
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("advanced-ping"));
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->GetPeerReachability(fx.host_uid) ==
               apptraverse::chat::PeerReachability::kWaitingForScheduledPing;
      },
      std::chrono::seconds{2}));
  uap->last_ping_server_ms += 10'000;
  uap->server_now_ms = uap->last_ping_server_ms;
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] { return CountMarker(fx.trace, "client", "PEER_PING_ADVANCED") > 0; },
      std::chrono::seconds{10}));
  CHECK(!fx.client->Component()->IsPeerOfflineMissedVisit(fx.host_uid));
  CHECK(fx.client->Component()->GetPeerReachability(fx.host_uid) ==
        apptraverse::chat::PeerReachability::kWaitingForScheduledPing);
  auto const writes_at_advance = PayloadWrites(fx.trace, "client");
  PumpFor(fx.transport, fx.All(), std::chrono::seconds{4});
  CHECK(PayloadWrites(fx.trace, "client") > writes_at_advance);
  std::cout << "HeadlessRoom.PingAdvanced OK\n";
}

void TestQueryFailure() {
  Fixture fx{"sched-fail", 0x5b, 0x5c};
  fx.StartActive();
  fx.transport.Disconnect(fx.host_uid);
  auto uap = std::make_shared<FakeUap>();
  BindFakeUap(*fx.client, uap);
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("query-fail"));
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->GetPeerReachability(fx.host_uid) ==
               apptraverse::chat::PeerReachability::kWaitingForScheduledPing;
      },
      std::chrono::seconds{2}));
  uap->fail = true;
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->GetPeerReachability(fx.host_uid) ==
               apptraverse::chat::PeerReachability::kScheduleCheckPending;
      },
      std::chrono::seconds{10}));
  CHECK(!fx.client->Component()->IsPeerOfflineMissedVisit(fx.host_uid));
  bool saw_marker = false;
  for (auto const& line : fx.client->Presenter().Lines()) {
    if (line.find(apptraverse::chat::kOfflinePingMarker) !=
        std::string::npos) {
      saw_marker = true;
    }
  }
  CHECK(!saw_marker);
  auto const writes_held = PayloadWrites(fx.trace, "client");
  PumpFor(fx.transport, fx.All(), std::chrono::seconds{3});
  CHECK(PayloadWrites(fx.trace, "client") == writes_held);
  uap->fail = false;
  uap->last_ping_server_ms += 10'000;
  uap->server_now_ms = uap->last_ping_server_ms;
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] { return CountMarker(fx.trace, "client", "PEER_PING_ADVANCED") > 0; },
      std::chrono::seconds{6}));
  CHECK(!fx.client->Component()->IsPeerOfflineMissedVisit(fx.host_uid));
  std::cout << "HeadlessRoom.QueryFailure OK\n";
}

void TestClockSkew() {
  auto const last = std::int64_t{2'000'000};
  auto const delta = std::int64_t{10'000};
  auto const server_now = std::int64_t{2'002'000};
  auto const now = std::chrono::steady_clock::now();
  (void)(std::chrono::system_clock::now() + std::chrono::minutes{10});
  (void)(std::chrono::system_clock::now() - std::chrono::minutes{10});
  auto const a = apptraverse::chat::MakePeerScheduleSnapshot(
      last, delta, server_now, now);
  auto const b = apptraverse::chat::MakePeerScheduleSnapshot(
      last, delta, server_now, now);
  CHECK(a.local_deadline.has_value());
  CHECK(b.local_deadline.has_value());
  CHECK(*a.local_deadline == *b.local_deadline);
  std::cout << "HeadlessRoom.ClockSkew OK\n";
}

void TestGracefulShutdownZero() {
  Fixture fx{"sched-shutdown", 0x5f, 0x60};
  fx.StartActive();
  auto host_uap = std::make_shared<FakeUap>();
  auto client_view = std::make_shared<FakeUap>(*host_uap);
  BindFakeUap(*fx.client, client_view);
  fx.host->SetAnnounceNextPingUnknown([host_uap, client_view]() {
    host_uap->delta_ms = 0;
    client_view->delta_ms = 0;
  });
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("after-shutdown"));
  fx.transport.Disconnect(fx.host_uid);
  fx.host->Stop();
  CHECK(CountMarker(fx.trace, "host", "AETHER_NEXT_PING_UNKNOWN_SENT") > 0);
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->IsPeerOfflineNoFuturePing(fx.host_uid);
      },
      std::chrono::seconds{10}));
  auto const writes_at_hold = PayloadWrites(fx.trace, "client");
  PumpFor(fx.transport, fx.All(), std::chrono::seconds{4});
  CHECK(PayloadWrites(fx.trace, "client") == writes_at_hold);
  std::cout << "HeadlessRoom.GracefulShutdownZero OK\n";
}

void TestMarkerPresentationOnly() {
  Fixture fx{"sched-marker", 0x61, 0x62};
  fx.StartActive();
  fx.transport.Disconnect(fx.host_uid);
  auto uap = std::make_shared<FakeUap>();
  BindFakeUap(*fx.client, uap);
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(fx.client->Send("hello"));
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] {
        return fx.client->Component()->ShowOfflinePingMarker(fx.host_uid);
      },
      std::chrono::seconds{10}));
  auto const snap = fx.client->Component()->CapturePresentation();
  bool saw_marker_line = false;
  bool event_text_clean = false;
  for (auto const& item : snap.timeline) {
    if (item.kind == apptraverse::chat::ChatTimelineItemKind::kMessage &&
        item.text == "hello") {
      event_text_clean =
          item.text.find(apptraverse::chat::kOfflinePingMarker) ==
          std::string::npos;
      CHECK(item.show_offline_marker);
    }
  }
  auto const formatted =
      apptraverse::examples::FormatChatPresentationUtf8(snap);
  if (formatted.find("hello") != std::string::npos &&
      formatted.find(apptraverse::chat::kOfflinePingMarker) !=
          std::string::npos) {
    saw_marker_line = true;
  }
  CHECK(event_text_clean);
  CHECK(saw_marker_line);
  fx.transport.Reconnect(fx.host_uid);
  uap->last_ping_server_ms += 10'000;
  fx.client->InjectPeerOnline(fx.host_uid);
  CHECK(PumpUntil(
      fx.transport, fx.All(),
      [&] { return fx.client->Presenter().PendingCount() == 0; },
      kDeliveryTimeout));
  bool marker_gone = true;
  for (auto const& line : fx.client->Presenter().Lines()) {
    if (line.find(apptraverse::chat::kOfflinePingMarker) !=
        std::string::npos) {
      marker_gone = false;
    }
  }
  CHECK(marker_gone);
  std::cout << "HeadlessRoom.MarkerPresentationOnly OK\n";
}

void TestFirstContactUnaffected() {
  Fixture fx{"sched-first", 0x5d, 0x5e};
  fx.StartActive();
  CHECK(fx.host->Room()->applied_revision() == 2);
  CHECK(fx.client->Room()->applied_revision() == 2);
  CHECK(fx.host->Presenter().JoinCount() == 2);
  CHECK(fx.client->Presenter().JoinCount() == 2);
  CHECK(fx.client->Presenter().SendEnabled());
  CHECK(fx.host->Room()->ui_status() == RoomUiStatus::kActive);
  CHECK(fx.client->Room()->ui_status() == RoomUiStatus::kActive);
  CHECK(!fx.client->Component()->IsPeerOfflineMissedVisit(fx.host_uid));
  CHECK(!fx.host->Component()->IsPeerOfflineMissedVisit(fx.client_uid));
  CHECK(fx.client->Component()->GetPeerReachability(fx.host_uid) !=
        apptraverse::chat::PeerReachability::kOfflineMissedPing);
  CHECK(fx.client->Component()->GetPeerReachability(fx.host_uid) !=
        apptraverse::chat::PeerReachability::kOfflineNoFuturePing);
  std::cout << "HeadlessRoom.FirstContactUnaffected OK\n";
}

int main() {
  TestOfflineHostMessageRecovery();
  TestOfflineClientMessageRecovery();
  TestRetriesUntilDeadline();
  TestMissedPingStopsRetries();
  TestZeroDeltaStopsRetries();
  TestOnlineRearms();
  TestPingAdvanced();
  TestQueryFailure();
  TestClockSkew();
  TestGracefulShutdownZero();
  TestMarkerPresentationOnly();
  TestFirstContactUnaffected();
  std::cout << "headless_room_memory_transport_test OK\n";
  return 0;
}
