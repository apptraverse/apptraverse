#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

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
class SharedResource;
class AppendTitleEvent;

class SharedResource : public ae::Obj {
  AE_OBJECT(SharedResource, Obj, 0)

 protected:
  SharedResource() = default;

 public:
  explicit SharedResource(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(path))

  std::string path;
};

class SharedDocument : public apptraverse::NodeFor<SharedDocument> {
  AE_OBJECT(SharedDocument, Node, 0)

 protected:
  SharedDocument() = default;

 public:
  explicit SharedDocument(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(title), AE_MMBR(resource))

  std::string title;
  SharedResource::ptr resource;

  void Apply(AppendTitleEvent const& event);
  void CaptureBaseStateForTest() { CaptureBaseState(); }

  SharedDocument::ptr InstantiatePrefab(std::string instance_title);
};

class AppendTitleEvent
    : public apptraverse::EventFor<SharedDocument, AppendTitleEvent> {
  AE_OBJECT(AppendTitleEvent, Event, 0)

 protected:
  AppendTitleEvent() = default;

 public:
  explicit AppendTitleEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(suffix))

  std::string suffix;
};

void SharedDocument::Apply(AppendTitleEvent const& event) {
  title += event.suffix;
}

SharedDocument::ptr SharedDocument::InstantiatePrefab(
    std::string instance_title) {
  assert(domain != nullptr);
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(journal.empty());

  auto prefab = SharedDocument::ptr::MakeFromThis(this);
  prefab.Save();
  if (base.is_valid()) {
    base.Save();
  }

  auto instance = prefab.Clone();
  auto instance_base = base.Clone();
  assert(instance.is_valid());
  assert(instance_base.is_valid());
  instance->base = instance_base;
  instance->title = std::move(instance_title);
  instance->resource = resource;
  if (instance->resource.is_valid()) {
    instance->resource.Reset();
    instance->resource.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  }
  instance->CaptureBaseState();
  return instance;
}

class WindowPresenter : public ae::Obj {
  AE_OBJECT(WindowPresenter, Obj, 0)

 protected:
  WindowPresenter() = default;

 public:
  explicit WindowPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(document), AE_MMBR(hwnd), AE_MMBR(resource))

  SharedDocument::ptr document;
  std::uint64_t hwnd{0};
  SharedResource::ptr resource;
};

class ConsolePresenter : public ae::Obj {
  AE_OBJECT(ConsolePresenter, Obj, 0)

 protected:
  ConsolePresenter() = default;

 public:
  explicit ConsolePresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(document), AE_MMBR(cursor), AE_MMBR(resource))

  SharedDocument::ptr document;
  std::string cursor;
  SharedResource::ptr resource;
};

static_assert(!std::is_base_of_v<apptraverse::Node, WindowPresenter>);
static_assert(!std::is_base_of_v<apptraverse::Node, ConsolePresenter>);
static_assert(!std::is_base_of_v<apptraverse::Node, SharedResource>);

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
  ae::RamDomainStorage last_transfer;

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
    last_transfer.state = transfer.state;

    if (engines.count(recipient) == 0) {
      return;
    }

    auto* storage = storages.at(recipient);
    auto* domain = domains.at(recipient);
    auto* engine = engines.at(recipient);
    for (auto const& entry : transfer.state) {
      storage->state[entry.first] = entry.second;
    }

    apptraverse::ReplicationMessage::ptr incoming =
        apptraverse::ReplicationMessage::ptr::Declare(
            ae::CreateWith{*domain}.with_id(message.id()));
    incoming.Load();
    engine->Receive(incoming);
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

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId::Type id) {
  return storage.state.find(ae::ObjId{id}) != storage.state.end();
}

}  // namespace

int main() {
  using apptraverse::ReplicaId;
  using apptraverse::test::AppendTitleEvent;
  using apptraverse::test::BindReplica;
  using apptraverse::test::ConsolePresenter;
  using apptraverse::test::MeshTransport;
  using apptraverse::test::ReplicaBundle;
  using apptraverse::test::SharedDocument;
  using apptraverse::test::SharedResource;
  using apptraverse::test::WindowPresenter;

  // Prefab: clone shared subgraph, keep static resource id-only.
  {
    ae::RamDomainStorage storage;
    ae::Domain domain{ae::Now(), storage};

    auto resource =
        SharedResource::ptr::Create(ae::CreateWith{domain}.with_id(50));
    resource->path = "/static/icon";
    resource.Save();

    auto base =
        SharedDocument::ptr::Create(ae::CreateWith{domain}.with_id(11));
    base->title = "";

    auto prefab =
        SharedDocument::ptr::Create(ae::CreateWith{domain}.with_id(10));
    prefab->title = "";
    prefab->base = base;
    prefab->resource = resource;
    prefab->resource.Reset();
    prefab->resource.SetFlags(ae::ObjFlags::kUnloadedByDefault);
    prefab->CaptureBaseStateForTest();
    prefab.Save();

    auto instance = prefab->InstantiatePrefab("Instance");
    CHECK(instance.id() != prefab.id());
    CHECK(instance->base.id() != prefab->base.id());
    CHECK(instance->title == "Instance");
    CHECK(instance->resource.id().id() == 50);
    CHECK(!instance->resource.is_loaded());
    instance->resource.Load();
    CHECK(instance->resource.Load().get() == resource.Load().get());
  }

  // G. Different local presenters for the same shared object.
  {
    ReplicaId const id_a{1};
    ReplicaId const id_b{2};
    MeshTransport mesh;

    ReplicaBundle a{id_a, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    ReplicaBundle b{id_b, ae::ObjId{100}, ae::ObjId{101}, ae::ObjId{10}, "Doc"};
    a.clock.Set(11);

    auto resource_a =
        SharedResource::ptr::Create(ae::CreateWith{a.domain}.with_id(500));
    resource_a->path = "a-theme";
    auto resource_b =
        SharedResource::ptr::Create(ae::CreateWith{b.domain}.with_id(500));
    resource_b->path = "b-theme";

    auto window = WindowPresenter::ptr::Create(
        ae::CreateWith{a.domain}.with_id(900));
    window->document = a.root;
    window->hwnd = 0xABCDu;
    window->resource = resource_a;

    auto console = ConsolePresenter::ptr::Create(
        ae::CreateWith{b.domain}.with_id(901));
    console->document = b.root;
    console->cursor = "block";
    console->resource = resource_b;

    BindReplica(mesh, a);
    BindReplica(mesh, b);
    a.engine->AddPeer(id_b);
    b.engine->AddPeer(id_a);

    auto event =
        AppendTitleEvent::ptr::Create(ae::CreateWith{a.domain}.with_id(200));
    event->suffix = "!";
    a.engine->CommitLocal(a.root, event);

    CHECK(a.root->title == "Doc!");
    CHECK(b.root->title == "Doc!");
    CHECK(a.root.id() == b.root.id());
    CHECK(window->GetClassId() != console->GetClassId());
    CHECK(window->hwnd == 0xABCDu);
    CHECK(console->cursor == "block");
    CHECK(window->resource.Load().get() == resource_a.Load().get());
    CHECK(console->resource.Load().get() == resource_b.Load().get());
    CHECK(!ContainsObj(mesh.last_transfer, 900));
    CHECK(!ContainsObj(mesh.last_transfer, 901));
    CHECK(!ContainsObj(mesh.last_transfer, 500));
    CHECK(a.root->journal.size() == 1);
    CHECK(b.root->journal.size() == 1);
  }

  return EXIT_SUCCESS;
}
