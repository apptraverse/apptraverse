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
#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/runtime_node.h"

#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_events.h"
#include "chat_identity_bar.h"
#include "chat_model.h"
#include "chat_presentation.h"
#include "chat_presence.h"
#include "chat_presence_overlay.h"
#include "ui_send_latency_tracker.h"

namespace apptraverse::test {
using namespace apptraverse;
using namespace chat;

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
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "test-uid");
  CHECK(FormatChatFeedLine(*application->room->feed[0]) ==
        "Nikolay joined the chat");

  CommitSendChatMessage(*application->room, *application->local_client,
                        "hello");
  CHECK(FormatChatFeedLine(*application->room->feed[1]) ==
        "Nikolay: hello");

  auto const gen_before = application->room->Generation();
  CHECK(!CommitSendChatMessage(*application->room,
                               *application->local_client, "")
             .is_valid());
  CHECK(application->room->Generation() == gen_before);
}

void TestPresentationSnapshotFromModelGraph() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "host-uid");
  CommitSendChatMessage(*application->room, *application->local_client,
                        "ff", 1'720'000'000'057LL);

  ChatPresentationOptions options;
  options.local_aether_uid = "host-uid";
  options.latency_ms_for_event = [](std::uint32_t) {
    return std::optional<double>{4.3};
  };
  auto snap =
      BuildChatPresentationSnapshot(*application->room, options);
  CHECK(snap.feed.size() == 2);
  CHECK(snap.feed[0].display_line == "Host joined the chat");
  CHECK(snap.feed[1].sent_at_unix_ms == 1'720'000'000'057LL);
  CHECK(snap.feed[1].display_line.find("[") == 0);
  CHECK(snap.feed[1].display_line.find("] Host: ff") != std::string::npos);
  CHECK(snap.feed[1].display_line.find("[UI 4.3 ms]") != std::string::npos);
  CHECK(snap.feed[1].is_local_message);
  CHECK(snap.contacts.size() == 1);
  CHECK(snap.contacts[0].display_name == "Host");
  CHECK(snap.contacts[0].is_local);
  CHECK(snap.contacts[0].aether_uid == "host-uid");
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

void TestPresenceTriStateMapping() {
  CHECK(PresenceFromLocalDiag(false, false) == PresenceState::kUnknown);
  CHECK(PresenceFromLocalDiag(true, true) == PresenceState::kOnline);
  CHECK(PresenceFromLocalDiag(true, false) == PresenceState::kOffline);
  CHECK(PresenceIsOnline(PresenceState::kOnline));
  CHECK(!PresenceIsOnline(PresenceState::kOffline));
  CHECK(!PresenceIsOnline(PresenceState::kUnknown));
}

void TestLocalSelfSameStatusDoesNotBumpGeneration() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "host-uid");

  auto& client = *application->local_client;
  CommitPresenceChanged(client, PresenceState::kOnline);
  auto const gen = client.Generation();
  CommitPresenceChanged(client, PresenceState::kOnline);
  CHECK(client.Generation() == gen);

  auto const gen_online = client.Generation();
  CommitPresenceChanged(client, PresenceState::kOffline);
  CHECK(client.Generation() > gen_online);

  auto const gen_offline = client.Generation();
  CommitPresenceChanged(client, PresenceState::kUnknown);
  CHECK(client.Generation() > gen_offline);
  auto const gen_unknown = client.Generation();
  CommitPresenceChanged(client, PresenceState::kOnline);
  CHECK(client.Generation() > gen_unknown);
}

void TestContactsLocalFirstFromClientsOnly() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");

  auto client = ChatClient::ptr::Create(ae::CreateWith{domain});
  client->SetAetherUidText("11111111-2222-3333-4444-555555555555");
  auto name = ImmutableString::ptr::Create(ae::CreateWith{domain});
  name->bytes = "Client";
  client->display_name = name;
  apptraverse::InitializeRuntimeNode(*client);
  CommitClientAdded(*application->room, *client);
  static_cast<void>(
      CommitPresenceChanged(*application->local_client, PresenceState::kOnline));
  client->SetPresence(PresenceState::kOffline);

  ChatPresentationOptions host_opts;
  host_opts.local_aether_uid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
  auto host_snap =
      BuildChatPresentationSnapshot(*application->room, host_opts);
  CHECK(host_snap.contacts.size() == 2);
  CHECK(host_snap.contacts[0].is_local);
  CHECK(host_snap.contacts[0].display_name == "Host");
  CHECK(host_snap.contacts[0].aether_uid == host_opts.local_aether_uid);
  CHECK(host_snap.contacts[0].presence == PresenceState::kOnline);
  CHECK(host_snap.contacts[1].is_local == false);
  CHECK(host_snap.contacts[1].display_name == "Client");
  CHECK(host_snap.contacts[1].presence == PresenceState::kOffline);
  CHECK(FormatContactPresenceLabel(PresenceState::kOnline, L"Host")
            .find(L"\u25CF") == 0);
  CHECK(FormatContactPresenceLabel(PresenceState::kOffline, L"Client")
            .find(L"\u25CB") == 0);
  CHECK(FormatContactPresenceLabel(PresenceState::kUnknown, L"X").find(L"?") ==
        0);

  ChatPresentationOptions client_opts;
  client_opts.local_aether_uid = "11111111-2222-3333-4444-555555555555";
  auto client_snap =
      BuildChatPresentationSnapshot(*application->room, client_opts);
  CHECK(client_snap.contacts.size() == 2);
  CHECK(client_snap.contacts[0].is_local);
  CHECK(client_snap.contacts[0].display_name == "Client");
  CHECK(client_snap.contacts[1].is_local == false);
  CHECK(client_snap.contacts[1].display_name == "Host");
}

void TestPresenceOverlaySurvivesOnlineClear() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "host-uid");

  ChatPresenceOverlay overlay;
  overlay.SetLocalSelf(PresenceState::kOnline);
  overlay.ApplyToRoom(*application->room, "host-uid");
  CHECK(application->local_client->GetPresence() == PresenceState::kOnline);

  // Simulate journal rebuild wiping presence presentation cache.
  application->local_client->SetPresence(PresenceState::kUnknown);
  overlay.ApplyToRoom(*application->room, "host-uid");
  CHECK(application->local_client->GetPresence() == PresenceState::kOnline);
}

void TestNewChatClientStartsUnknown() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto client = ChatClient::ptr::Create(ae::CreateWith{domain});
  CHECK(client->GetPresence() == PresenceState::kUnknown);
}

void TestReplayKeepsJournaledPresence() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "test-uid");
  CHECK(application->local_client->GetPresence() == PresenceState::kConnecting);
  CHECK(CommitPresenceChanged(*application->local_client,
                              PresenceState::kOnline));
  application->local_client->ReplayFromBase();
  CHECK(application->local_client->GetPresence() == PresenceState::kOnline);
  CHECK(application->room->clients[0]->GetPresence() ==
        PresenceState::kOnline);
}

void TestPresenceOverlayApplyUnchangedReturnsZero() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "host-uid");

  ChatPresenceOverlay overlay;
  CHECK(overlay.SetLocalSelf(PresenceState::kOnline));
  CHECK(overlay.ApplyToRoom(*application->room, "host-uid") == 1);
  CHECK(overlay.ApplyToRoom(*application->room, "host-uid") == 0);
  CHECK(!overlay.SetLocalSelf(PresenceState::kOnline));
  CHECK(overlay.ApplyToRoom(*application->room, "host-uid") == 0);
}

void TestIdentityBarProjectionHeadless() {
  CHECK(LooksLikeAetherUid("3ac93165-3d37-4970-87a6-fa4ee27744e4"));
  CHECK(!LooksLikeAetherUid(""));
  CHECK(!LooksLikeAetherUid("not-a-uid"));
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  BeginCurrentRun(*application);
  auto view = ProjectIdentityBar(ChatRole::Host, *application->network,
                                 *application->aether);
  CHECK(view.field_text == kIdentityBarRegistering);
  CHECK(view.copy_visible);
  CHECK(!view.copy_enabled);
  CHECK(!view.join_visible);
  view = ProjectIdentityBar(ChatRole::Client, *application->network,
                            *application->aether);
  CHECK(view.field_text == kIdentityBarRegistering);
  CHECK(!view.copy_visible);
  CHECK(view.join_visible);
  CHECK(!view.join_enabled);
  CHECK(FeedListWasAtBottom(0, 10, 5));
  CHECK(FeedListWasAtBottom(4, 1, 5));
  CHECK(!FeedListWasAtBottom(0, 2, 10));
}

}  // namespace apptraverse::test

int main() {
  using namespace apptraverse::test;
  try {
    TestSourceGuardNoMirrorOrHwnd();
    TestLocalChatHostJoinAndMessages();
    TestPresentationSnapshotFromModelGraph();
    TestLegacyZeroTimestampHasNoFakeTime();
    TestUiSendLatencyTracker();
    TestPresenceTriStateMapping();
    TestLocalSelfSameStatusDoesNotBumpGeneration();
    TestContactsLocalFirstFromClientsOnly();
    TestPresenceOverlaySurvivesOnlineClear();
    TestNewChatClientStartsUnknown();
    TestReplayKeepsJournaledPresence();
    TestPresenceOverlayApplyUnchangedReturnsZero();
    TestIdentityBarProjectionHeadless();
    std::cout << "chat_presentation_headless_test OK\n";
    return 0;
  } catch (std::exception const& ex) {
    std::cerr << "exception: " << ex.what() << '\n';
    return 2;
  } catch (...) {
    std::cerr << "unknown exception\n";
    return 3;
  }
}
