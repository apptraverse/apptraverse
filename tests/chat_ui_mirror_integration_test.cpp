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

void TestMessageFieldsModelToUi() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{ae::Now(), model_storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
  auto application = BuildChatGraph(model_domain, "Nikolay");
  FinalizeDistilledGraph(*application);
  application->host_client->SetAetherUidText("local-uid");
  CommitHostJoin(*application);

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

  std::int64_t const sent_at = 1'720'000'000'100LL;
  runtime.Post([&] {
    CommitSendChatMessage(*application->chat_room, *application->host_client,
                          "hello", sent_at);
  });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(!applies.empty());
  CHECK(ui_application->chat_room->feed.size() == 2);
  auto const& ui_item = ui_application->chat_room->feed[1];
  ui_item.Load();
  CHECK(ui_item->sent_at_unix_ms == sent_at);
  CHECK(ui_item->source_event_obj_id != 0);
  CHECK(application->chat_room->feed[1]->sent_at_unix_ms == sent_at);
}

void TestHostOnlineModelToUiProjection() {
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

  runtime.Post([&] { SetHostClientOnline(*application->host_client, true); });
  runtime.PumpOnce(std::chrono::steady_clock::now());
  CHECK(ui_application->chat_room->clients[0]->online);
}

void TestDynamicClientAttachMapping() {
  EnsureChatRegistration();
  ae::RamDomainStorage model_storage;
  OverlayDomainStorage ui_storage{model_storage};
  ae::Domain model_domain{ae::Now(), model_storage};
  ae::Domain ui_domain{ae::Now(), ui_storage};
  auto application = BuildChatGraph(model_domain, "Host");
  FinalizeDistilledGraph(*application);
  CommitHostJoin(*application);

  UiMirror mirror{ui_domain, ui_storage,
                  [](std::uint32_t, PublicationChannel<3>*) {}};
  ModelRuntime runtime{*application, mirror};
  runtime.AddPresentationRoot(*application->chat_room);

  auto client = ChatClient::ptr::Create(ae::CreateWith{model_domain});
  client->SetAetherUidText("remote-uid");
  auto name = ImmutableString::ptr::Create(ae::CreateWith{model_domain});
  name->bytes = "Client";
  client->display_name = name;
  runtime.AttachNode(*client, *application->chat_room);
  CHECK(runtime.IsInExecutionList(*client));
  CHECK(runtime.IsMappedToPresentationRoot(
      *client, chat::ToObjId(chat::ChatObjId::ChatRoom)));
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  using namespace apptraverse::test;
  TestMessageFieldsModelToUi();
  TestHostOnlineModelToUiProjection();
  TestDynamicClientAttachMapping();
  std::cout << "chat_ui_mirror_integration_test OK\n";
  return 0;
}
