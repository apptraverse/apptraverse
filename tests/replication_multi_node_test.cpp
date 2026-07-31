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

class SharedApplication;
class SharedDocumentNode;
class SharedMemberNode;
class RenameApplicationEvent;
class AppendDocumentEvent;
class RenameMemberEvent;

class SharedDocumentNode : public apptraverse::NodeFor<SharedDocumentNode> {
  AE_OBJECT(SharedDocumentNode, Node, 0)

 protected:
  SharedDocumentNode() = default;

 public:
  explicit SharedDocumentNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(body))

  std::string body;

  void Apply(AppendDocumentEvent const& event);
  void CaptureBaseStateForTest() { CaptureBaseState(); }
};

class SharedMemberNode : public apptraverse::NodeFor<SharedMemberNode> {
  AE_OBJECT(SharedMemberNode, Node, 0)

 protected:
  SharedMemberNode() = default;

 public:
  explicit SharedMemberNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;

  void Apply(RenameMemberEvent const& event);
  void CaptureBaseStateForTest() { CaptureBaseState(); }
};

class SharedApplication : public apptraverse::NodeFor<SharedApplication> {
  AE_OBJECT(SharedApplication, Node, 0)

 protected:
  SharedApplication() = default;

 public:
  explicit SharedApplication(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(title), AE_MMBR(document), AE_MMBR(member))

  std::string title;
  SharedDocumentNode::ptr document;
  SharedMemberNode::ptr member;

  void Apply(RenameApplicationEvent const& event);
  void CaptureBaseStateForTest() { CaptureBaseState(); }
};

class RenameApplicationEvent
    : public apptraverse::EventFor<SharedApplication, RenameApplicationEvent> {
  AE_OBJECT(RenameApplicationEvent, Event, 0)

 protected:
  RenameApplicationEvent() = default;

 public:
  explicit RenameApplicationEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(title))

  std::string title;
};

class AppendDocumentEvent
    : public apptraverse::EventFor<SharedDocumentNode, AppendDocumentEvent> {
  AE_OBJECT(AppendDocumentEvent, Event, 0)

 protected:
  AppendDocumentEvent() = default;

 public:
  explicit AppendDocumentEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

class RenameMemberEvent
    : public apptraverse::EventFor<SharedMemberNode, RenameMemberEvent> {
  AE_OBJECT(RenameMemberEvent, Event, 0)

 protected:
  RenameMemberEvent() = default;

 public:
  explicit RenameMemberEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

void SharedApplication::Apply(RenameApplicationEvent const& event) {
  title = event.title;
}

void SharedDocumentNode::Apply(AppendDocumentEvent const& event) {
  body += event.suffix;
}

void SharedMemberNode::Apply(RenameMemberEvent const& event) {
  name = event.name;
}

struct ReplicaBundle {
  apptraverse::ReplicaId id;
  ae::RamDomainStorage storage;
  ae::Domain domain;
  ae::RamDomainStorage message_storage;
  ae::Domain message_domain;
  SharedApplication::ptr root;
  SharedDocumentNode::ptr document;
  SharedMemberNode::ptr member;
  apptraverse::ReplicationState::ptr state;
  std::unique_ptr<apptraverse::ReplicationEngine> engine;

  ReplicaBundle(apptraverse::ReplicaId replica_id, std::string title)
      : id{replica_id},
        domain{ae::Now(), storage},
        message_domain{ae::Now(), message_storage} {
    auto app_base = SharedApplication::ptr::Create(
        ae::CreateWith{domain}.with_id(101));
    app_base->title = "unset";

    auto document_base = SharedDocumentNode::ptr::Create(
        ae::CreateWith{domain}.with_id(201));
    document_base->body = "unset";

    auto member_base = SharedMemberNode::ptr::Create(
        ae::CreateWith{domain}.with_id(301));
    member_base->name = "unset";

    document = SharedDocumentNode::ptr::Create(
        ae::CreateWith{domain}.with_id(200));
    document->body = "Body";
    document->base = document_base;
    document->CaptureBaseStateForTest();

    member =
        SharedMemberNode::ptr::Create(ae::CreateWith{domain}.with_id(300));
    member->name = "Member";
    member->base = member_base;
    member->CaptureBaseStateForTest();

    root =
        SharedApplication::ptr::Create(ae::CreateWith{domain}.with_id(100));
    root->title = std::move(title);
    root->document = document;
    root->member = member;
    root->base = app_base;
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
  std::size_t event_send_count{0};

  void Send(apptraverse::ReplicaId recipient,
            apptraverse::ReplicationMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    if (engines.count(recipient) == 0) {
      return;
    }
    if (message->GetClassId() ==
        apptraverse::EventReplicationMessage::kClassId) {
      ++event_send_count;
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
  using apptraverse::test::AppendDocumentEvent;
  using apptraverse::test::BindReplica;
  using apptraverse::test::MeshTransport;
  using apptraverse::test::RenameApplicationEvent;
  using apptraverse::test::RenameMemberEvent;
  using apptraverse::test::ReplicaBundle;

  ReplicaId const id_a{1};
  ReplicaId const id_b{2};
  ReplicaId const id_c{3};

  MeshTransport mesh;
  ReplicaBundle a{id_a, "App"};
  ReplicaBundle b{id_b, "App"};
  BindReplica(mesh, a);
  BindReplica(mesh, b);
  a.engine->AddPeer(id_b);
  b.engine->AddPeer(id_a);

  // Keep journals visible for placement checks.
  ReplicaId const id_blocker{99};
  a.engine->AddPeer(id_blocker);
  b.engine->AddPeer(id_blocker);

  CHECK(a.state->IsKnownSharedNode(a.root.id()));
  CHECK(a.state->IsKnownSharedNode(a.document.id()));
  CHECK(a.state->IsKnownSharedNode(a.member.id()));
  CHECK(!a.state->IsKnownSharedNode(a.root->base.id()));

  auto rename_app = RenameApplicationEvent::ptr::Create(
      ae::CreateWith{a.domain}.with_id(400));
  rename_app->title = "App-A";
  a.engine->CommitLocal(a.root, rename_app);

  auto append_doc = AppendDocumentEvent::ptr::Create(
      ae::CreateWith{a.domain}.with_id(401));
  append_doc->suffix = "-X";
  a.engine->CommitLocal(a.document, append_doc);

  auto rename_member = RenameMemberEvent::ptr::Create(
      ae::CreateWith{a.domain}.with_id(402));
  rename_member->name = "Member-A";
  a.engine->CommitLocal(a.member, rename_member);

  CHECK(a.root->journal.size() == 1);
  CHECK(a.document->journal.size() == 1);
  CHECK(a.member->journal.size() == 1);
  CHECK(b.root->journal.size() == 1);
  CHECK(b.document->journal.size() == 1);
  CHECK(b.member->journal.size() == 1);
  CHECK(a.root->journal[0].event.id().id() == 400);
  CHECK(a.document->journal[0].event.id().id() == 401);
  CHECK(a.member->journal[0].event.id().id() == 402);
  CHECK(a.root->title == "App-A");
  CHECK(b.root->title == "App-A");
  CHECK(a.document->body == "Body-X");
  CHECK(b.document->body == "Body-X");
  CHECK(a.member->name == "Member-A");
  CHECK(b.member->name == "Member-A");
  CHECK(b.state->outgoing.empty());

  auto rename_member_b = RenameMemberEvent::ptr::Create(
      ae::CreateWith{b.domain}.with_id(403));
  rename_member_b->name = "Member-B";
  auto const sends_before = mesh.event_send_count;
  b.engine->CommitLocal(b.member, rename_member_b);
  CHECK(mesh.event_send_count > sends_before);
  CHECK(a.member->name == "Member-B");
  CHECK(b.member->name == "Member-B");
  CHECK(a.member->journal.size() == 2);
  CHECK(a.root->journal.size() == 1);
  CHECK(a.document->journal.size() == 1);

  // Persist shared graph + replication state, rebuild engines, continue.
  a.root.Save();
  a.state.Save();
  b.root.Save();
  b.state.Save();
  a.engine.reset();
  b.engine.reset();

  a.root.Load();
  a.document = a.root->document;
  a.member = a.root->member;
  a.state.Load();
  b.root.Load();
  b.document = b.root->document;
  b.member = b.root->member;
  b.state.Load();

  BindReplica(mesh, a);
  BindReplica(mesh, b);
  a.engine->FlushPending();
  b.engine->FlushPending();

  CHECK(a.member->name == "Member-B");
  CHECK(b.document->body == "Body-X");

  ReplicaBundle c{id_c, "App"};
  BindReplica(mesh, c);
  a.engine->SendBootstrap(id_c);
  b.engine->AddPeer(id_c);
  c.engine->AddPeer(id_a);
  c.engine->AddPeer(id_b);
  c.document = c.root->document;
  c.member = c.root->member;
  c.document.Load();
  c.member.Load();

  CHECK(c.root.id() == a.root.id());
  CHECK(c.document.id() == a.document.id());
  CHECK(c.member.id() == a.member.id());
  CHECK(c.root.Load().get() != a.root.Load().get());
  CHECK(c.root->title == a.root->title);
  CHECK(c.document->body == a.document->body);
  CHECK(c.member->name == a.member->name);
  CHECK(c.root->journal.size() == a.root->journal.size());
  CHECK(c.document->journal.size() == a.document->journal.size());
  CHECK(c.member->journal.size() == a.member->journal.size());

  auto future = AppendDocumentEvent::ptr::Create(
      ae::CreateWith{a.domain}.with_id(404));
  future->suffix = "-Y";
  a.engine->CommitLocal(a.document, future);
  CHECK(c.document->body == "Body-X-Y");
  CHECK(a.state->FindOutgoing(a.document->journal.back().identity, id_c) !=
        nullptr);

  return EXIT_SUCCESS;
}
