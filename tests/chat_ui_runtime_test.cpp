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
#include "chat_ids.h"
#include "chat_model.h"
#include "chat_presence.h"
#include "chat_shared.h"

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

void TestLocalChatHostJoinAndMessages() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CHECK(application->chat_room.is_valid());
  CHECK(application->host_client.is_valid());
  CHECK(application->local_aether.is_valid());
  CHECK(application->chat_room->clients.empty());
  CHECK(application->chat_room->feed.empty());
  CHECK(application->local_aether->UidTextBytes() == "...");

  CommitHostJoin(*application);
  CHECK(application->chat_room->clients.size() == 1);
  CHECK(application->chat_room->clients[0].id().id() ==
        application->host_client.id().id());
  CHECK(application->chat_room->feed.size() == 1);
  CHECK(application->chat_room->feed[0]->kind == kChatFeedKindJoin);
  CHECK(FormatChatFeedLine(*application->chat_room->feed[0]) ==
        "Nikolay joined the chat");

  CommitSendChatMessage(*application->chat_room, *application->host_client,
                        "hello");
  CHECK(application->chat_room->feed.size() == 2);
  CHECK(application->chat_room->feed[1]->kind == kChatFeedKindMessage);
  CHECK(FormatChatFeedLine(*application->chat_room->feed[1]) ==
        "Nikolay: hello");

  auto const gen_before = application->chat_room->Generation();
  CommitSendChatMessage(*application->chat_room, *application->host_client, "");
  CommitSendChatMessage(*application->chat_room, *application->host_client,
                        "   \t");
  CHECK(application->chat_room->feed.size() == 2);
  CHECK(application->chat_room->Generation() == gen_before);

  CommitSendChatMessage(*application->chat_room, *application->host_client,
                        "second");
  CommitSendChatMessage(*application->chat_room, *application->host_client,
                        "third");
  CHECK(application->chat_room->feed.size() == 4);
  CHECK(FormatChatFeedLine(*application->chat_room->feed[2]) ==
        "Nikolay: second");
  CHECK(FormatChatFeedLine(*application->chat_room->feed[3]) ==
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
  CommitHostJoin(*application);

  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  auto ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  CHECK(ui_application->chat_room.is_valid());
  CHECK(ui_application->chat_room->clients.size() == 1);
  CHECK(ui_application->chat_room->feed.size() == 1);
  CHECK(FormatChatFeedLine(*ui_application->chat_room->feed[0]) ==
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
  runtime.AddPresentationRoot(*application->chat_room);

  runtime.Post([&] {
    CommitSendChatMessage(*application->chat_room, *application->host_client,
                          "hello");
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(applies.back().root_id == chat::ToObjId(chat::ChatObjId::ChatRoom));
  CHECK(ui_application->chat_room->feed.size() == 2);
  CHECK(FormatChatFeedLine(*ui_application->chat_room->feed[0]) ==
        "Nikolay joined the chat");
  {
    auto const model_line =
        FormatChatFeedLine(*application->chat_room->feed[1]);
    auto const ui_line =
        FormatChatFeedLine(*ui_application->chat_room->feed[1]);
    if (ui_line != "Nikolay: hello") {
      std::cerr << "model=[" << model_line << "] ui=[" << ui_line << "]\n";
      auto ui_item = ui_application->chat_room->feed[1];
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
  CHECK(FormatChatFeedLine(*ui_application->chat_room->feed[1]) ==
        "Nikolay: hello");
  CHECK(ui_application->chat_room->clients.size() == 1);
}

void TestLocalAetherUidModelToUiProjection() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CommitHostJoin(*application);

  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  auto ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
  CHECK(ui_application->local_aether.is_valid());
  CHECK(ui_application->local_aether->UidTextBytes() == "...");

  std::vector<UiApplyResult> applies;
  UiMirror* mirror_ptr = nullptr;
  UiMirror mirror{
      ui_domain, ui_storage,
      [&](std::uint32_t root_id, PublicationChannel<3>* channel) {
        applies.push_back(mirror_ptr->ApplyPublished(*channel, root_id));
      }};
  mirror_ptr = &mirror;
  ModelRuntime runtime{*application, mirror};
  runtime.AddPresentationRoot(*application->local_aether);

  std::string const uid = "3ac93165-1111-2222-3333-444444444444";
  runtime.Post([&] {
    SetLocalAetherUidText(*application->local_aether, uid);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(applies.back().root_id ==
        chat::ToObjId(chat::ChatObjId::LocalAetherIdentity));
  CHECK(ui_application->local_aether->UidTextBytes() == uid);
  CHECK(application->local_aether->UidTextBytes() == uid);
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
  CHECK(FormatContactPresenceLabel(PresenceState::kUnknown, L"Nikolay") ==
        L"? Nikolay");
}

void TestHostOnlineModelToUiProjection() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{model_storage};
  ae::Domain ui_domain{ui_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CommitHostJoin(*application);
  CHECK(application->host_client->GetPresence() == PresenceState::kUnknown);

  auto ui_root =
      CopyModelGraphToUiDomain(*application, ui_domain, ui_storage);
  auto ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));

  std::vector<UiApplyResult> applies;
  UiMirror* mirror_ptr = nullptr;
  UiMirror mirror{
      ui_domain, ui_storage,
      [&](std::uint32_t root_id, PublicationChannel<3>* channel) {
        applies.push_back(mirror_ptr->ApplyPublished(*channel, root_id));
      }};
  mirror_ptr = &mirror;
  ModelRuntime runtime{*application, mirror};
  runtime.AddPresentationRoot(*application->chat_room);
  runtime.AttachNode(*application->host_client, *application->chat_room);

  runtime.Post([&] {
    SetHostClientPresence(*application->host_client, PresenceState::kOnline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(applies.back().root_id == chat::ToObjId(chat::ChatObjId::ChatRoom));
  auto const generation_online = application->host_client->Generation();
  CHECK(application->host_client->GetPresence() == PresenceState::kOnline);
  CHECK(ui_application->chat_room->clients[0]->GetPresence() ==
        PresenceState::kOnline);

  applies.clear();
  runtime.Post([&] {
    SetHostClientPresence(*application->host_client, PresenceState::kOnline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(application->host_client->Generation() == generation_online);
  CHECK(applies.empty());

  runtime.Post([&] {
    SetHostClientPresence(*application->host_client, PresenceState::kOffline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(application->host_client->Generation() > generation_online);
  CHECK(application->host_client->GetPresence() == PresenceState::kOffline);
  CHECK(ui_application->chat_room->clients[0]->GetPresence() ==
        PresenceState::kOffline);

  auto const generation_offline = application->host_client->Generation();
  runtime.Post([&] {
    SetHostClientPresence(*application->host_client, PresenceState::kUnknown);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(application->host_client->Generation() > generation_offline);
  CHECK(ui_application->chat_room->clients[0]->GetPresence() ==
        PresenceState::kUnknown);

  runtime.Post([&] {
    SetHostClientPresence(*application->host_client, PresenceState::kOnline);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(application->host_client->GetPresence() == PresenceState::kOnline);
  CHECK(ui_application->chat_room->clients[0]->GetPresence() ==
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

void TestResetRuntimePresenceStateOnLoad() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  CommitHostJoin(*application);
  application->host_client->SetPresence(PresenceState::kOnline);
  ResetRuntimePresenceState(*application);
  CHECK(application->host_client->GetPresence() == PresenceState::kUnknown);
  CHECK(application->chat_room->clients.size() == 1);
  CHECK(application->chat_room->clients[0]->GetPresence() ==
        PresenceState::kUnknown);
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
  auto ui_application = Application::ptr::MakeFromThis(
      static_cast<Application*>(ui_root.get()));
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

void TestConnectionBarPresenterStructure() {
#ifdef CHAT_UI_RUNTIME_DEMO_SOURCE_DIR
  std::filesystem::path const root{CHAT_UI_RUNTIME_DEMO_SOURCE_DIR};
  {
    std::ifstream in{root / "windows/win_connection_bar_presenter.h"};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("WinConnectionBarPresenter") != std::string::npos);
    CHECK(text.find("Your Aether ID:") != std::string::npos);
    CHECK(text.find("Host Aether ID:") != std::string::npos);
    CHECK(text.find("L\"Copy\"") != std::string::npos);
    CHECK(text.find("L\"Connect\"") != std::string::npos);
    CHECK(text.find("TryConnectHost") != std::string::npos);
    CHECK(text.find("NotifyMonitoring") == std::string::npos);
    CHECK(text.find("ES_READONLY") != std::string::npos);
    CHECK(text.find("CopyWideTextToClipboard") != std::string::npos);
  }
  {
    std::ifstream in{root / "windows/win_presenters.h"};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("WinConnectionBarPresenter connection_bar") !=
          std::string::npos);
    CHECK(text.find("kChatConnectionBarHeight") != std::string::npos);
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
    CHECK(text.find("--connect-host-uid") != std::string::npos);
    CHECK(text.find("--monitor-peer-uid") == std::string::npos);
  }
  {
    std::ifstream in{root / "windows/win_app.cpp"};
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    CHECK(text.find("ConnectToHostCommand") != std::string::npos);
    CHECK(text.find("AddPresencePeer") == std::string::npos);
    CHECK(text.find("SetApplicationRole") != std::string::npos);
    CHECK(text.find("monitor_peer_uid.txt") == std::string::npos);
  }
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
}

void TestApplyPresenceOverlayUnchangedReturnsZero() {
  EnsureChatRegistration();
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto application = BuildChatGraph(domain, "Host");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("host-uid");

  ChatSharedBinding binding;
  InitializeChatSharedBinding(binding, *application, "host-uid");
  CommitLocalJoin(binding, *application->host_client);
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

}  // namespace
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
  using apptraverse::test::TestConnectionBarPresenterStructure;
  using apptraverse::test::TestContactPresencePresentationGlyphs;
  using apptraverse::test::TestHostOnlineModelToUiProjection;
  using apptraverse::test::TestLocalAetherUidModelToUiProjection;
  using apptraverse::test::TestLocalChatHostJoinAndMessages;
  using apptraverse::test::TestLocalChatUiProjectionFromDomain;
  using apptraverse::test::TestLocalPresenceScheduleStateMapping;
  using apptraverse::test::TestPresenterTracksNestedClientGeneration;
  using apptraverse::test::TestResetRuntimePresenceStateOnLoad;
  using apptraverse::test::TestApplyPresenceOverlayUnchangedReturnsZero;
  using apptraverse::test::TestNoManualSerializersOrRuntimeClasses;
  TestLocalChatUiProjectionFromDomain();
  TestLocalAetherUidModelToUiProjection();
  TestApplicationRoleModelToUiProjection();
  TestConnectToHostCommandRegistersPeer();
  TestLocalPresenceScheduleStateMapping();
  TestContactPresencePresentationGlyphs();
  TestHostOnlineModelToUiProjection();
  TestApplyPresenceOverlayUnchangedReturnsZero();
  TestPresenterTracksNestedClientGeneration();
  TestConnectionBarPresenterStructure();
  TestResetRuntimePresenceStateOnLoad();
  TestAetherPinMatchesExpectedSha();
  TestAetherRxScheduleConfiguredInRuntime();
  TestAetherPresenceQueryOnlyOnAetherThread();
  TestNoManualSerializersOrRuntimeClasses();
  std::cout << "chat_ui_runtime_test OK\n";
  return 0;
}
