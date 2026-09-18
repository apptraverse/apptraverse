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
    CHECK(!dialog.conversation.is_valid());
    CHECK(app->next_message_sequence == 1);

    // Bind local endpoint + own uid, then create a Conversation for CommitShared.
    app->OnAetherLocalUid("own-1");
    CHECK(app->local_endpoint_uid == "own-1");
    CHECK(dialog.own_uid == "own-1");

    dialog.SetPeerUid("peer-a");
    auto conversation =
        apptraverse::Conversation::ptr::Create(ae::CreateWith{domain});
    apptraverse::InitializeRuntimeNode(*conversation, dialog);
    conversation->SetJournalCompactionBlocked(true);
    dialog.BindConversation(conversation);
    CHECK(dialog.conversation.is_valid());

    dialog.SetDraft("hello");
    app->AppendOutgoingMessage("hello");
    CHECK(dialog.draft.empty());
    CHECK(dialog.conversation->messages.size() == 1);
    CHECK(dialog.conversation->messages[0].text == "hello");
    CHECK(dialog.conversation->messages[0].id.origin_uid == "own-1");
    CHECK(dialog.conversation->messages[0].id.origin_sequence == 1);
    CHECK(app->next_message_sequence == 2);

    // Peer switch archives conversation messages; no cross-peer leak.
    dialog.SetPeerUid("peer-b");
    CHECK(dialog.peer_uid == "peer-b");
    CHECK(!dialog.conversation.is_valid());
    CHECK(dialog.messages.empty());
    CHECK(dialog.draft.empty());

    auto conversation_b =
        apptraverse::Conversation::ptr::Create(ae::CreateWith{domain});
    apptraverse::InitializeRuntimeNode(*conversation_b, dialog);
    conversation_b->SetJournalCompactionBlocked(true);
    dialog.BindConversation(conversation_b);
    app->AppendOutgoingMessage("other");
    CHECK(dialog.conversation->messages.size() == 1);
    CHECK(dialog.conversation->messages[0].text == "other");

    dialog.SetPeerUid("peer-a");
    CHECK(dialog.peer_uid == "peer-a");
    CHECK(!dialog.conversation.is_valid());
    CHECK(dialog.messages.size() == 1);
    CHECK(dialog.messages[0].text == "hello");
    CHECK(dialog.messages[0].outgoing);

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
    CHECK(app->local_endpoint_uid == "own-1");
    CHECK(dialog.peer_uid == "peer-a");
    CHECK(dialog.messages.size() == 1);
    CHECK(dialog.messages[0].text == "hello");
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
