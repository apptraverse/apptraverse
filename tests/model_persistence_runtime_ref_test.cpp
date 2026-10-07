#include <cstdlib>
#include <cstring>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/model_persistence.h"
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

APPTRAVERSE_REGISTER(PersistentDoc);
APPTRAVERSE_REGISTER(PersistentDocChangedEvent);

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

bool StorageContainsObjectId(ae::RamDomainStorage const& storage,
                               ae::ObjId id) {
  return storage.state.find(id) != storage.state.end();
}

bool StorageContainsClass(ae::RamDomainStorage const& storage,
                          std::uint32_t class_id) {
  for (auto const& [obj_id, class_map_opt] : storage.state) {
    (void)obj_id;
    if (!class_map_opt) {
      continue;
    }
    if (class_map_opt->count(class_id) != 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

void RunModelPersistenceRuntimeRefTest() {
  ae::ObjId const kDocId{0x0A11AD01u};
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = PersistentDoc::ptr::Create(
      ae::CreateWith{domain}.with_id(kDocId));
  CHECK(doc->obj_id == kDocId);
  InitializeRuntimeNode(*doc);
  auto runtime = NetworkState::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*runtime, *doc);
  doc->runtime_observer = runtime;
  doc->label = "linked";

  static_cast<void>(CommitNetworkAvailable(*runtime, 1));

  int reflected_objptr_fields = 0;
  ForEachReflectedObjPtrOn(*doc, [&](ae::ObjectPtrBase& ref) {
    ++reflected_objptr_fields;
    (void)ref;
  });
  CHECK(reflected_objptr_fields == 1);

  ByteSink snapshot;
  std::uint64_t const gen_before = doc->Generation();
  std::size_t const journal_before = doc->journal.size();
  SerializePersistentModelSnapshot(*doc, snapshot);
  CHECK(doc->runtime_observer.is_valid());
  CHECK(doc->runtime_observer.id() == runtime->obj_id);
  CHECK(doc->label == "linked");
  CHECK(doc->base.is_valid());
  CHECK(doc->Generation() == gen_before);
  CHECK(doc->journal.size() == journal_before);

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_doc = PersistentDoc::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kDocId));
  CHECK(load_doc->obj_id == kDocId);
  InitializeRuntimeNode(*load_doc);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_doc);
  CHECK(load_doc->label == "linked");
  CHECK(!load_doc->runtime_observer.is_valid());
  CHECK(!StorageContainsClass(load_storage, NetworkState::kClassId));
  CHECK(!StorageContainsObjectId(load_storage, runtime->obj_id));

  auto fresh_runtime = NetworkState::ptr::Create(ae::CreateWith{load_domain});
  InitializeRuntimeNode(*fresh_runtime, *load_doc);
  load_doc->runtime_observer = fresh_runtime;
  static_cast<void>(CommitNetworkInitializing(*fresh_runtime, 2));
  CHECK(fresh_runtime->GetAvailability() == NetworkAvailability::kInitializing);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunModelPersistenceRuntimeRefTest();
  return 0;
}
