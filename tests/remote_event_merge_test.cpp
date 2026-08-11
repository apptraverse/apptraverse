#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class MergeDoc;
class MergePeer;
class AddValueEvent;

class MergePeer : public NodeFor<MergePeer> {
  APPTRAVERSE_OBJECT(MergePeer, Node, 0)

 protected:
  MergePeer() = default;

 public:
  explicit MergePeer(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

class MergeDoc : public NodeFor<MergeDoc> {
  APPTRAVERSE_OBJECT(MergeDoc, Node, 0)

 protected:
  MergeDoc() = default;

 public:
  explicit MergeDoc(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value), AE_MMBR(last_peer))

  std::int32_t value{0};
  SharedPtr<MergePeer> last_peer;

  void Apply(AddValueEvent const& event);
};

class AddValueEvent : public EventFor<MergeDoc, AddValueEvent> {
  APPTRAVERSE_OBJECT(AddValueEvent, Event, 0)

 protected:
  AddValueEvent() = default;

 public:
  explicit AddValueEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta), AE_MMBR(author))

  std::int32_t delta{0};
  SharedPtr<MergePeer> author;
};

void MergeDoc::Apply(AddValueEvent const& event) {
  value += event.delta;
  last_peer = event.author;
}

APPTRAVERSE_REGISTER(MergePeer);
APPTRAVERSE_REGISTER(MergeDoc);
APPTRAVERSE_REGISTER(AddValueEvent);

bool TransferRemoteEventGraph(Event::ptr source_event,
                              std::uint64_t original_timestamp_us,
                              ae::IDomainStorage& source_storage,
                              Node::ptr target_node,
                              ae::IDomainStorage& target_storage) {
  SyncReplica target{*target_node.domain(), target_storage, target_node.id()};
  ImportObjectGraph(source_event, source_storage, target,
                    SharedCopyMode::kReferenceExistingTargets);
  auto imported = Event::ptr::Declare(
      ae::CreateWith{*target_node.domain()}.with_id(source_event.id()));
  imported.Load();
  assert(imported.is_loaded());
  return target_node->AcceptRemoteEvent(std::move(imported),
                                        original_timestamp_us);
}

struct SourceReplica {
  ae::RamDomainStorage storage;
  ae::Domain domain;
  MergeDoc::ptr doc;
  MergePeer::ptr peer;
  MergeDoc::ptr doc_base;
  MergePeer::ptr peer_base;

  SourceReplica() : domain{ae::Now(), storage} {
    doc_base = MergeDoc::ptr::Create(ae::CreateWith{domain}.with_id(100));
    doc = MergeDoc::ptr::Create(ae::CreateWith{domain}.with_id(101));
    peer_base = MergePeer::ptr::Create(ae::CreateWith{domain}.with_id(200));
    peer = MergePeer::ptr::Create(ae::CreateWith{domain}.with_id(201));
    peer->name = "alice";
    peer->base = peer_base;
    peer->CaptureBaseState();
    doc->base = doc_base;
    doc->CaptureBaseState();
  }

  AddValueEvent::ptr MakeEvent(std::int32_t delta, ae::ObjId::Type event_id) {
    auto event =
        AddValueEvent::ptr::Create(ae::CreateWith{domain}.with_id(event_id));
    event->delta = delta;
    event->author = peer;
    event.Save();
    return event;
  }
};

void TestAcceptNewDuplicateEarlierRebindReload() {
  SourceReplica source;
  auto early = source.MakeEvent(2, 401);
  auto late = source.MakeEvent(3, 402);
  std::uint64_t const early_ts = 1000;
  std::uint64_t const late_ts = 2000;

  ae::RamDomainStorage target_storage;
  ae::Domain target_domain{ae::Now(), target_storage};
  auto target_doc_base =
      MergeDoc::ptr::Create(ae::CreateWith{target_domain}.with_id(100));
  auto target_doc =
      MergeDoc::ptr::Create(ae::CreateWith{target_domain}.with_id(101));
  target_doc->base = target_doc_base;
  target_doc->CaptureBaseState();
  target_doc.Save();

  CHECK(target_doc->journal.empty());
  CHECK(target_doc->value == 0);

  CHECK(TransferRemoteEventGraph(late, late_ts, source.storage, target_doc,
                                 target_storage));
  CHECK(target_doc->journal.size() == 1);
  CHECK(target_doc->journal.front().timestamp_us == late_ts);
  CHECK(target_doc->journal.front().event.id().id() == 402);
  CHECK(target_doc->value == 3);
  CHECK(target_doc->last_peer.id().id() == 201);
  CHECK(target_doc->last_peer.domain() == &target_domain);
  CHECK(!target_doc->last_peer.is_loaded());

  CHECK(!TransferRemoteEventGraph(late, late_ts, source.storage, target_doc,
                                  target_storage));
  CHECK(target_doc->journal.size() == 1);
  CHECK(target_doc->value == 3);

  SyncReplica target{target_domain, target_storage, target_doc.id()};
  ImportObjectGraph(source.peer, source.storage, target,
                    SharedCopyMode::kCopyLoadedTargets);

  CHECK(TransferRemoteEventGraph(early, early_ts, source.storage, target_doc,
                                 target_storage));
  CHECK(target_doc->journal.size() == 2);
  CHECK(target_doc->journal[0].timestamp_us == early_ts);
  CHECK(target_doc->journal[1].timestamp_us == late_ts);
  CHECK(target_doc->value == 5);

  target_doc->last_peer.Load();
  CHECK(target_doc->last_peer.is_loaded());
  CHECK(target_doc->last_peer->name == "alice");
  CHECK(target_doc->last_peer.domain() == &target_domain);
  for (auto const& record : target_doc->journal) {
    CHECK(record.event.domain() == &target_domain);
    auto add = AddValueEvent::ptr::Declare(
        ae::CreateWith{target_domain}.with_id(record.event.id()));
    add.Load();
    CHECK(add.is_loaded());
    CHECK(add->author.domain() == &target_domain);
  }

  target_doc.Save();
  ae::Domain reloaded_domain{ae::Now(), target_storage};
  auto reloaded =
      MergeDoc::ptr::Declare(ae::CreateWith{reloaded_domain}.with_id(101));
  reloaded.Load();
  CHECK(reloaded.is_loaded());
  CHECK(reloaded->journal.size() == 2);
  CHECK(reloaded->value == 5);
  CHECK(reloaded->HasEvent(ae::ObjId{401}));
  CHECK(reloaded->HasEvent(ae::ObjId{402}));
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestAcceptNewDuplicateEarlierRebindReload();
  return 0;
}
