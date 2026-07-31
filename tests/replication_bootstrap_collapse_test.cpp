#include <cassert>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/replica_id.h"
#include "apptraverse/replication_engine.h"
#include "apptraverse/replication_message.h"
#include "apptraverse/replication_state.h"
#include "apptraverse/replication_transport.h"

namespace apptraverse::test {

class SharedDocument;
class AppendItemEvent;

class SharedDocument : public apptraverse::NodeFor<SharedDocument> {
  AE_OBJECT(SharedDocument, Node, 0)

 protected:
  SharedDocument() = default;

 public:
  explicit SharedDocument(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(title), AE_MMBR(items))

  std::string title;
  std::vector<std::string> items;

  void Apply(AppendItemEvent const& event);
  void CaptureBaseStateForTest() { CaptureBaseState(); }
};

class AppendItemEvent
    : public apptraverse::EventFor<SharedDocument, AppendItemEvent> {
  AE_OBJECT(AppendItemEvent, Event, 0)

 protected:
  AppendItemEvent() = default;

 public:
  explicit AppendItemEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  std::string text;
};

void SharedDocument::Apply(AppendItemEvent const& event) {
  items.push_back(event.text);
}

struct ReplicaBundle {
  apptraverse::ReplicaId id;
  ae::RamDomainStorage storage;
  ae::Domain domain;
  ae::RamDomainStorage message_storage;
  ae::Domain message_domain;
  SharedDocument::ptr root;
  apptraverse::ReplicationState::ptr state;
  std::unique_ptr<apptraverse::ReplicationEngine> engine;
  bool online{true};

  ReplicaBundle(apptraverse::ReplicaId replica_id, ae::ObjId root_id,
                ae::ObjId base_id, ae::ObjId state_id, std::string title)
      : id{replica_id},
        domain{ae::Now(), storage},
        message_domain{ae::Now(), message_storage} {
    auto base =
        SharedDocument::ptr::Create(ae::CreateWith{domain}.with_id(base_id));
    base->title = "unset";
    root = SharedDocument::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
    root->title = std::move(title);
    root->base = base;
    root->CaptureBaseStateForTest();

    state = apptraverse::ReplicationState::ptr::Create(
        ae::CreateWith{domain}.with_id(state_id));
    state->local_replica_id = id;
  }
};

class MeshTransport final : public apptraverse::IReplicationTransport {
 public:
  std::map<apptraverse::ReplicaId, apptraverse::ReplicationEngine*> engines;
  std::map<apptraverse::ReplicaId, ae::RamDomainStorage*> storages;
  std::map<apptraverse::ReplicaId, ae::Domain*> domains;
  std::map<apptraverse::ReplicaId, ae::RamDomainStorage*> message_storages;
  std::map<apptraverse::ReplicaId, bool*> online;

  void Send(apptraverse::ReplicaId recipient,
            apptraverse::ReplicationMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    if (online.count(recipient) != 0 && online[recipient] != nullptr &&
        !*online[recipient]) {
      return;
    }
    if (engines.count(recipient) == 0) {
      return;
    }
    DeliverNow(recipient, message);
  }

  void DeliverNow(apptraverse::ReplicaId recipient,
                  apptraverse::ReplicationMessage::ptr message) {
    auto* engine = engines.at(recipient);
    auto* storage = storages.at(recipient);
    auto* domain = domains.at(recipient);

    message.Save();
    ae::RamDomainStorage* source = nullptr;
    for (auto const& [_, message_storage] : message_storages) {
      if (message_storage->state.find(message.id()) !=
          message_storage->state.end()) {
        source = message_storage;
        break;
      }
    }
    assert(source != nullptr);
    for (auto const& entry : source->state) {
      storage->state[entry.first] = entry.second;
    }
    source->state.clear();

    apptraverse::ReplicationMessage::ptr incoming =
        apptraverse::ReplicationMessage::ptr::Declare(
            ae::CreateWith{*domain}.with_id(message.id()));
    incoming.Load();
    engine->Receive(incoming);
  }
};

void BindReplica(MeshTransport& mesh, ReplicaBundle& replica) {
  replica.engine = std::make_unique<apptraverse::ReplicationEngine>(
      replica.root, replica.state, replica.message_domain, mesh);
  mesh.engines[replica.id] = replica.engine.get();
  mesh.storages[replica.id] = &replica.storage;
  mesh.domains[replica.id] = &replica.domain;
  mesh.message_storages[replica.id] = &replica.message_storage;
  mesh.online[replica.id] = &replica.online;
}

bool JournalsEqual(apptraverse::Node const& left,
                   apptraverse::Node const& right) {
  if (left.journal.size() != right.journal.size()) {
    return false;
  }
  for (std::size_t i = 0; i < left.journal.size(); ++i) {
    if (!(left.journal[i].identity == right.journal[i].identity) ||
        !(left.journal[i].order == right.journal[i].order)) {
      return false;
    }
  }
  return true;
}

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  using apptraverse::ReplicaId;
  using apptraverse::test::AppendItemEvent;
  using apptraverse::test::BindReplica;
  using apptraverse::test::JournalsEqual;
  using apptraverse::test::MeshTransport;
  using apptraverse::test::ReplicaBundle;

  ReplicaId const id_a{1};
  ReplicaId const id_b{2};
  ReplicaId const id_c{3};

  // G. Bootstrap of a new replica after partial collapse history.
  {
    MeshTransport mesh;
    ReplicaBundle a{id_a, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    ReplicaBundle b{id_b, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    BindReplica(mesh, a);
    BindReplica(mesh, b);
    a.engine->AddPeer(id_b);
    b.engine->AddPeer(id_a);

    auto first =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(200));
    first->text = "collapsed";
    a.engine->CommitLocal(a.root, first);
    CHECK(a.root->journal.empty());
    CHECK(b.root->journal.empty());
    CHECK(a.root->items.size() == 1);
    CHECK(b.root->items.size() == 1);

    // Offline known peer blocks collapse of the remaining journal prefix.
    ReplicaId const id_offline{99};
    a.engine->AddPeer(id_offline);
    b.engine->AddPeer(id_offline);

    auto second =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(201));
    second->text = "remaining";
    a.engine->CommitLocal(a.root, second);
    CHECK(a.root->journal.size() == 1);
    CHECK(b.root->journal.size() == 1);

    ReplicaBundle c{id_c, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    BindReplica(mesh, c);
    a.engine->SendBootstrap(id_c);
    b.engine->AddPeer(id_c);
    c.engine->AddPeer(id_a);
    c.engine->AddPeer(id_b);

    CHECK(c.root->items.size() == 2);
    CHECK(c.root->items[0] == "collapsed");
    CHECK(c.root->items[1] == "remaining");
    CHECK(c.root->journal.size() == 1);
    CHECK(JournalsEqual(*a.root, *c.root));

    // C must not be attached to deliveries of events committed before bootstrap.
    CHECK(a.state->FindOutgoing(a.root->journal[0].identity, id_c) == nullptr);

    auto future =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(202));
    future->text = "future";
    a.engine->CommitLocal(a.root, future);
    CHECK(c.root->items.size() == 3);
    CHECK(c.root->items.back() == "future");
    CHECK(a.state->FindOutgoing(a.root->journal.back().identity, id_c) !=
          nullptr);
  }

  // H. Offline peer blocks collapse; reconnect converges and collapses.
  {
    MeshTransport mesh;
    ReplicaBundle a{id_a, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    ReplicaBundle b{id_b, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    ReplicaBundle c{id_c, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    BindReplica(mesh, a);
    BindReplica(mesh, b);
    BindReplica(mesh, c);

    a.engine->AddPeer(id_b);
    a.engine->AddPeer(id_c);
    b.engine->AddPeer(id_a);
    b.engine->AddPeer(id_c);
    c.engine->AddPeer(id_a);
    c.engine->AddPeer(id_b);

    c.online = false;

    auto ea =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(300));
    ea->text = "from-a";
    a.engine->CommitLocal(a.root, ea);

    auto eb =
        AppendItemEvent::ptr::Create(ae::CreateWith{b.domain}.with_id(301));
    eb->text = "from-b";
    b.engine->CommitLocal(b.root, eb);

    auto ec =
        AppendItemEvent::ptr::Create(ae::CreateWith{c.domain}.with_id(302));
    ec->text = "from-c";
    c.engine->CommitLocal(c.root, ec);

    CHECK(a.root->journal.size() >= 1);
    CHECK(b.root->journal.size() >= 1);
    CHECK(c.root->journal.size() == 1);
    CHECK(!a.state->IsGloballyConfirmed(a.root->journal[0].identity));
    a.engine->TryCollapse();
    b.engine->TryCollapse();
    c.engine->TryCollapse();
    CHECK(!a.root->journal.empty());
    CHECK(!b.root->journal.empty());
    CHECK(!c.root->journal.empty());

    c.online = true;
    a.engine->FlushPending();
    b.engine->FlushPending();
    c.engine->FlushPending();
    a.engine->FlushPending();
    b.engine->FlushPending();
    c.engine->FlushPending();

    CHECK(JournalsEqual(*a.root, *b.root));
    CHECK(JournalsEqual(*a.root, *c.root));
    CHECK(a.root->items.size() == 3);
    CHECK(b.root->items.size() == 3);
    CHECK(c.root->items.size() == 3);

    a.engine->TryCollapse();
    b.engine->TryCollapse();
    c.engine->TryCollapse();

    CHECK(a.root->journal.empty());
    CHECK(b.root->journal.empty());
    CHECK(c.root->journal.empty());
    CHECK(a.root->items.size() == 3);
    CHECK(b.root->items.size() == 3);
    CHECK(c.root->items.size() == 3);
  }

  return EXIT_SUCCESS;
}
