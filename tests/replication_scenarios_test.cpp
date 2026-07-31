#include <algorithm>
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
class SharedMember;
class SharedChild;
class SharedResource;
class AppendItemEvent;
class IntroduceMemberEvent;
class RenameMemberEvent;

class SharedResource : public ae::Obj {
  AE_OBJECT(SharedResource, Obj, 0)

 protected:
  SharedResource() = default;

 public:
  explicit SharedResource(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class SharedChild : public ae::Obj {
  AE_OBJECT(SharedChild, Obj, 0)

 protected:
  SharedChild() = default;

 public:
  explicit SharedChild(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(title), AE_MMBR(owner))

  std::string title;
  ae::ObjPtr<SharedMember> owner;
};

class SharedMember : public ae::Obj {
  AE_OBJECT(SharedMember, Obj, 0)

 protected:
  SharedMember() = default;

 public:
  explicit SharedMember(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(child), AE_MMBR(resource))

  std::string name;
  SharedChild::ptr child;
  SharedResource::ptr resource;
};

struct SharedItem {
  std::string text;
  SharedMember::ptr author;

  AE_REFLECT_MEMBERS(text, author)
};

class SharedDocument : public apptraverse::NodeFor<SharedDocument> {
  AE_OBJECT(SharedDocument, Node, 0)

 protected:
  SharedDocument() = default;

 public:
  explicit SharedDocument(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(title), AE_MMBR(items), AE_MMBR(members))

  std::string title;
  std::vector<SharedItem> items;
  std::vector<SharedMember::ptr> members;

  void Apply(AppendItemEvent const& event);
  void Apply(IntroduceMemberEvent const& event);
  void Apply(RenameMemberEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }
};

class AppendItemEvent
    : public apptraverse::EventFor<SharedDocument, AppendItemEvent> {
  AE_OBJECT(AppendItemEvent, Event, 0)

 protected:
  AppendItemEvent() = default;

 public:
  explicit AppendItemEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(author))

  std::string text;
  SharedMember::ptr author;
};

class IntroduceMemberEvent
    : public apptraverse::EventFor<SharedDocument, IntroduceMemberEvent> {
  AE_OBJECT(IntroduceMemberEvent, Event, 0)

 protected:
  IntroduceMemberEvent() = default;

 public:
  explicit IntroduceMemberEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(member))

  SharedMember::ptr member;
};

class RenameMemberEvent
    : public apptraverse::EventFor<SharedDocument, RenameMemberEvent> {
  AE_OBJECT(RenameMemberEvent, Event, 0)

 protected:
  RenameMemberEvent() = default;

 public:
  explicit RenameMemberEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(member), AE_MMBR(name))

  SharedMember::ptr member;
  std::string name;
};

void SharedDocument::Apply(AppendItemEvent const& event) {
  items.push_back(SharedItem{event.text, event.author});
}

void SharedDocument::Apply(IntroduceMemberEvent const& event) {
  assert(event.member.is_valid());
  members.push_back(event.member);
}

void SharedDocument::Apply(RenameMemberEvent const& event) {
  assert(event.member.is_valid());
  event.member.Load();
  assert(event.member.is_loaded());
  event.member->name = event.name;
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
  struct SendRecord {
    apptraverse::ReplicaId recipient;
    bool is_event{false};
    apptraverse::EventIdentity event_identity{};
  };

  std::map<apptraverse::ReplicaId, apptraverse::ReplicationEngine*> engines;
  std::map<apptraverse::ReplicaId, ae::RamDomainStorage*> storages;
  std::map<apptraverse::ReplicaId, ae::Domain*> domains;
  std::map<apptraverse::ReplicaId, ae::RamDomainStorage*> message_storages;
  std::map<apptraverse::ReplicaId, bool*> online;
  std::vector<SendRecord> sends;
  bool drop_acks{false};
  bool deliver{true};
  std::vector<
      std::pair<apptraverse::ReplicaId, apptraverse::ReplicationMessage::ptr>>
      delayed;
  ae::RamDomainStorage last_captured_transfer_;
  bool record_is_event_transfer_{false};

  void Send(apptraverse::ReplicaId recipient,
            apptraverse::ReplicationMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());

    SendRecord record;
    record.recipient = recipient;
    if (message->GetClassId() ==
        apptraverse::EventReplicationMessage::kClassId) {
      auto* event_message =
          message.Load().as<apptraverse::EventReplicationMessage>();
      assert(event_message != nullptr);
      record.is_event = true;
      record.event_identity = event_message->identity;
    }
    sends.push_back(record);

    if (online.count(recipient) != 0 && online[recipient] != nullptr &&
        !*online[recipient]) {
      return;
    }
    if (engines.count(recipient) == 0) {
      return;
    }

    if (message->GetClassId() ==
            apptraverse::AckReplicationMessage::kClassId &&
        drop_acks) {
      return;
    }

    if (!deliver) {
      delayed.emplace_back(recipient, std::move(message));
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

    if (record_is_event_transfer_ &&
        message->GetClassId() ==
            apptraverse::EventReplicationMessage::kClassId) {
      last_captured_transfer_.state = source->state;
    }

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

  void FlushDelayed() {
    auto pending = std::move(delayed);
    delayed.clear();
    for (auto& entry : pending) {
      DeliverNow(entry.first, entry.second);
    }
  }

  void CaptureNextEventTransfer(bool enabled) {
    record_is_event_transfer_ = enabled;
  }

  ae::RamDomainStorage const& LastCapturedTransfer() const {
    return last_captured_transfer_;
  }

  std::size_t CountEventSends(apptraverse::ReplicaId recipient,
                              apptraverse::EventIdentity const& identity) const {
    std::size_t count = 0;
    for (auto const& send : sends) {
      if (send.recipient == recipient && send.is_event &&
          send.event_identity == identity) {
        ++count;
      }
    }
    return count;
  }

  std::size_t CountEventSendsFor(
      apptraverse::EventIdentity const& identity) const {
    std::size_t count = 0;
    for (auto const& send : sends) {
      if (send.is_event && send.event_identity == identity) {
        ++count;
      }
    }
    return count;
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

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId::Type id) {
  return storage.state.find(ae::ObjId{id}) != storage.state.end();
}

}  // namespace

int main() {
  using apptraverse::ReplicaId;
  using apptraverse::test::AppendItemEvent;
  using apptraverse::test::BindReplica;
  using apptraverse::test::IntroduceMemberEvent;
  using apptraverse::test::JournalsEqual;
  using apptraverse::test::MeshTransport;
  using apptraverse::test::RenameMemberEvent;
  using apptraverse::test::ReplicaBundle;
  using apptraverse::test::SharedChild;
  using apptraverse::test::SharedMember;
  using apptraverse::test::SharedResource;

  ReplicaId const id_a{1};
  ReplicaId const id_b{2};
  ReplicaId const id_c{3};

  // A. Two replicas: deliver once, no forwarding by receiver.
  {
    MeshTransport mesh;
    ReplicaBundle a{id_a, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    ReplicaBundle b{id_b, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    BindReplica(mesh, a);
    BindReplica(mesh, b);
    a.engine->AddPeer(id_b);
    b.engine->AddPeer(id_a);

    auto event =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(200));
    event->text = "hello";
    a.engine->CommitLocal(a.root, event);

    CHECK(a.root->items.size() == 1);
    CHECK(b.root->items.size() == 1);
    CHECK(b.root->items[0].text == "hello");
    CHECK(JournalsEqual(*a.root, *b.root));

    auto const identity = a.root->journal.empty()
                              ? apptraverse::EventIdentity{id_a, 1}
                              : a.root->journal[0].identity;
    CHECK(mesh.CountEventSends(id_b, identity) == 1);
    CHECK(mesh.CountEventSends(id_a, identity) == 0);
    CHECK(mesh.CountEventSendsFor(identity) == 1);
  }

  // B + C. Introduce unknown subgraph, then reference-only follow-up.
  {
    MeshTransport mesh;
    ReplicaBundle a{id_a, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    ReplicaBundle b{id_b, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};

    auto resource_a =
        SharedResource::ptr::Create(ae::CreateWith{a.domain}.with_id(500));
    resource_a->label = "theme-a";
    auto resource_b =
        SharedResource::ptr::Create(ae::CreateWith{b.domain}.with_id(500));
    resource_b->label = "theme-b";

    BindReplica(mesh, a);
    BindReplica(mesh, b);
    a.engine->AddPeer(id_b);
    b.engine->AddPeer(id_a);

    auto child =
        SharedChild::ptr::Create(ae::CreateWith{a.domain}.with_id(301));
    child->title = "child";
    auto member =
        SharedMember::ptr::Create(ae::CreateWith{a.domain}.with_id(300));
    member->name = "Ada";
    member->child = child;
    child->owner = member;
    member->resource = resource_a;
    member->resource.Reset();
    member->resource.SetFlags(ae::ObjFlags::kUnloadedByDefault);

    auto introduce = IntroduceMemberEvent::ptr::Create(
        ae::CreateWith{a.domain}.with_id(201));
    introduce->member = member;

    mesh.CaptureNextEventTransfer(true);
    a.engine->CommitLocal(a.root, introduce);
    mesh.CaptureNextEventTransfer(false);

    CHECK(a.state->IsKnownShared(ae::ObjId{300}));
    CHECK(a.state->IsKnownShared(ae::ObjId{301}));
    CHECK(ContainsObj(mesh.LastCapturedTransfer(), 201));
    CHECK(ContainsObj(mesh.LastCapturedTransfer(), 300));
    CHECK(ContainsObj(mesh.LastCapturedTransfer(), 301));
    CHECK(!ContainsObj(mesh.LastCapturedTransfer(), 500));

    CHECK(a.root->members.size() == 1);
    CHECK(b.root->members.size() == 1);
    CHECK(b.root->members[0].id().id() == 300);
    CHECK(b.root->members[0]->child.id().id() == 301);
    CHECK(b.root->members[0]->child->owner.id().id() == 300);
    CHECK(b.root->members[0]->resource.id().id() == 500);
    b.root->members[0]->resource.Load();
    CHECK(b.root->members[0]->resource.Load().get() == resource_b.Load().get());
    CHECK(b.root->members[0]->resource->label == "theme-b");

    auto rename =
        RenameMemberEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(202));
    rename->member = a.root->members[0];
    rename->name = "Ada Lovelace";

    mesh.CaptureNextEventTransfer(true);
    a.engine->CommitLocal(a.root, rename);
    mesh.CaptureNextEventTransfer(false);

    CHECK(a.root->members[0]->name == "Ada Lovelace");
    CHECK(b.root->members[0]->name == "Ada Lovelace");
    CHECK(ContainsObj(mesh.LastCapturedTransfer(), 202));
    CHECK(!ContainsObj(mesh.LastCapturedTransfer(), 300));
    CHECK(!ContainsObj(mesh.LastCapturedTransfer(), 301));
  }

  // D. Lost ack, restart, retry, dedupe, re-ack.
  {
    MeshTransport mesh;
    mesh.drop_acks = true;

    ReplicaBundle a{id_a, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    ReplicaBundle b{id_b, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    BindReplica(mesh, a);
    BindReplica(mesh, b);
    a.engine->AddPeer(id_b);
    b.engine->AddPeer(id_a);

    auto event =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(210));
    event->text = "retry";
    a.engine->CommitLocal(a.root, event);
    CHECK(b.root->items.size() == 1);
    CHECK(a.state->outgoing.size() == 1);
    CHECK(!a.state->outgoing[0].acknowledged);

    auto const identity = a.state->outgoing[0].event_identity;
    CHECK(mesh.CountEventSends(id_b, identity) == 1);

    a.engine.reset();
    mesh.drop_acks = false;
    BindReplica(mesh, a);
    a.engine->FlushPending();

    CHECK(mesh.CountEventSends(id_b, identity) == 2);
    CHECK(b.root->items.size() == 1);
    CHECK(a.state->FindOutgoing(identity, id_b) == nullptr ||
          a.state->FindOutgoing(identity, id_b)->acknowledged);
    CHECK(!a.state->outgoing.empty() || a.root->journal.empty());
  }

  // E + F. Three replicas, origin-only fan-out, out-of-order delivery.
  {
    MeshTransport mesh;
    mesh.deliver = false;

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

    // Block collapse while verifying canonical journal convergence.
    ReplicaId const id_blocker{99};
    a.engine->AddPeer(id_blocker);
    b.engine->AddPeer(id_blocker);
    c.engine->AddPeer(id_blocker);

    auto ea =
        AppendItemEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(220));
    ea->text = "A";
    auto eb =
        AppendItemEvent::ptr::Create(ae::CreateWith{b.domain}.with_id(221));
    eb->text = "B";
    auto ec =
        AppendItemEvent::ptr::Create(ae::CreateWith{c.domain}.with_id(222));
    ec->text = "C";

    a.engine->CommitLocal(a.root, ea);
    b.engine->CommitLocal(b.root, eb);
    c.engine->CommitLocal(c.root, ec);

    auto const identity_a = a.state->origin_events[0].identity;
    auto const identity_b = b.state->origin_events[0].identity;
    auto const identity_c = c.state->origin_events[0].identity;
    CHECK(mesh.CountEventSendsFor(identity_a) == 3);
    CHECK(mesh.CountEventSendsFor(identity_b) == 3);
    CHECK(mesh.CountEventSendsFor(identity_c) == 3);
    CHECK(mesh.CountEventSends(id_a, identity_a) == 0);
    CHECK(mesh.CountEventSends(id_b, identity_b) == 0);
    CHECK(mesh.CountEventSends(id_c, identity_c) == 0);

    std::reverse(mesh.delayed.begin(), mesh.delayed.end());
    mesh.deliver = true;
    for (int round = 0; round < 8; ++round) {
      mesh.FlushDelayed();
      a.engine->FlushPending();
      b.engine->FlushPending();
      c.engine->FlushPending();
    }

    CHECK(a.root->journal.size() == 3);
    CHECK(JournalsEqual(*a.root, *b.root));
    CHECK(JournalsEqual(*a.root, *c.root));
    CHECK(a.root->items.size() == 3);
    CHECK(b.root->items.size() == 3);
    CHECK(c.root->items.size() == 3);

    for (std::size_t i = 0; i < a.root->items.size(); ++i) {
      CHECK(a.root->items[i].text == b.root->items[i].text);
      CHECK(a.root->items[i].text == c.root->items[i].text);
    }
  }

  return EXIT_SUCCESS;
}
