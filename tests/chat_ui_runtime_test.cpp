#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#include <cstdlib>
#endif

#include "aether/clock.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/distill.h"
#include "apptraverse/graph_mirror.h"
#include "apptraverse/model_runtime.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/ui_mirror.h"

#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_events.h"
#include "chat_identity_bar.h"
#include "chat_model.h"
#include "chat_presence.h"
#include "chat_shared.h"
#include "apptraverse/node.h"
#include "apptraverse/runtime_lifecycle.h"

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

void TestLocalChatHostJoinAndMessages() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CHECK(application->room.is_valid());
  CHECK(!application->local_client.is_valid());
  CHECK(application->network.is_valid());
  CHECK(application->room->clients.empty());
  CHECK(application->room->feed.empty());
  CHECK(application->aether->CurrentUid().empty());
  CHECK(!application->aether->IsRegisteredForCurrentRun());
  CHECK(application->network->GetAvailability() ==
        apptraverse::NetworkAvailability::kInitializing);

  CompleteLocalRegistration(*application, "test-uid");
  CHECK(application->room->clients.size() == 1);
  CHECK(application->room->clients[0].id().id() ==
        application->local_client.id().id());
  CHECK(application->room->feed.size() == 1);
  CHECK(application->room->feed[0]->kind == kChatFeedKindJoin);
  CHECK(FormatChatFeedLine(*application->room->feed[0]) ==
        "Nikolay joined the chat");

  CommitSendChatMessage(*application->room, *application->local_client,
                        "hello");
  CHECK(application->room->feed.size() == 2);
  CHECK(application->room->feed[1]->kind == kChatFeedKindMessage);
  CHECK(FormatChatFeedLine(*application->room->feed[1]) ==
        "Nikolay: hello");

  auto const gen_before = application->room->Generation();
  CommitSendChatMessage(*application->room, *application->local_client, "");
  CommitSendChatMessage(*application->room, *application->local_client,
                        "   \t");
  CHECK(application->room->feed.size() == 2);
  CHECK(application->room->Generation() == gen_before);

  CommitSendChatMessage(*application->room, *application->local_client,
                        "second");
  CommitSendChatMessage(*application->room, *application->local_client,
                        "third");
  CHECK(application->room->feed.size() == 4);
  CHECK(FormatChatFeedLine(*application->room->feed[2]) ==
        "Nikolay: second");
  CHECK(FormatChatFeedLine(*application->room->feed[3]) ==
        "Nikolay: third");
}

void TestLocalChatUiProjectionFromDomain() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "test-uid");

  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  auto ui_application = ChatApplication::ptr::MakeFromThis(
      static_cast<ChatApplication*>(ui_root.get()));
  CHECK(ui_application->room.is_valid());
  CHECK(ui_application->room->clients.size() == 1);
  CHECK(ui_application->room->feed.size() == 1);
  CHECK(FormatChatFeedLine(*ui_application->room->feed[0]) ==
        "Nikolay joined the chat");

  std::vector<UiApplyResult> applies;
  UiMirror* mirror_ptr = nullptr;
  UiMirror mirror{
      ui_domain, ui_storage,
      [&](std::uint32_t root_id, PublicationChannel<3>* channel) {
        applies.push_back(mirror_ptr->ApplyPublished(*channel, root_id));
      }};
  mirror_ptr = &mirror;
  ModelRuntime runtime{*application, mirror};
  runtime.AddPresentationRoot(*application->room);

  runtime.Post([&] {
    CommitSendChatMessage(*application->room, *application->local_client,
                          "hello");
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(applies.back().root_id == ToObjId(ChatObjId::ChatRoom));
  CHECK(ui_application->room->feed.size() == 2);
  CHECK(FormatChatFeedLine(*ui_application->room->feed[0]) ==
        "Nikolay joined the chat");
  {
    auto const model_line =
        FormatChatFeedLine(*application->room->feed[1]);
    auto const ui_line =
        FormatChatFeedLine(*ui_application->room->feed[1]);
    if (ui_line != "Nikolay: hello") {
      std::cerr << "model=[" << model_line << "] ui=[" << ui_line << "]\n";
      auto ui_item = ui_application->room->feed[1];
      ui_item.Load();
      std::cerr << "ui kind=" << ui_item->kind
                << " body_valid=" << ui_item->body.is_valid() << "\n";
      if (ui_item->body.is_valid()) {
        ui_item->body.Load();
        std::cerr << "body=[" << ui_item->body->bytes << "] len="
                  << ui_item->body->bytes.size() << "\n";
      }
    }
  }
  CHECK(FormatChatFeedLine(*ui_application->room->feed[1]) ==
        "Nikolay: hello");
  CHECK(ui_application->room->clients.size() == 1);
}

void TestLocalAetherUidModelToUiProjection() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  auto ui_application = ChatApplication::ptr::MakeFromThis(
      static_cast<ChatApplication*>(ui_root.get()));
  CHECK(ui_application->aether.is_valid());
  CHECK(ui_application->aether->CurrentUid().empty());

  std::vector<UiApplyResult> applies;
  UiMirror* mirror_ptr = nullptr;
  UiMirror mirror{
      ui_domain, ui_storage,
      [&](std::uint32_t root_id, PublicationChannel<3>* channel) {
        applies.push_back(mirror_ptr->ApplyPublished(*channel, root_id));
      }};
  mirror_ptr = &mirror;
  ModelRuntime runtime{*application, mirror};
  runtime.AddPresentationRoot(*application->aether);

  std::string const uid = "3ac93165-1111-2222-3333-444444444444";
  runtime.Post([&] {
    CommitAetherRegistrationCompleted(*application->aether, uid);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(applies.back().root_id ==
        ToObjId(ChatObjId::AetherRegistration));
  CHECK(ui_application->aether->CurrentUid() == uid);
  CHECK(application->aether->CurrentUid() == uid);
}

void TestChatNamedObjectClassIds() {
  using apptraverse::Event;
  using apptraverse::Node;
  CHECK(ChatClient::kClassId ==
        crc32::from_literal("chat::ChatClient").value);
  CHECK(ChatApplication::kClassId ==
        crc32::from_literal("chat::ChatApplication").value);
  CHECK(ChatRoom::kClassId ==
        crc32::from_literal("chat::ChatRoom").value);
  CHECK(ClientAddedEvent::kClassId ==
        crc32::from_literal("chat::ClientAddedEvent").value);
  CHECK(PresenceChangedEvent::kClassId ==
        crc32::from_literal("chat::PresenceChangedEvent").value);
  CHECK(PresenceMonitoringStartedEvent::kClassId ==
        crc32::from_literal("chat::PresenceMonitoringStartedEvent").value);
  CHECK(apptraverse::AetherRegistrationCompletedEvent::kClassId ==
        crc32::from_literal("apptraverse::AetherRegistrationCompletedEvent")
            .value);
  CHECK(ChatClient::kClassId != Node::kClassId);
  CHECK(ChatApplication::kClassId !=
        crc32::from_literal("apptraverse::Application").value);
  CHECK(ClientAddedEvent::kClassId != Event::kClassId);
}

void TestCreateOrLoadIgnoresCliWhenStateExists() {
  EnsureChatRegistration();
  auto dir = std::filesystem::temp_directory_path() /
             ("chat_create_or_load_" +
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()));
  ChatCreateOptions first;
  first.role = ChatRole::Host;
  first.display_name = "Original";
  auto runtime = CreateOrLoadChatModel(dir, first);
  CHECK(runtime.application->GetRole() == ChatRole::Host);
  CHECK(runtime.application->LocalDisplayNameBytes() == "Original");
  CHECK(!runtime.application->aether->IsRegisteredForCurrentRun());
  CHECK(runtime.application->network->GetAvailability() ==
        apptraverse::NetworkAvailability::kInitializing);
  CHECK(runtime.application->room->clients.empty());
  CHECK(!runtime.application->local_client.is_valid());

  ChatCreateOptions second;
  second.role = ChatRole::Client;
  second.display_name = "Ignored";
  auto loaded = CreateOrLoadChatModel(dir, second);
  CHECK(loaded.application->GetRole() == ChatRole::Host);
  CHECK(loaded.application->LocalDisplayNameBytes() == "Original");
  std::filesystem::remove_all(dir);
}

void TestLocalPresenceScheduleStateMapping() {
  CHECK(PresenceFromLocalDiag(false, false) == PresenceState::kUnknown);
  CHECK(PresenceFromLocalDiag(true, true) == PresenceState::kOnline);
  CHECK(PresenceFromLocalDiag(true, false) == PresenceState::kOffline);
}

void TestContactPresencePresentationGlyphs() {
  CHECK(ContactPresencePrefix(PresenceState::kOnline) == L"\u25CF ");
  CHECK(ContactPresencePrefix(PresenceState::kOffline) == L"\u25CB ");
  CHECK(ContactPresencePrefix(PresenceState::kUnknown) == L"? ");
  CHECK(FormatContactPresenceLabel(PresenceState::kOnline, L"Nikolay") ==
        L"\u25CF Nikolay");
  CHECK(FormatContactPresenceLabel(PresenceState::kOffline, L"Nikolay") ==
        L"\u25CB Nikolay");
  CHECK(ContactPresencePrefix(PresenceState::kConnecting) == L"\u25CC ");
  CHECK(FormatContactPresenceLabel(PresenceState::kConnecting, L"Nikolay") ==
        L"\u25CC Nikolay");
}

void TestHostOnlineModelToUiProjection() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "test-uid");
  CHECK(application->local_client->GetPresence() == PresenceState::kConnecting);

  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  auto ui_application = ChatApplication::ptr::MakeFromThis(
      static_cast<ChatApplication*>(ui_root.get()));

  std::vector<UiApplyResult> applies;
  UiMirror* mirror_ptr = nullptr;
  UiMirror mirror{
      ui_domain, ui_storage,
      [&](std::uint32_t root_id, PublicationChannel<3>* channel) {
        applies.push_back(mirror_ptr->ApplyPublished(*channel, root_id));
      }};
  mirror_ptr = &mirror;
  ModelRuntime runtime{*application, mirror};
  runtime.AddPresentationRoot(*application->room);
  runtime.AttachNode(*application->local_client, *application->room);

  runtime.Post([&] {
    CommitPresenceChanged(*application->local_client, PresenceState::kOnline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(applies.back().root_id == ToObjId(ChatObjId::ChatRoom));
  auto const generation_online = application->local_client->Generation();
  CHECK(application->local_client->GetPresence() == PresenceState::kOnline);
  CHECK(ui_application->room->clients[0]->GetPresence() ==
        PresenceState::kOnline);

  applies.clear();
  runtime.Post([&] {
    CommitPresenceChanged(*application->local_client, PresenceState::kOnline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(application->local_client->Generation() == generation_online);
  CHECK(applies.empty());

  runtime.Post([&] {
    CommitPresenceChanged(*application->local_client, PresenceState::kOffline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(application->local_client->Generation() > generation_online);
  CHECK(application->local_client->GetPresence() == PresenceState::kOffline);
  CHECK(ui_application->room->clients[0]->GetPresence() ==
        PresenceState::kOffline);

  auto const generation_offline = application->local_client->Generation();
  runtime.Post([&] {
    CommitPresenceChanged(*application->local_client, PresenceState::kUnknown);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(application->local_client->Generation() > generation_offline);
  CHECK(ui_application->room->clients[0]->GetPresence() ==
        PresenceState::kUnknown);

  runtime.Post([&] {
    CommitPresenceChanged(*application->local_client, PresenceState::kOnline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(application->local_client->GetPresence() == PresenceState::kOnline);
  CHECK(ui_application->room->clients[0]->GetPresence() ==
        PresenceState::kOnline);
}

void TestPresenterTracksNestedClientGeneration() {
#ifdef CHAT_UI_RUNTIME_DEMO_SOURCE_DIR
  std::ifstream in{std::filesystem::path{CHAT_UI_RUNTIME_DEMO_SOURCE_DIR} /
                   "windows/win_presenters.h"};
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  CHECK(text.find("last_client_generations_") != std::string::npos);
  CHECK(text.find("SyncClientGenerations") != std::string::npos);
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

void TestPresenceChangedEventIsJournaled() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CompleteLocalRegistration(*application, "test-uid");
  CHECK(application->local_client->GetPresence() == PresenceState::kConnecting);
  CHECK(application->local_client->journal.size() == 1);
  CHECK(CommitPresenceChanged(*application->local_client,
                              PresenceState::kOnline));
  CHECK(application->local_client->GetPresence() == PresenceState::kOnline);
  CHECK(application->local_client->journal.size() == 2);
  application->local_client->ReplayFromBase();
  CHECK(application->local_client->GetPresence() == PresenceState::kOnline);
}

void TestAetherPinMatchesExpectedSha() {
  CHECK(std::string{APPTRAVERSE_AETHER_EXPECTED_SHA} ==
        "0b0e3b54b9ffa730c41597c8b18f6a75255bded3");
#ifdef CHAT_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const version_cmake =
      std::filesystem::path{CHAT_UI_RUNTIME_DEMO_SOURCE_DIR}
          .parent_path()
          .parent_path() /
      "cmake" / "aether_version.cmake";
  CHECK(std::filesystem::exists(version_cmake));
  std::ifstream in{version_cmake};
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  CHECK(text.find("0b0e3b54b9ffa730c41597c8b18f6a75255bded3") !=
        std::string::npos);
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

void TestAetherRxScheduleConfiguredInRuntime() {
#ifdef CHAT_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const runtime_cpp =
      std::filesystem::path{CHAT_UI_RUNTIME_DEMO_SOURCE_DIR} / "windows" /
      "aether_runtime.cpp";
  CHECK(std::filesystem::exists(runtime_cpp));
  std::ifstream in{runtime_cpp};
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  CHECK(text.find("DiagnoseLocalPresence") != std::string::npos);
  CHECK(text.find("EnableLocalPresenceMonitoring") != std::string::npos);
  CHECK(text.find("ConfigureRxTimings") != std::string::npos);
  CHECK(text.find("LOCAL_PRESENCE state=") != std::string::npos);
  CHECK(text.find("IsLocallyOnline") == std::string::npos);
  CHECK(text.find("QueryPeerPresence") == std::string::npos);
  CHECK(text.find("MonitorPeerPresence") == std::string::npos);
  CHECK(text.find("RemotePresencePoller") == std::string::npos);
  CHECK(text.find("SetReceiveSchedule") == std::string::npos);
  CHECK(text.find("QueryPeerReceiveSchedule") == std::string::npos);
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

void TestAetherPresenceQueryOnlyOnAetherThread() {
#ifdef CHAT_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const root{CHAT_UI_RUNTIME_DEMO_SOURCE_DIR};
  for (auto const& rel :
       {std::filesystem::path{"windows/win_app.cpp"},
        std::filesystem::path{"windows/win_presenters.h"},
        std::filesystem::path{"common/chat_commands.h"},
        std::filesystem::path{"common/chat_model.h"}}) {
    std::ifstream in{root / rel};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("QueryPeerPresence") == std::string::npos);
    CHECK(text.find("DiagnoseLocalPresence") == std::string::npos);
  }
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

void TestApplicationRoleModelToUiProjection() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CHECK(application->GetRole() == ChatRole::Host);

  SetApplicationRole(*application, ChatRole::Client);
  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  auto ui_application = ChatApplication::ptr::MakeFromThis(
      static_cast<ChatApplication*>(ui_root.get()));
  CHECK(ui_application->GetRole() == ChatRole::Client);
  CHECK(application->GetRole() == ChatRole::Client);
}

void TestConnectToHostCommandRegistersPeer() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "local-client-uid");
  std::string const host_uid = "83df0bb1-08ac-45f8-8003-8eeb7fa8f425";
  int open_count = 0;
  ConnectToHostCommand(binding, host_uid,
                       [&](std::string const& uid) {
                         CHECK(uid == host_uid);
                         ++open_count;
                       });
  CHECK(open_count == 1);
  CHECK(binding.instance.shared_room_id == host_uid);
  CHECK(binding.instance.peers.size() == 1);
  CHECK(binding.instance.peers[0].remote_aether_uid == host_uid);
  CHECK(!binding.instance.peers[0].channel_ready);
}

void TestIdentityBarPresenterStructure() {
#ifdef CHAT_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const root{CHAT_UI_RUNTIME_DEMO_SOURCE_DIR};
  {
    std::ifstream in{root / "windows/win_identity_bar_presenter.h"};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("WinIdentityBarPresenter") != std::string::npos);
    CHECK(text.find("Aether ID:") != std::string::npos);
    CHECK(text.find("L\"Copy\"") != std::string::npos);
    CHECK(text.find("L\"Join room\"") != std::string::npos);
    CHECK(text.find("JOIN_ROOM unimplemented") != std::string::npos);
    CHECK(text.find("status_hwnd_") == std::string::npos);
    CHECK(text.find("L\"Registered\"") == std::string::npos);
    CHECK(text.find("ChatConnectionUiStatus") == std::string::npos);
    CHECK(text.find("Not connected") == std::string::npos);
    CHECK(text.find("TryConnectHost") == std::string::npos);
    CHECK(text.find("CopyWideTextToClipboard") != std::string::npos);
  }
  {
    std::ifstream in{root / "windows/win_presenters.h"};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("WinIdentityBarPresenter identity_bar") !=
          std::string::npos);
    CHECK(text.find("WinConnectionBarPresenter") == std::string::npos);
    CHECK(text.find("kChatConnectionBarHeight") != std::string::npos);
    CHECK(text.find("L\"Registered\"") == std::string::npos);
    CHECK(text.find("aether_label_hwnd") == std::string::npos);
    CHECK(text.find("connect_hwnd") == std::string::npos);
  }
  {
    std::ifstream in{root / "windows/main.cpp"};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("--host") != std::string::npos);
    CHECK(text.find("--client") != std::string::npos);
    CHECK(text.find("cannot be used together") != std::string::npos);
    CHECK(text.find("--state-dir") != std::string::npos);
    CHECK(text.find("--connect-host-uid") == std::string::npos);
    CHECK(text.find("--monitor-peer-uid") == std::string::npos);
  }
  {
    std::ifstream in{root / "windows/win_app.cpp"};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("CompleteLocalRegistration") != std::string::npos);
    CHECK(text.find("BeginCurrentRun") != std::string::npos);
    CHECK(text.find("ConnectToHostCommand") == std::string::npos);
    CHECK(text.find("AddPresencePeer") == std::string::npos);
    CHECK(text.find("CreateOrLoadChatModel") != std::string::npos);
    CHECK(text.find("monitor_peer_uid.txt") == std::string::npos);
  }
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

void TestIdentityBarProjection() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  BeginCurrentRun(*application);
  auto host = ProjectIdentityBar(ChatRole::Host, *application->network,
                                 *application->aether);
  CHECK(host.field_text == kIdentityBarRegistering);
  CHECK(host.field_readonly);
  CHECK(host.copy_visible);
  CHECK(!host.copy_enabled);
  CHECK(!host.join_visible);

  auto client = ProjectIdentityBar(ChatRole::Client, *application->network,
                                   *application->aether);
  CHECK(client.field_text == kIdentityBarRegistering);
  CHECK(client.field_readonly);
  CHECK(!client.copy_visible);
  CHECK(client.join_visible);
  CHECK(!client.join_enabled);

  CHECK(CommitNetworkInterfaceUnavailable(*application->network,
                                          application->runtime->run_id));
  host = ProjectIdentityBar(ChatRole::Host, *application->network,
                            *application->aether);
  CHECK(host.field_text == kIdentityBarNoInterface);
  CHECK(!host.copy_enabled);

  CHECK(CommitInternetUnavailable(*application->network,
                                  application->runtime->run_id));
  host = ProjectIdentityBar(ChatRole::Host, *application->network,
                            *application->aether);
  CHECK(host.field_text == kIdentityBarNoInternet);

  CompleteLocalRegistration(*application, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
  host = ProjectIdentityBar(ChatRole::Host, *application->network,
                            *application->aether);
  CHECK(host.field_text == "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
  CHECK(host.copy_enabled);
  CHECK(host.field_readonly);
  client = ProjectIdentityBar(ChatRole::Client, *application->network,
                              *application->aether);
  CHECK(client.field_text.empty());
  CHECK(!client.field_readonly);
  CHECK(client.join_enabled);
  CHECK(client.show_edit_cue);
  CHECK(application->room->journal.size() == 1);
  CHECK(application->local_client->GetPresence() ==
        PresenceState::kConnecting);
}

void TestSecondLaunchStartsRegistering() {
  EnsureChatRegistration();
  auto dir = std::filesystem::temp_directory_path() /
             ("chat_second_run_" +
              std::to_string(
                  std::chrono::steady_clock::now().time_since_epoch().count()));
  ChatCreateOptions options;
  options.role = ChatRole::Host;
  options.display_name = "Host";
  auto first = CreateOrLoadChatModel(dir, options);
  BeginCurrentRun(*first.application);
  CompleteLocalRegistration(*first.application, "host-uid");
  CHECK(first.application->aether->IsRegisteredForCurrentRun());
  CHECK(first.application->local_client->GetPresence() ==
        PresenceState::kConnecting);
  CHECK(CommitPresenceChanged(*first.application->local_client,
                              PresenceState::kOnline));
  apptraverse::SaveDistilledRoot(*first.application);  // runtime-save-ok: test

  auto second = LoadChatModel(dir);
  CHECK(second.application->aether->uid == "host-uid");
  CHECK(second.application->room->clients.size() == 1);
  BeginCurrentRun(*second.application);
  CHECK(!second.application->aether->IsRegisteredForCurrentRun());
  CHECK(second.application->aether->CurrentUid().empty());
  auto view = ProjectIdentityBar(ChatRole::Host, *second.application->network,
                                 *second.application->aether);
  CHECK(view.field_text == kIdentityBarRegistering);
  CHECK(!view.copy_enabled);
  CHECK(second.application->local_client->GetPresence() ==
        PresenceState::kConnecting);
  std::filesystem::remove_all(dir);
}

void TestApplyPresenceOverlayUnchangedReturnsZero() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  CreateUnjoinedLocalClient(*application, "host-uid");

  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "host-uid");
  CommitLocalJoin(binding, *application->local_client);
  SetLocalPresenceObservation(binding, PresenceState::kOnline);
  CHECK(ApplyPresenceOverlay(binding) == 0);
  CHECK(!SetLocalPresenceObservation(binding, PresenceState::kOnline));
}

void TestNoManualSerializersOrRuntimeClasses() {
#ifdef CHAT_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const root{CHAT_UI_RUNTIME_DEMO_SOURCE_DIR};
  CHECK(std::filesystem::exists(root));
  std::vector<std::string> forbidden = {
      "WriteUiState",       "ReadRuntime",        "RuntimeWindow",
      "RuntimeTextToolbar", "RuntimeColorToolbar", "RuntimeChat",
      "RuntimeObject",      "UiRuntimeRegistry",   "PayloadArchive",
      "ImmutableObjectStore", "ConstRef",          "AddMessageEvent",
      "AddMessageCommand",  "last_pub_a_",         "last_pub_b_",
      "window_b_",          "CaptureBaseState(",   "text_id",
      "kUiSubgraphMagic",   "0x41545549",          "kReuseObject",
      "ReuseObjectRecord",  "reused_obj_ids",      "EnsureUiObject",
      "UiRecordKind",       "MaterializedOps",     "MaterializedOpsRegistrar",
      "SaveMaterializedField", "LoadMaterializedField",
      "RegisterMaterializedOps", "FindMaterializedOps",
      "APPTRAVERSE_REGISTER_MATERIALIZED", "materialized_ops.h",
      "ui_materialized.h",  "SerializeMaterializedObject",
      "DeserializeMaterializedObject", "EagerLoadReachable",
      "WinCenterStripPresenter", "PaintWindow", "LayoutWindow",
      "CenterStrip", "ColorToolbar"};
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    auto const ext = entry.path().extension().string();
    if (ext != ".h" && ext != ".cpp") {
      continue;
    }
    std::ifstream in{entry.path()};
    std::string line;
    while (std::getline(in, line)) {
      for (auto const& token : forbidden) {
        CHECK(line.find(token) == std::string::npos);
      }
      auto const pos = line.find(".Save()");
      if (pos != std::string::npos) {
        CHECK(line.find("runtime-save-ok") != std::string::npos);
      }
    }
  }
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

}  // namespace apptraverse::test

int main() {
#if defined(_MSC_VER)
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
  using apptraverse::test::TestApplicationRoleModelToUiProjection;
  using apptraverse::test::TestAetherPinMatchesExpectedSha;
  using apptraverse::test::TestAetherPresenceQueryOnlyOnAetherThread;
  using apptraverse::test::TestAetherRxScheduleConfiguredInRuntime;
  using apptraverse::test::TestConnectToHostCommandRegistersPeer;
  using apptraverse::test::TestIdentityBarPresenterStructure;
  using apptraverse::test::TestIdentityBarProjection;
  using apptraverse::test::TestSecondLaunchStartsRegistering;
  using apptraverse::test::TestChatNamedObjectClassIds;
  using apptraverse::test::TestCreateOrLoadIgnoresCliWhenStateExists;
  using apptraverse::test::TestHostOnlineModelToUiProjection;
  using apptraverse::test::TestLocalAetherUidModelToUiProjection;
  using apptraverse::test::TestLocalChatHostJoinAndMessages;
  using apptraverse::test::TestLocalChatUiProjectionFromDomain;
  using apptraverse::test::TestLocalPresenceScheduleStateMapping;
  using apptraverse::test::TestContactPresencePresentationGlyphs;
  using apptraverse::test::TestPresenterTracksNestedClientGeneration;
  using apptraverse::test::TestPresenceChangedEventIsJournaled;
  using apptraverse::test::TestApplyPresenceOverlayUnchangedReturnsZero;
  using apptraverse::test::TestNoManualSerializersOrRuntimeClasses;
  TestLocalChatHostJoinAndMessages();
  TestLocalChatUiProjectionFromDomain();
  TestLocalAetherUidModelToUiProjection();
  TestApplicationRoleModelToUiProjection();
  TestConnectToHostCommandRegistersPeer();
  TestChatNamedObjectClassIds();
  TestCreateOrLoadIgnoresCliWhenStateExists();
  TestLocalPresenceScheduleStateMapping();
  TestContactPresencePresentationGlyphs();
  TestHostOnlineModelToUiProjection();
  TestApplyPresenceOverlayUnchangedReturnsZero();
  TestIdentityBarProjection();
  TestSecondLaunchStartsRegistering();
  TestPresenterTracksNestedClientGeneration();
  TestIdentityBarPresenterStructure();
  TestPresenceChangedEventIsJournaled();
  TestAetherPinMatchesExpectedSha();
  TestAetherRxScheduleConfiguredInRuntime();
  TestAetherPresenceQueryOnlyOnAetherThread();
  TestNoManualSerializersOrRuntimeClasses();
  std::cout << "chat_ui_runtime_test OK\n";
  return 0;
}
