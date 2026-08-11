#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_graph_copy.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class BlockDoc;
class BlockPeer;
class BlockAddEvent;

class BlockPeer : public NodeFor<BlockPeer> {
  APPTRAVERSE_OBJECT(BlockPeer, Node, 0)

 protected:
  BlockPeer() = default;

 public:
  explicit BlockPeer(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

class BlockDoc : public NodeFor<BlockDoc> {
  APPTRAVERSE_OBJECT(BlockDoc, Node, 0)

 protected:
  BlockDoc() = default;

 public:
  explicit BlockDoc(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(local_note), AE_MMBR(last_peer))

  std::int32_t value{0};
  LocalPtr<BlockPeer> local_note;
  SharedPtr<BlockPeer> last_peer;

  void Apply(BlockAddEvent const& event);
};

class BlockAddEvent : public EventFor<BlockDoc, BlockAddEvent> {
  APPTRAVERSE_OBJECT(BlockAddEvent, Event, 0)

 protected:
  BlockAddEvent() = default;

 public:
  explicit BlockAddEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta), AE_MMBR(author))

  std::int32_t delta{0};
  SharedPtr<BlockPeer> author;
};

void BlockDoc::Apply(BlockAddEvent const& event) {
  value += event.delta;
  last_peer = event.author;
}

APPTRAVERSE_REGISTER(BlockPeer);
APPTRAVERSE_REGISTER(BlockDoc);
APPTRAVERSE_REGISTER(BlockAddEvent);

struct ChatFixture {
  ae::RamDomainStorage storage;
  ae::Domain domain;
  Client::ptr client;
  Client::ptr client_base;
  Chat::ptr chat;
  Chat::ptr chat_base;

  ChatFixture() : domain{ae::Now(), storage} {
    client_base =
        Client::ptr::Create(ae::CreateWith{domain}.with_id(201));
    client = Client::ptr::Create(ae::CreateWith{domain}.with_id(202));
    client->name = "Alice";
    client->base = client_base;
    client->CaptureBaseState();
    client.Save();

    chat_base = Chat::ptr::Create(ae::CreateWith{domain}.with_id(101));
    chat = Chat::ptr::Create(ae::CreateWith{domain}.with_id(102));
    chat->base = chat_base;
    chat->CaptureBaseState();
    chat.Save();
  }

  AddMessageEvent::ptr MakeMessage(std::string text, ae::ObjId::Type id) {
    auto event =
        AddMessageEvent::ptr::Create(ae::CreateWith{domain}.with_id(id));
    event->author = client;
    event->text = std::move(text);
    event.Save();
    return event;
  }

  JoinClientEvent::ptr MakeJoin(ae::ObjId::Type id) {
    auto event =
        JoinClientEvent::ptr::Create(ae::CreateWith{domain}.with_id(id));
    event->client = client;
    event.Save();
    return event;
  }
};

void TestAddMessageBeforeJoinBlockedThenAccepted() {
  ChatFixture fx;
  CHECK(fx.chat->entries.empty());
  CHECK(fx.chat->journal.empty());
  CHECK(fx.client.is_loaded());
  CHECK(fx.client->name == "Alice");

  auto message = fx.MakeMessage("hello", 501);
  auto const journal_before = fx.chat->journal.size();

  auto result = fx.chat->TryAcceptRemoteEvent(message, 1000);
  CHECK(result == RemoteEventResult::kBlocked);
  CHECK(fx.chat->journal.size() == journal_before);
  CHECK(fx.chat->entries.empty());
  CHECK(!fx.chat->HasEvent(ae::ObjId{501}));

  auto join = fx.MakeJoin(500);
  result = fx.chat->TryAcceptRemoteEvent(join, 900);
  CHECK(result == RemoteEventResult::kAccepted);
  CHECK(fx.chat->journal.size() == 1);
  CHECK(fx.chat->journal.front().timestamp_us == 900);
  CHECK(fx.chat->entries.size() == 1);
  CHECK(fx.chat->entries.front().kind == ChatEntryKind::kJoined);

  auto message_again = AddMessageEvent::ptr::Declare(
      ae::CreateWith{fx.domain}.with_id(501));
  message_again.Load();
  CHECK(message_again.is_loaded());

  result = fx.chat->TryAcceptRemoteEvent(message_again, 1000);
  CHECK(result == RemoteEventResult::kAccepted);
  CHECK(fx.chat->journal.size() == 2);
  CHECK(fx.chat->journal[0].timestamp_us == 900);
  CHECK(fx.chat->journal[1].timestamp_us == 1000);
  CHECK(fx.chat->entries.size() == 2);
  CHECK(fx.chat->entries.back().text == "hello");

  auto message_dup = AddMessageEvent::ptr::Declare(
      ae::CreateWith{fx.domain}.with_id(501));
  message_dup.Load();
  result = fx.chat->TryAcceptRemoteEvent(message_dup, 1000);
  CHECK(result == RemoteEventResult::kDuplicate);
  CHECK(fx.chat->journal.size() == 2);
}

void TestTimestampAndEarlierRebuild() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto peer_base =
      BlockPeer::ptr::Create(ae::CreateWith{domain}.with_id(10));
  auto peer = BlockPeer::ptr::Create(ae::CreateWith{domain}.with_id(11));
  peer->name = "p";
  peer->base = peer_base;
  peer->CaptureBaseState();

  auto doc_base =
      BlockDoc::ptr::Create(ae::CreateWith{domain}.with_id(20));
  auto doc = BlockDoc::ptr::Create(ae::CreateWith{domain}.with_id(21));
  doc->base = doc_base;
  doc->CaptureBaseState();

  auto late =
      BlockAddEvent::ptr::Create(ae::CreateWith{domain}.with_id(31));
  late->delta = 3;
  late->author = peer;
  late.Save();
  CHECK(doc->TryAcceptRemoteEvent(late, 2000) ==
        RemoteEventResult::kAccepted);
  CHECK(doc->journal.front().timestamp_us == 2000);
  CHECK(doc->value == 3);

  auto early =
      BlockAddEvent::ptr::Create(ae::CreateWith{domain}.with_id(30));
  early->delta = 2;
  early->author = peer;
  early.Save();
  CHECK(doc->TryAcceptRemoteEvent(early, 1000) ==
        RemoteEventResult::kAccepted);
  CHECK(doc->journal.size() == 2);
  CHECK(doc->journal[0].timestamp_us == 1000);
  CHECK(doc->journal[1].timestamp_us == 2000);
  CHECK(doc->value == 5);
}

void TestExistingNodeStateMergeKeepsLocalPtr() {
  ae::RamDomainStorage source_storage;
  ae::Domain source_domain{ae::Now(), source_storage};
  auto peer_base =
      BlockPeer::ptr::Create(ae::CreateWith{source_domain}.with_id(10));
  auto peer =
      BlockPeer::ptr::Create(ae::CreateWith{source_domain}.with_id(11));
  peer->name = "remote";
  peer->base = peer_base;
  peer->CaptureBaseState();
  peer.Save();

  auto doc_base =
      BlockDoc::ptr::Create(ae::CreateWith{source_domain}.with_id(20));
  auto doc =
      BlockDoc::ptr::Create(ae::CreateWith{source_domain}.with_id(21));
  doc->base = doc_base;
  doc->CaptureBaseState();
  auto event =
      BlockAddEvent::ptr::Create(ae::CreateWith{source_domain}.with_id(30));
  event->delta = 4;
  event->author = peer;
  doc->Commit(event);
  doc.Save();

  ae::RamDomainStorage target_storage;
  ae::Domain target_domain{ae::Now(), target_storage};
  auto target_peer_base =
      BlockPeer::ptr::Create(ae::CreateWith{target_domain}.with_id(40));
  auto target_local =
      BlockPeer::ptr::Create(ae::CreateWith{target_domain}.with_id(41));
  target_local->name = "local-only";
  target_local->base = target_peer_base;
  target_local->CaptureBaseState();

  auto target_doc_base =
      BlockDoc::ptr::Create(ae::CreateWith{target_domain}.with_id(20));
  auto target_doc =
      BlockDoc::ptr::Create(ae::CreateWith{target_domain}.with_id(21));
  target_doc->base = target_doc_base;
  target_doc->CaptureBaseState();
  target_doc->local_note = target_local;
  target_doc.Save();

  SyncReplica target{target_domain, target_storage, target_doc.id()};
  ImportObjectGraph(peer, source_storage, target,
                    SharedCopyMode::kCopyLoadedTargets);

  for (auto const& record : doc->journal) {
    if (target_doc->HasEvent(record.event.id())) {
      continue;
    }
    auto source_event = record.event;
    source_event.Load();
    ImportObjectGraph(source_event, source_storage, target,
                      SharedCopyMode::kReferenceExistingTargets);
    auto imported = Event::ptr::Declare(
        ae::CreateWith{target_domain}.with_id(source_event.id()));
    imported.Load();
    auto const result = target_doc->TryAcceptRemoteEvent(
        std::move(imported), record.timestamp_us);
    CHECK(result == RemoteEventResult::kAccepted);
  }
  target_doc.Save();

  target_doc.Load();
  CHECK(target_doc->local_note.is_valid());
  CHECK(target_doc->local_note.id() == target_local.id());
  target_doc->local_note.Load();
  CHECK(target_doc->local_note->name == "local-only");
  CHECK(target_doc->value == 4);
  CHECK(target_doc->HasEvent(ae::ObjId{30}));
  CHECK(target_doc->base.id() == target_doc_base.id());
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestAddMessageBeforeJoinBlockedThenAccepted();
  apptraverse::test::TestTimestampAndEarlierRebuild();
  apptraverse::test::TestExistingNodeStateMergeKeepsLocalPtr();
  std::cout << "blockable_remote_event_test OK\n";
  return 0;
}
