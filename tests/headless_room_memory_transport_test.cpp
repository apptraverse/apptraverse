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

int main() {
  TestOfflineHostMessageRecovery();
  TestOfflineClientMessageRecovery();
  std::cout << "headless_room_memory_transport_test OK\n";
  return 0;
}
