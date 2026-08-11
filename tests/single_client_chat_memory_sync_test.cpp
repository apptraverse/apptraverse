#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/ideal_memory_sync.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_graph.h"
#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"
#include "model/window.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

#include "../examples/single_client_chat/common/chat_transcript.h"
#include "../examples/single_client_chat/common/graph_builder.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class FakeChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(FakeChatPresenter, ChatPresenter, 0)

 protected:
  FakeChatPresenter() = default;

 public:
  explicit FakeChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}

  AE_OBJECT_REFLECT()
};

class FakeWindow : public NodeFor<FakeWindow, Window> {
  APPTRAVERSE_OBJECT(FakeWindow, Window, 0)

 protected:
  FakeWindow() = default;

 public:
  explicit FakeWindow(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  void Apply(WindowChangedEvent const&) override {}
};

class FakeWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(FakeWindowPresenter, WindowPresenter, 0)

 protected:
  FakeWindowPresenter() = default;

 public:
  explicit FakeWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}

  AE_OBJECT_REFLECT()
};

APPTRAVERSE_REGISTER(FakeChatPresenter);
APPTRAVERSE_REGISTER(FakeWindow);
APPTRAVERSE_REGISTER(FakeWindowPresenter);

void SleepForDistinctTimestamp() {
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

struct ChatReplica {
  ae::RamDomainStorage storage;
  ae::Domain domain;
  examples::SingleClientChatGraph graph;
  std::string platform_name;

  explicit ChatReplica(std::string name)
      : domain{ae::Now(), storage}, platform_name{std::move(name)} {
    graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                 FakeChatPresenter>(
        domain, platform_name);
    graph.app.Save();
  }

  MemoryReplica AsMemoryReplica() {
    return MemoryReplica{domain, storage, graph.chat.id()};
  }

  SyncResult SyncBidirectionalWith(ChatReplica& other) {
    auto self = AsMemoryReplica();
    auto peer = other.AsMemoryReplica();
    return SynchronizeSharedGraphBidirectional(self, peer);
  }

  SyncResult SyncOneWayTo(ChatReplica& other) {
    auto self = AsMemoryReplica();
    auto peer = other.AsMemoryReplica();
    return SynchronizeSharedGraphOneWay(self, peer);
  }

  std::string Transcript() const {
    return examples::FormatChatTranscriptUtf8(graph.chat);
  }

  void Submit(std::string text) {
    SleepForDistinctTimestamp();
    graph.chat_presenter->SubmitText(std::move(text));
    graph.chat.Save();
    graph.app.Save();
  }
};

bool ContainsNodeId(std::vector<Node::ptr> const& nodes, ae::ObjId id) {
  for (auto const& node : nodes) {
    if (node.id() == id) {
      return true;
    }
  }
  return false;
}

void ExpectSharedClients(ChatReplica& replica, ae::ObjId a, ae::ObjId b) {
  auto discovered = DiscoverSharedGraph(replica.graph.chat);
  CHECK(discovered.size() == 3);
  CHECK(ContainsNodeId(discovered, replica.graph.chat.id()));
  CHECK(ContainsNodeId(discovered, a));
  CHECK(ContainsNodeId(discovered, b));
}

void TestSingleClientChatMemorySync() {
  ChatReplica windows{"Windows"};
  ChatReplica android{"Android"};

  CHECK(windows.graph.chat.id().id() == ToObjId(ApplicationObjId::Chat));
  CHECK(android.graph.chat.id().id() == ToObjId(ApplicationObjId::Chat));
  CHECK(windows.graph.local_client.id() != android.graph.local_client.id());

  auto win_before = DiscoverSharedGraph(windows.graph.chat);
  CHECK(win_before.size() == 2);
  CHECK(ContainsNodeId(win_before, windows.graph.chat.id()));
  CHECK(ContainsNodeId(win_before, windows.graph.local_client.id()));

  auto and_before = DiscoverSharedGraph(android.graph.chat);
  CHECK(and_before.size() == 2);
  CHECK(ContainsNodeId(and_before, android.graph.chat.id()));
  CHECK(ContainsNodeId(and_before, android.graph.local_client.id()));

  CHECK(windows.graph.chat->journal.size() == 1);
  CHECK(android.graph.chat->journal.size() == 1);
  auto const win_join_id = windows.graph.chat->journal.front().event.id();
  auto const and_join_id = android.graph.chat->journal.front().event.id();
  CHECK(win_join_id != and_join_id);

  windows.Submit("W-before");
  android.Submit("A-before");
  CHECK(windows.graph.chat->journal.size() == 2);
  CHECK(android.graph.chat->journal.size() == 2);

  auto const initial = windows.SyncBidirectionalWith(android);
  CHECK(initial.nodes_imported >= 2);  // each side imports the other Client
  CHECK(initial.events_imported >= 2);

  // Reload chat pointers after sync (storage updated).
  windows.graph.chat.Load();
  android.graph.chat.Load();
  windows.graph.local_client.Load();
  android.graph.local_client.Load();
  windows.graph.app.Load();
  android.graph.app.Load();
  windows.graph.chat_presenter.Load();
  android.graph.chat_presenter.Load();

  ExpectSharedClients(windows, windows.graph.local_client.id(),
                      android.graph.local_client.id());
  ExpectSharedClients(android, windows.graph.local_client.id(),
                      android.graph.local_client.id());

  CHECK(windows.graph.chat->journal.size() == 4);
  CHECK(android.graph.chat->journal.size() == 4);
  CHECK(windows.Transcript() == android.Transcript());
  CHECK(windows.Transcript().find("W-before") != std::string::npos);
  CHECK(windows.Transcript().find("A-before") != std::string::npos);
  CHECK(windows.Transcript().find("Windows joined") != std::string::npos);
  CHECK(windows.Transcript().find("Android joined") != std::string::npos);

  // Local identity isolation.
  CHECK(windows.graph.app->local_client.id() == windows.graph.local_client.id());
  CHECK(android.graph.app->local_client.id() == android.graph.local_client.id());
  CHECK(windows.graph.chat_presenter->local_client.id() ==
        windows.graph.local_client.id());
  CHECK(android.graph.chat_presenter->local_client.id() ==
        android.graph.local_client.id());
  CHECK(windows.graph.chat->presenter.id() == windows.graph.chat_presenter.id());
  CHECK(android.graph.chat->presenter.id() == android.graph.chat_presenter.id());
  CHECK(windows.graph.app->window.id() == windows.graph.window.id());
  CHECK(android.graph.app->window.id() == android.graph.window.id());

  // New events after initial sync.
  windows.Submit("W-after");
  auto const w_to_a = windows.SyncOneWayTo(android);
  CHECK(w_to_a.nodes_imported == 0);
  CHECK(w_to_a.events_imported == 1);
  android.graph.chat.Load();
  CHECK(android.Transcript().find("W-after") != std::string::npos);

  android.Submit("A-after");
  auto const a_to_w = android.SyncOneWayTo(windows);
  CHECK(a_to_w.nodes_imported == 0);
  CHECK(a_to_w.events_imported == 1);
  windows.graph.chat.Load();
  CHECK(windows.Transcript().find("A-after") != std::string::npos);
  CHECK(windows.Transcript() == android.Transcript());

  // Offline-like independent changes.
  windows.Submit("W-offline-1");
  windows.Submit("W-offline-2");
  android.Submit("A-offline-1");
  auto const offline = windows.SyncBidirectionalWith(android);
  CHECK(offline.nodes_imported == 0);
  CHECK(offline.events_imported == 3);
  windows.graph.chat.Load();
  android.graph.chat.Load();
  CHECK(windows.Transcript() == android.Transcript());
  CHECK(windows.graph.chat->journal.size() == android.graph.chat->journal.size());

  // New shared Client Carol discovered automatically.
  SleepForDistinctTimestamp();
  auto carol_base = Client::ptr::Create(ae::CreateWith{windows.domain});
  auto carol = Client::ptr::Create(ae::CreateWith{windows.domain});
  carol->name = "Carol";
  carol->base = carol_base;
  carol->CaptureBaseState();
  auto join_carol = JoinClientEvent::ptr::Create(ae::CreateWith{windows.domain});
  join_carol->client = carol;
  windows.graph.chat->Commit(join_carol);
  windows.graph.chat.Save();
  auto const carol_id = carol.id();

  auto const carol_sync = windows.SyncOneWayTo(android);
  CHECK(carol_sync.nodes_imported == 1);
  CHECK(carol_sync.events_imported == 1);
  android.graph.chat.Load();
  auto and_discovered = DiscoverSharedGraph(android.graph.chat);
  CHECK(ContainsNodeId(and_discovered, carol_id));
  auto carol_loaded =
      Client::ptr::Declare(ae::CreateWith{android.domain}.with_id(carol_id));
  carol_loaded.Load();
  CHECK(carol_loaded.is_loaded());
  CHECK(carol_loaded->name == "Carol");
  CHECK(android.Transcript().find("Carol joined") != std::string::npos);

  // Duplicate sync → zero changes.
  auto const dup_one = windows.SyncOneWayTo(android);
  CHECK(dup_one.nodes_imported == 0);
  CHECK(dup_one.events_imported == 0);
  auto const dup_bi = windows.SyncBidirectionalWith(android);
  CHECK(dup_bi.nodes_imported == 0);
  CHECK(dup_bi.events_imported == 0);

  auto const journal_size = windows.graph.chat->journal.size();
  auto const transcript = windows.Transcript();
  CHECK(android.graph.chat->journal.size() == journal_size);
  CHECK(android.Transcript() == transcript);

  // Local graph isolation still holds.
  CHECK(windows.graph.app->local_client.id() == windows.graph.local_client.id());
  CHECK(android.graph.app->local_client.id() == android.graph.local_client.id());
  CHECK(windows.graph.window.id() ==
        ae::ObjId{ToObjId(ApplicationObjId::Window)});
  CHECK(android.graph.window.id() ==
        ae::ObjId{ToObjId(ApplicationObjId::Window)});
  // Presenters use fixed application ObjIds; they stay local to each Domain.
  CHECK(windows.graph.chat_presenter.domain() == &windows.domain);
  CHECK(android.graph.chat_presenter.domain() == &android.domain);
  CHECK(windows.graph.window_presenter.domain() == &windows.domain);
  CHECK(android.graph.window_presenter.domain() == &android.domain);

  // Save / reload both replicas.
  windows.graph.app.Save();
  android.graph.app.Save();
  windows.graph.chat.Save();
  android.graph.chat.Save();

  ae::Domain windows2{ae::Now(), windows.storage};
  ae::Domain android2{ae::Now(), android.storage};
  auto win_app = App::ptr::Declare(
      ae::CreateWith{windows2}.with_id(ToObjId(ApplicationObjId::Application)));
  auto and_app = App::ptr::Declare(
      ae::CreateWith{android2}.with_id(ToObjId(ApplicationObjId::Application)));
  win_app.Load();
  and_app.Load();
  CHECK(win_app.is_loaded());
  CHECK(and_app.is_loaded());
  win_app->local_client.Load();
  and_app->local_client.Load();
  CHECK(win_app->local_client->name == "Windows");
  CHECK(and_app->local_client->name == "Android");

  auto win_chat = Chat::ptr::Declare(
      ae::CreateWith{windows2}.with_id(ToObjId(ApplicationObjId::Chat)));
  auto and_chat = Chat::ptr::Declare(
      ae::CreateWith{android2}.with_id(ToObjId(ApplicationObjId::Chat)));
  win_chat.Load();
  and_chat.Load();
  CHECK(win_chat->journal.size() == journal_size);
  CHECK(and_chat->journal.size() == journal_size);
  CHECK(examples::FormatChatTranscriptUtf8(win_chat) == transcript);
  CHECK(examples::FormatChatTranscriptUtf8(and_chat) == transcript);

  auto carol_win =
      Client::ptr::Declare(ae::CreateWith{windows2}.with_id(carol_id));
  auto carol_and =
      Client::ptr::Declare(ae::CreateWith{android2}.with_id(carol_id));
  carol_win.Load();
  carol_and.Load();
  CHECK(carol_win.is_loaded());
  CHECK(carol_and.is_loaded());

  MemoryReplica win_reloaded{windows2, windows.storage, win_chat.id()};
  MemoryReplica and_reloaded{android2, android.storage, and_chat.id()};
  auto const after_reload =
      SynchronizeSharedGraphBidirectional(win_reloaded, and_reloaded);
  CHECK(after_reload.nodes_imported == 0);
  CHECK(after_reload.events_imported == 0);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestSingleClientChatMemorySync();
  return 0;
}
