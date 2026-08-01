#include <cassert>
#include <cstdint>
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
#include "apptraverse/replication_clock.h"
#include "apptraverse/replication_engine.h"
#include "apptraverse/replication_message.h"
#include "apptraverse/replication_state.h"
#include "apptraverse/replication_transport.h"

namespace apptraverse::test {

class ManualReplicationClock final : public apptraverse::IReplicationClock {
 public:
  std::uint64_t value{1};
  void Set(std::uint64_t next) { value = next; }
  std::uint64_t NowMicroseconds() override { return value; }
};

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
  ManualReplicationClock clock;
  std::unique_ptr<apptraverse::ReplicationEngine> engine;

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

  void Send(apptraverse::ReplicaId recipient,
            apptraverse::ReplicationMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());

    ae::RamDomainStorage* source = nullptr;
    for (auto const& [_, message_storage] : message_storages) {
      if (message_storage->state.find(message.id()) !=
          message_storage->state.end()) {
        source = message_storage;
        break;
      }
    }
    assert(source != nullptr);

    ae::RamDomainStorage transfer;
    transfer.state = source->state;
    source->state.clear();

    if (engines.count(recipient) == 0) {
      return;
    }

    for (auto const& entry : transfer.state) {
      storages.at(recipient)->state[entry.first] = entry.second;
    }

    apptraverse::ReplicationMessage::ptr incoming =
        apptraverse::ReplicationMessage::ptr::Declare(
            ae::CreateWith{*domains.at(recipient)}.with_id(message.id()));
    incoming.Load();
    engines.at(recipient)->Receive(incoming);
  }
};

void BindReplica(MeshTransport& mesh, ReplicaBundle& replica) {
  replica.engine = std::make_unique<apptraverse::ReplicationEngine>(
      replica.root, replica.state, replica.message_domain, mesh, replica.clock);
  mesh.engines[replica.id] = replica.engine.get();
  mesh.storages[replica.id] = &replica.storage;
  mesh.domains[replica.id] = &replica.domain;
  mesh.message_storages[replica.id] = &replica.message_storage;
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
  using apptraverse::test::MeshTransport;
  using apptraverse::test::ReplicaBundle;

  ReplicaId const id_a{1};
  ReplicaId const id_b{2};
  ReplicaId const id_c{3};

  MeshTransport mesh;
  ReplicaBundle a{id_a, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
  ReplicaBundle b{id_b, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
  a.clock.Set(10);
  BindReplica(mesh, a);
  BindReplica(mesh, b);
  a.engine->AddPeer(id_b);
  b.engine->AddPeer(id_a);

  auto const base_id = a.root->base.id();

  a.clock.Set(10);
  auto e1 =
      AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(200));
  e1->text = "one";
  a.engine->CommitLocal(a.root, e1);

  a.clock.Set(20);
  auto e2 =
      AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(201));
  e2->text = "two";
  a.engine->CommitLocal(a.root, e2);

  CHECK(a.root->journal.size() == 2);
  CHECK(b.root->journal.size() == 2);
  CHECK(a.root->base.id() == base_id);

  ReplicaBundle c{id_c, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
  BindReplica(mesh, c);

  a.engine->SendBootstrap(id_c);
  CHECK(!a.state->KnowsPeer(id_c));
  CHECK(c.root->journal.size() == 2);
  CHECK(c.root->items.size() == 2);
  CHECK(c.root->items[0] == "one");
  CHECK(c.root->items[1] == "two");
  CHECK(c.root->base.id() == base_id);

  // Without AddPeer, future events are not destined for C.
  a.clock.Set(30);
  auto e3 =
      AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(202));
  e3->text = "three";
  a.engine->CommitLocal(a.root, e3);
  CHECK(c.root->items.size() == 2);
  CHECK(a.state->FindOutgoing(a.root->journal.back().identity, id_c) == nullptr);

  a.engine->AddPeer(id_c);
  c.engine->AddPeer(id_a);

  a.clock.Set(40);
  auto e4 =
      AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(203));
  e4->text = "four";
  a.engine->CommitLocal(a.root, e4);
  CHECK(c.root->items.size() == 3);
  CHECK(c.root->items.back() == "four");
  CHECK(c.root->journal.size() == 3);
  CHECK(a.root->journal.size() == 4);

  return EXIT_SUCCESS;
}
