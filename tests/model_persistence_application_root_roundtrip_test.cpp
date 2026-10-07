#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/registry.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/model_persistence.h"
#include "apptraverse/node.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_lifecycle.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

class PersistentDoc;
class PersistentDocChangedEvent;

class PersistentDoc : public NodeFor<PersistentDoc> {
  APPTRAVERSE_OBJECT(PersistentDoc, Node, 0)

 protected:
  PersistentDoc() = default;

 public:
  explicit PersistentDoc(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(runtime_observer))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, label, runtime_observer);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label, runtime_observer);
  }

  std::string label;
  NetworkState::ptr runtime_observer;

  void Apply(PersistentDocChangedEvent const& event);
};

class PersistentDocChangedEvent
    : public EventFor<PersistentDoc, PersistentDocChangedEvent> {
  APPTRAVERSE_OBJECT(PersistentDocChangedEvent, Event, 0)

 protected:
  PersistentDocChangedEvent() = default;

 public:
  explicit PersistentDocChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class PersistentApplication : public ae::Obj {
  APPTRAVERSE_OBJECT(PersistentApplication, ae::Obj, 0)

 protected:
  PersistentApplication() = default;

 public:
  explicit PersistentApplication(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(doc))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, doc);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, doc);
  }

  PersistentDoc::ptr doc;
};

APPTRAVERSE_REGISTER(PersistentDoc);
APPTRAVERSE_REGISTER(PersistentDocChangedEvent);
APPTRAVERSE_REGISTER(PersistentApplication);

#include "apptraverse/object_persistence_macros.h"
APPTRAVERSE_REGISTER_PERSISTED_OBJPTR_WALKER(PersistentDoc);

void PersistentDoc::Apply(PersistentDocChangedEvent const& event) {
  label = event.label;
}

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

std::unordered_set<std::uint32_t> SnapshotObjectIds(std::vector<std::uint8_t> const& bytes) {
  std::unordered_set<std::uint32_t> ids;
  if (bytes.size() < sizeof(std::uint32_t) * 2) {
    return ids;
  }
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  in.pos = sizeof(std::uint32_t);
  std::uint32_t layer_count = 0;
  in.read(&layer_count, sizeof(layer_count));
  for (std::uint32_t i = 0; i < layer_count; ++i) {
    std::uint32_t obj_id = 0;
    std::uint32_t class_id = 0;
    std::uint8_t version = 0;
    std::uint32_t size = 0;
    in.read(&obj_id, sizeof(obj_id));
    in.read(&class_id, sizeof(class_id));
    in.read(&version, sizeof(version));
    in.read(&size, sizeof(size));
    CHECK(in.ok && in.pos + size <= in.size);
    ids.insert(obj_id);
    in.pos += size;
  }
  return ids;
}

void RunOneRoundtripMaterializedOnlyRuntimeRef() {
  ae::ObjId const kAppId{0xA9900001u};
  ae::ObjId const kDocId{0xA9900002u};

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto app = PersistentApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId));
  auto doc = PersistentDoc::ptr::Create(ae::CreateWith{domain}.with_id(kDocId));
  InitializeRuntimeNode(*doc);
  app->doc = doc;

  auto runtime = NetworkState::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*runtime, *doc);
  doc->runtime_observer = runtime;
  doc->label = "roundtrip";

  auto event_a = PersistentDocChangedEvent::ptr::Create(ae::CreateWith{domain});
  event_a->label = "event_a";
  doc->Commit(event_a);
  auto event_b = PersistentDocChangedEvent::ptr::Create(ae::CreateWith{domain});
  event_b->label = "event_b";
  doc->Commit(event_b);

  CHECK(doc->base.is_valid());
  ae::ObjId::Type const base_id = doc->base.id().id();
  std::size_t const journal_before = doc->journal.size();
  std::uint64_t const gen_before = doc->Generation();

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*app, snapshot);
  CHECK(doc->runtime_observer.is_valid());
  CHECK(doc->journal.size() == journal_before);
  CHECK(doc->Generation() == gen_before);
  CHECK(doc->label == "event_b");

  auto const snapshot_ids = SnapshotObjectIds(snapshot.bytes);
  CHECK(snapshot_ids.count(kAppId.id()) != 0);
  CHECK(snapshot_ids.count(kDocId.id()) != 0);
  CHECK(snapshot_ids.count(base_id) != 0);
  CHECK(snapshot_ids.count(runtime->obj_id.id()) == 0);

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_app = PersistentApplication::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kAppId));
  auto load_doc = PersistentDoc::ptr::Create(ae::CreateWith{load_domain}.with_id(kDocId));
  InitializeRuntimeNode(*load_doc);
  load_app->doc = load_doc;

  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_app);

  CHECK(load_app->doc.is_valid());
  CHECK(load_app->doc->label == "event_b");
  CHECK(!load_app->doc->runtime_observer.is_valid());
  CHECK(load_app->doc->base.is_valid());
  CHECK(load_app->doc->journal.size() >= 2);

  auto event_c = PersistentDocChangedEvent::ptr::Create(ae::CreateWith{load_domain});
  event_c->label = "event_c";
  load_app->doc->Commit(event_c);
  CHECK(load_app->doc->label == "event_c");
  load_app->doc->CompactJournal(apptraverse::SystemUtcMicros());

  ByteSink snapshot2;
  SerializePersistentModelSnapshot(*load_app, snapshot2);
  ae::RamDomainStorage load_storage2;
  ae::Domain load_domain2{load_storage2};
  auto load_app2 = PersistentApplication::ptr::Create(
      ae::CreateWith{load_domain2}.with_id(kAppId));
  auto load_doc2 = PersistentDoc::ptr::Create(ae::CreateWith{load_domain2}.with_id(kDocId));
  InitializeRuntimeNode(*load_doc2);
  load_app2->doc = load_doc2;
  ByteSource in2;
  in2.data = snapshot2.bytes.data();
  in2.size = snapshot2.bytes.size();
  in2.pos = 0;
  LoadPersistentModelSnapshot(in2, load_domain2, load_storage2, *load_app2);
  CHECK(load_app2->doc->label == "event_c");
}

void RunOneRoundtripBaseCapturedRuntimeRef() {
  ae::ObjId const kAppId{0xA9900101u};
  ae::ObjId const kDocId{0xA9900102u};

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto app = PersistentApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId));
  auto doc = PersistentDoc::ptr::Create(ae::CreateWith{domain}.with_id(kDocId));
  auto runtime = NetworkState::ptr::Create(ae::CreateWith{domain});
  doc->runtime_observer = runtime;
  InitializeRuntimeNode(*doc);
  app->doc = doc;
  CHECK(doc->base.is_valid());
  CHECK(static_cast<PersistentDoc&>(*doc->base).runtime_observer.is_valid());

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*app, snapshot);

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_app = PersistentApplication::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kAppId));
  auto load_doc = PersistentDoc::ptr::Create(ae::CreateWith{load_domain}.with_id(kDocId));
  InitializeRuntimeNode(*load_doc);
  load_app->doc = load_doc;

  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_app);
  CHECK(!load_app->doc->runtime_observer.is_valid());
  CHECK(!static_cast<PersistentDoc&>(*load_app->doc->base).runtime_observer.is_valid());
}

}  // namespace

void RunModelPersistenceApplicationRootRoundtripTest() {
  for (int i = 0; i < 20; ++i) {
    RunOneRoundtripMaterializedOnlyRuntimeRef();
    RunOneRoundtripBaseCapturedRuntimeRef();
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunModelPersistenceApplicationRootRoundtripTest();
  std::cout << "apptraverse_model_persistence_application_root_roundtrip_test OK\n";
  return 0;
}
