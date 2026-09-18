#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/runtime_node.h"

#include "messenger_distill.h"
#include "messenger_ids.h"
#include "messenger_model.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"     \
                << __LINE__ << '\n';                                        \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureMessengerModelRegistration();

  std::filesystem::path const state_dir =
      std::filesystem::temp_directory_path() / "apptraverse_messenger_model_test";
  std::filesystem::remove_all(state_dir);

  {
    apptraverse::DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto app = apptraverse::BuildMessengerGraph(domain);
    apptraverse::FinalizeDistilledGraph(*app);
    apptraverse::SaveDistilledRoot(*app);
  }

  {
    apptraverse::DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto app = apptraverse::LoadApplication<apptraverse::Application>(
        domain, ae::ObjId{apptraverse::messenger::ToObjId(
                    apptraverse::messenger::ObjId::Application)});
    apptraverse::BindReachableNodesMaterializedChangeNotifier(
        *app, nullptr, nullptr);

    auto& dialog = *app->surfaces->surfaces.front()->dialog;
    CHECK(dialog.own_uid.empty());
    CHECK(dialog.peer_uid.empty());
    CHECK(dialog.messages.empty());

    dialog.SetPeerUid("peer-a");
    dialog.SetDraft("hello");
    dialog.AppendOutgoingMessage("hello");
    CHECK(dialog.peer_uid == "peer-a");
    CHECK(dialog.draft.empty());
    CHECK(dialog.messages.size() == 1);
    CHECK(dialog.messages[0].text == "hello");
    CHECK(dialog.messages[0].outgoing);

    dialog.SetPeerUid("peer-b");
    CHECK(dialog.peer_uid == "peer-b");
    CHECK(dialog.messages.empty());
    CHECK(dialog.draft.empty());
    dialog.AppendOutgoingMessage("other");
    CHECK(dialog.messages.size() == 1);
    CHECK(dialog.messages[0].text == "other");

    dialog.SetPeerUid("peer-a");
    CHECK(dialog.peer_uid == "peer-a");
    CHECK(dialog.messages.size() == 1);
    CHECK(dialog.messages[0].text == "hello");

    dialog.SetOwnUid("own-1");
    CHECK(dialog.own_uid == "own-1");

    apptraverse::ClearReachableNodesMaterializedChangeNotifier(*app);
    app.Save();
  }

  {
    apptraverse::DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto app = apptraverse::LoadApplication<apptraverse::Application>(
        domain, ae::ObjId{apptraverse::messenger::ToObjId(
                    apptraverse::messenger::ObjId::Application)});
    auto& dialog = *app->surfaces->surfaces.front()->dialog;
    CHECK(dialog.own_uid == "own-1");
    CHECK(dialog.peer_uid == "peer-a");
    CHECK(dialog.messages.size() == 1);
    CHECK(dialog.messages[0].text == "hello");
    // Journal replay: archived peer-b still present after reload.
    bool found_b = false;
    for (auto const& entry : dialog.archived) {
      if (entry.peer_uid == "peer-b") {
        found_b = true;
        CHECK(entry.messages.size() == 1);
        CHECK(entry.messages[0].text == "other");
      }
    }
    CHECK(found_b);
  }

  std::filesystem::remove_all(state_dir);
  std::cout << "messenger_model_test ok\n";
  return 0;
}
