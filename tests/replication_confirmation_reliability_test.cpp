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

  bool drop_confirmed{false};
  bool drop_confirmed_ack{false};
  std::size_t confirmed_sends{0};
  std::size_t confirmed_ack_sends{0};

  void Send(apptraverse::ReplicaId recipient,
            apptraverse::ReplicationMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());

    if (message->GetClassId() ==
        apptraverse::ConfirmedReplicationMessage::kClassId) {
      ++confirmed_sends;
      if (drop_confirmed) {
        return;
      }
    }
    if (message->GetClassId() ==
        apptraverse::ConfirmedAckReplicationMessage::kClassId) {
      ++confirmed_ack_sends;
      if (drop_confirmed_ack) {
        return;
      }
    }

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

  // Lost ConfirmedReplicationMessage, then restart + FlushPending.
  {
    MeshTransport mesh;
    mesh.drop_confirmed = true;

    ReplicaBundle a{id_a, "Doc"};
    ReplicaBundle b{id_b, "Doc"};
    BindReplica(mesh, a);
    BindReplica(mesh, b);
    a.engine->AddPeer(id_b);
    b.engine->AddPeer(id_a);

    auto event =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(200));
    event->text = "confirmed";
    a.engine->CommitLocal(a.root, event);

    CHECK(a.root->items.size() == 1);
    CHECK(b.root->items.size() == 1);
    CHECK(a.root->journal.empty());
    CHECK(!b.root->journal.empty());
    CHECK(a.state->IsGloballyConfirmed(
        a.state->confirmation_outgoing.empty()
            ? apptraverse::EventIdentity{id_a, 1}
            : a.state->confirmation_outgoing[0].identity));
    CHECK(!a.state->confirmation_outgoing.empty());
    CHECK(!a.state->confirmation_outgoing[0].acknowledged);
    CHECK(mesh.confirmed_sends >= 1);

    auto const identity = a.state->confirmation_outgoing[0].identity;

    a.root.Save();
    a.state.Save();
    a.engine.reset();
    mesh.drop_confirmed = false;

    a.root.Load();
    a.state.Load();
    BindReplica(mesh, a);
    a.engine->FlushPending();

    CHECK(b.root->journal.empty());
    CHECK(b.root->items.size() == 1);
    CHECK(a.state->confirmation_outgoing.empty());
    CHECK(a.state->FindOriginEvent(identity) == nullptr);
  }

  // Lost ConfirmedAck, retry confirmation, duplicate receive on B.
  {
    MeshTransport mesh;
    mesh.drop_confirmed_ack = true;

    ReplicaBundle a{id_a, "Doc"};
    ReplicaBundle b{id_b, "Doc"};
    BindReplica(mesh, a);
    BindReplica(mesh, b);
    a.engine->AddPeer(id_b);
    b.engine->AddPeer(id_a);

    auto event =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(210));
    event->text = "ack-lost";
    a.engine->CommitLocal(a.root, event);

    CHECK(a.root->journal.empty());
    CHECK(b.root->journal.empty());
    CHECK(!a.state->confirmation_outgoing.empty());
    CHECK(!a.state->confirmation_outgoing[0].acknowledged);
    CHECK(mesh.confirmed_ack_sends >= 1);

    auto const identity = a.state->confirmation_outgoing[0].identity;
    auto const confirmed_before = mesh.confirmed_sends;
    auto const ack_before = mesh.confirmed_ack_sends;

    mesh.drop_confirmed_ack = false;
    a.engine->FlushPending();

    CHECK(mesh.confirmed_sends > confirmed_before);
    CHECK(mesh.confirmed_ack_sends > ack_before);
    CHECK(a.state->confirmation_outgoing.empty());
    CHECK(a.state->FindOriginEvent(identity) == nullptr);
    CHECK(b.root->journal.empty());
    CHECK(b.root->items.size() == 1);
  }

  return EXIT_SUCCESS;
}
