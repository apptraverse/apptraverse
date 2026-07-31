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

  ReplicaBundle(apptraverse::ReplicaId replica_id, std::string title)
      : id{replica_id},
        domain{ae::Now(), storage},
        message_domain{ae::Now(), message_storage} {
    auto base =
        SharedDocument::ptr::Create(ae::CreateWith{domain}.with_id(101));
    base->title = "unset";
    root = SharedDocument::ptr::Create(ae::CreateWith{domain}.with_id(100));
    root->title = std::move(title);
    root->base = base;
    root->CaptureBaseStateForTest();
    state = apptraverse::ReplicationState::ptr::Create(
        ae::CreateWith{domain}.with_id(10));
    state->local_replica_id = id;
  }
};

class MeshTransport final : public apptraverse::IReplicationTransport {
 public:
  std::map<apptraverse::ReplicaId, apptraverse::ReplicationEngine*> engines;
  std::map<apptraverse::ReplicaId, ae::RamDomainStorage*> storages;
  std::map<apptraverse::ReplicaId, ae::Domain*> domains;
  std::map<apptraverse::ReplicaId, ae::RamDomainStorage*> message_storages;
  std::size_t send_count{0};

  void Send(apptraverse::ReplicaId recipient,
            apptraverse::ReplicationMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    ++send_count;
    if (engines.count(recipient) == 0) {
      return;
    }

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
      storages.at(recipient)->state[entry.first] = entry.second;
    }
    source->state.clear();

    apptraverse::ReplicationMessage::ptr incoming =
        apptraverse::ReplicationMessage::ptr::Declare(
            ae::CreateWith{*domains.at(recipient)}.with_id(message.id()));
    incoming.Load();
    engines.at(recipient)->Receive(incoming);
  }
};

void BindReplica(MeshTransport& mesh, ReplicaBundle& replica) {
  replica.engine = std::make_unique<apptraverse::ReplicationEngine>(
      replica.root, replica.state, replica.message_domain, mesh);
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
  ReplicaBundle a{id_a, "Doc"};
  ReplicaBundle b{id_b, "Doc"};
  ReplicaBundle c{id_c, "Doc"};
  BindReplica(mesh, a);
  BindReplica(mesh, b);
  BindReplica(mesh, c);
  a.engine->AddPeer(id_b);
  a.engine->AddPeer(id_c);
  b.engine->AddPeer(id_a);
  b.engine->AddPeer(id_c);
  c.engine->AddPeer(id_a);
  c.engine->AddPeer(id_b);

  auto event =
      AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(200));
  event->text = "once";
  a.engine->CommitLocal(a.root, event);

  CHECK(a.root->items.size() == 1);
  CHECK(b.root->items.size() == 1);
  CHECK(c.root->items.size() == 1);
  CHECK(a.root->items[0] == "once");
  CHECK(b.root->items[0] == "once");
  CHECK(c.root->items[0] == "once");
  CHECK(a.root->journal.empty());
  CHECK(b.root->journal.empty());
  CHECK(c.root->journal.empty());
  CHECK(a.state->outgoing.empty());
  CHECK(a.state->confirmation_outgoing.empty());
  CHECK(b.state->outgoing.empty());
  CHECK(c.state->outgoing.empty());

  auto const sends_after_commit = mesh.send_count;
  a.engine->FlushPending();
  b.engine->FlushPending();
  c.engine->FlushPending();
  CHECK(mesh.send_count == sends_after_commit);

  return EXIT_SUCCESS;
}
