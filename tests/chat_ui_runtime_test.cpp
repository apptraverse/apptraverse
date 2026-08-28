#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/distill.h"
#include "apptraverse/graph_mirror.h"
#include "apptraverse/model_runtime.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/ui_mirror.h"

#include "chat_bootstrap.h"
#include "chat_commands.h"
#include "chat_ids.h"
#include "chat_model.h"

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
  ae::Domain domain{ae::Now(), storage};
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
  ae::Domain model_domain{ae::Now(), model_storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
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
  CHECK(FormatChatFeedLine(*ui_application->chat_room->feed[1]) ==
        "Nikolay: hello");
  CHECK(ui_application->chat_room->clients.size() == 1);
}

void TestLocalAetherUidModelToUiProjection() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{ae::Now(), model_storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
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

void TestAetherPinMatchesExpectedSha() {
  CHECK(std::string{APPTRAVERSE_AETHER_EXPECTED_SHA} ==
        "941744cdccb364134da5cc61f4edc613465e843a");
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
  CHECK(text.find("941744cdccb364134da5cc61f4edc613465e843a") !=
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
  CHECK(text.find("SetReceiveSchedule") != std::string::npos);
  CHECK(text.find("AETHER_RX_SCHEDULE_SET ping_ms=3000 window_ms=3000") !=
        std::string::npos);
  CHECK(text.find("std::chrono::seconds{3}") != std::string::npos);
  CHECK(text.find("QueryPeerReceiveSchedule") == std::string::npos);
#else
  CHECK(false && "CHAT_UI_RUNTIME_DEMO_SOURCE_DIR is required");
#endif
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
  using apptraverse::test::TestAetherPinMatchesExpectedSha;
  using apptraverse::test::TestAetherRxScheduleConfiguredInRuntime;
  using apptraverse::test::TestLocalAetherUidModelToUiProjection;
  using apptraverse::test::TestLocalChatHostJoinAndMessages;
  using apptraverse::test::TestLocalChatUiProjectionFromDomain;
  using apptraverse::test::TestNoManualSerializersOrRuntimeClasses;

  TestLocalChatHostJoinAndMessages();
  TestLocalChatUiProjectionFromDomain();
  TestLocalAetherUidModelToUiProjection();
  TestAetherPinMatchesExpectedSha();
  TestAetherRxScheduleConfiguredInRuntime();
  TestNoManualSerializersOrRuntimeClasses();
  std::cout << "chat_ui_runtime_test OK\n";
  return 0;
}
