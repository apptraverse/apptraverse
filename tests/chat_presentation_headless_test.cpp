#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_events.h"
#include "chat_model.h"
#include "chat_presentation.h"
#include "chat_presence.h"
#include "chat_shared.h"
#include "remote_presence_poller.h"
#include "ui_send_latency_tracker.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

void TestSourceGuardNoMirrorOrHwnd() {
#ifdef CHAT_PRESENTATION_HEADLESS_SOURCE
  std::ifstream in{CHAT_PRESENTATION_HEADLESS_SOURCE};
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  auto const has_include = [&](std::string const& path) {
    return text.find("#include <" + path + ">") != std::string::npos ||
           text.find("#include \"" + path + "\"") != std::string::npos;
  };
  CHECK(!has_include("apptraverse/graph_mirror.h"));
  CHECK(!has_include("apptraverse/ui_mirror.h"));
  CHECK(!has_include("apptraverse/overlay_domain_storage.h"));
  CHECK(!has_include("windows.h"));
  CHECK(!has_include("win_presenters.h"));
  // Split tokens so this guard does not match its own source literals.
  CHECK(text.find(std::string("CopyModel") + "GraphToUiDomain") ==
        std::string::npos);
  CHECK(text.find(std::string("Create") + "Window") == std::string::npos);
  CHECK(text.find(std::string("LB_ADD") + "STRING") == std::string::npos);
#else
  CHECK(false && "CHAT_PRESENTATION_HEADLESS_SOURCE is required");
#endif
}

void TestLocalChatHostJoinAndMessages() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CommitHostJoin(*application);
  CHECK(FormatChatFeedLine(*application->chat_room->feed[0]) ==
        "Nikolay joined the chat");

  CommitSendChatMessage(*application->chat_room, *application->host_client,
                        "hello");
  CHECK(FormatChatFeedLine(*application->chat_room->feed[1]) ==
        "Nikolay: hello");

  auto const gen_before = application->chat_room->Generation();
  CHECK(!CommitSendChatMessage(*application->chat_room,
                               *application->host_client, "")
             .is_valid());
  CHECK(application->chat_room->Generation() == gen_before);
}

void TestPresentationSnapshotFromModelGraph() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("host-uid");
  CommitHostJoin(*application);
  CommitSendChatMessage(*application->chat_room, *application->host_client,
                        "ff", 1'720'000'000'057LL);

  ChatPresentationOptions options;
  options.local_aether_uid = "host-uid";
  options.latency_ms_for_event = [](std::uint32_t) {
    return std::optional<double>{4.3};
  };
  auto snap =
      BuildChatPresentationSnapshot(*application->chat_room, options);
  CHECK(snap.feed.size() == 2);
  CHECK(snap.feed[0].display_line == "Host joined the chat");
  CHECK(snap.feed[1].sent_at_unix_ms == 1'720'000'000'057LL);
  CHECK(snap.feed[1].display_line.find("[") == 0);
  CHECK(snap.feed[1].display_line.find("] Host: ff") != std::string::npos);
  CHECK(snap.feed[1].display_line.find("[UI 4.3 ms]") != std::string::npos);
  CHECK(snap.feed[1].is_local_message);
  CHECK(snap.contacts.size() == 1);
  CHECK(snap.contacts[0].display_name == "Host");
}

void TestTimestampCommitAndRemap() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Client");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("client-uid");
  CommitHostJoin(*application);

  std::int64_t const sent_at = 1'700'000'123'456LL;
  auto event = CommitSendChatMessage(*application->chat_room,
                                     *application->host_client, "ping", sent_at);
  CHECK(event.is_valid());
  CHECK(event->sent_at_unix_ms == sent_at);
  CHECK(application->chat_room->feed.back()->sent_at_unix_ms == sent_at);
  CHECK(application->chat_room->feed.back()->source_event_obj_id ==
        event.id().id());

  auto payload = SerializeSharedEventPayload(*event);

  ae::RamDomainStorage remote_storage;
  ae::Domain remote_domain{ae::Now(), remote_storage};
  auto remote_app = BuildChatGraph(remote_domain, "Host");
  FinalizeDistilledGraph(*remote_app);
  remote_app->host_client->SetAetherUidText("host-uid");
  CommitHostJoin(*remote_app);
  auto remote_client = ChatClient::ptr::Create(ae::CreateWith{remote_domain});
  remote_client->SetAetherUidText("client-uid");
  auto name = ImmutableString::ptr::Create(ae::CreateWith{remote_domain});
  name->bytes = "Client";
  remote_client->display_name = name;
  CommitJoinChat(*remote_app->chat_room, *remote_client);

  auto remapped =
      RemapIncomingEvent(*remote_app->chat_room, remote_domain, payload);
  CHECK(remapped.is_valid());
  auto* message = dynamic_cast<ChatMessageEvent*>(&*remapped);
  CHECK(message != nullptr);
  CHECK(message->sent_at_unix_ms == sent_at);

  CHECK(remote_app->chat_room->CanApply(*message));
  remote_app->chat_room->Commit(remapped);
  CHECK(remote_app->chat_room->feed.back()->sent_at_unix_ms == sent_at);
}

void TestLegacyZeroTimestampHasNoFakeTime() {
  CHECK(FormatUnixMsLocalTime(0).empty());
  CHECK(FormatChatMessageDisplayLine("Host", "old", 0) == "Host: old");
}

void TestUiSendLatencyTracker() {
  UiSendLatencyTracker tracker;
  using Clock = UiSendLatencyTracker::Clock;
  auto const t0 = Clock::time_point{std::chrono::milliseconds{100}};
  auto const t1 = Clock::time_point{
      std::chrono::milliseconds{100} + std::chrono::microseconds{7400}};
  auto const trace1 = tracker.Begin(t0);
  tracker.BindEvent(trace1, 50);
  auto latency = tracker.ResolveForPresentation(50, t1);
  CHECK(latency.has_value());
  CHECK(std::abs(*latency - 7.4) < 0.05);
  auto again = tracker.ResolveForPresentation(
      50, Clock::time_point{std::chrono::milliseconds{500}});
  CHECK(again.has_value());
  CHECK(std::abs(*again - 7.4) < 0.05);

  auto const trace2 =
      tracker.Begin(Clock::time_point{std::chrono::milliseconds{200}});
  auto const trace3 =
      tracker.Begin(Clock::time_point{std::chrono::milliseconds{201}});
  tracker.BindEvent(trace2, 61);
  tracker.BindEvent(trace3, 62);
  auto l2 = tracker.ResolveForPresentation(
      61, Clock::time_point{std::chrono::milliseconds{205}});
  auto l3 = tracker.ResolveForPresentation(
      62, Clock::time_point{std::chrono::milliseconds{210}});
  CHECK(l2.has_value() && l3.has_value());
  CHECK(std::abs(*l2 - 5.0) < 0.05);
  CHECK(std::abs(*l3 - 9.0) < 0.05);
  CHECK(!tracker.ResolveForPresentation(999, t1).has_value());
}

void TestRemotePresencePollerNoOverlap() {
  RemotePresencePoller poller;
  poller.Monitor("peer-a", "self");
  poller.Monitor("self", "self");
  CHECK(poller.MonitoredCount() == 1);
  int starts = 0;
  auto now = RemotePresencePoller::Clock::time_point{};
  poller.Tick(now, [&](std::string const& uid) {
    CHECK(uid == "peer-a");
    ++starts;
    return true;
  });
  CHECK(starts == 1);
  CHECK(poller.HasOutstanding("peer-a"));
  poller.Tick(now + std::chrono::milliseconds{10},
              [&](std::string const&) {
                ++starts;
                return true;
              });
  CHECK(starts == 1);
  poller.OnQueryFinished("peer-a", now + std::chrono::milliseconds{20});
  poller.Tick(now + std::chrono::milliseconds{30},
              [&](std::string const&) {
                ++starts;
                return true;
              });
  CHECK(starts == 1);
  poller.Tick(now + std::chrono::seconds{1} + std::chrono::milliseconds{21},
              [&](std::string const&) {
                ++starts;
                return true;
              });
  CHECK(starts == 2);
}

void TestPeerOnlineAppliedBeforeJoin() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("host-uid");
  CommitHostJoin(*application);

  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "host-uid");
  EnsureSharedPeer(binding, "client-uid");
  SetSharedPeerOnline(binding, "client-uid", true);
  CHECK(binding.instance.FindPeer("client-uid")->online);

  auto client = ChatClient::ptr::Create(ae::CreateWith{domain});
  client->SetAetherUidText("client-uid");
  auto name = ImmutableString::ptr::Create(ae::CreateWith{domain});
  name->bytes = "Client";
  client->display_name = name;
  client->online = binding.instance.FindPeer("client-uid")->online;
  CommitJoinChat(*application->chat_room, *client);
  CHECK(application->chat_room->FindClientByAetherUid("client-uid")->online);
}

void TestOfflineRetrySkippedOnlineTriggersSend() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("host-uid");
  CommitHostJoin(*application);

  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "host-uid");
  EnsureSharedPeer(binding, "client-uid");
  auto* peer = binding.instance.FindPeer("client-uid");
  peer->channel_ready = true;
  peer->online = false;
  peer->pending.push_back(
      SharedEventId{.origin_uid = "host-uid", .origin_sequence = 1});

  int sends = 0;
  auto now = std::chrono::steady_clock::now();
  binding.runtime.Tick(binding.instance, now,
                       [&](PeerDeliveryState&, SharedEventId const&) {
                         ++sends;
                         return true;
                       });
  CHECK(sends == 1);
  CHECK(peer->in_flight.has_value());

  binding.runtime.Tick(binding.instance, now + std::chrono::seconds{2},
                       [&](PeerDeliveryState&, SharedEventId const&) {
                         ++sends;
                         return true;
                       });
  CHECK(sends == 1);

  SetSharedPeerOnline(binding, "client-uid", true);
  binding.runtime.Tick(binding.instance, now + std::chrono::seconds{2},
                       [&](PeerDeliveryState&, SharedEventId const&) {
                         ++sends;
                         return true;
                       });
  CHECK(sends == 2);
}

void TestPresenceScheduleMapping() {
  CHECK(OnlineFromPeerScheduleState(kPeerScheduleStateExpected));
  CHECK(!OnlineFromPeerScheduleState(kPeerScheduleStateMissedDeadline));
  CHECK(!OnlineFromPeerScheduleState(kPeerScheduleStateUnknown));
  CHECK(!OnlineFromQuerySuccess(false, kPeerScheduleStateExpected));
}

void TestSameStatusDoesNotBumpGeneration() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("host-uid");
  CommitHostJoin(*application);

  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "host-uid");
  auto client = ChatClient::ptr::Create(ae::CreateWith{domain});
  client->SetAetherUidText("client-uid");
  auto name = ImmutableString::ptr::Create(ae::CreateWith{domain});
  name->bytes = "Client";
  client->display_name = name;
  CommitJoinChat(*application->chat_room, *client);

  SetSharedPeerOnline(binding, "client-uid", true);
  auto const gen = client->Generation();
  SetSharedPeerOnline(binding, "client-uid", true);
  CHECK(client->Generation() == gen);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  using namespace apptraverse::test;
  TestSourceGuardNoMirrorOrHwnd();
  TestLocalChatHostJoinAndMessages();
  TestPresentationSnapshotFromModelGraph();
  TestTimestampCommitAndRemap();
  TestLegacyZeroTimestampHasNoFakeTime();
  TestUiSendLatencyTracker();
  TestRemotePresencePollerNoOverlap();
  TestPeerOnlineAppliedBeforeJoin();
  TestOfflineRetrySkippedOnlineTriggersSend();
  TestPresenceScheduleMapping();
  TestSameStatusDoesNotBumpGeneration();
  std::cout << "chat_presentation_headless_test OK\n";
  return 0;
}
