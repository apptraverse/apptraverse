#include <cstdlib>
#include <cstring>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/model_persistence.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

class PersistentDoc;
class PersistentDocChangedEvent;
class PersistentObserverNode;

class PersistentObserverNode : public NodeFor<PersistentObserverNode> {
  APPTRAVERSE_OBJECT(PersistentObserverNode, Node, 0)

 protected:
  PersistentObserverNode() = default;

 public:
  explicit PersistentObserverNode(ae::ObjProp prop) : NodeFor{prop}, run_id{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(run_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, run_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, run_id);
  }

  std::uint64_t run_id;
};

class PersistentDoc : public apptraverse::NodeFor<PersistentDoc> {
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
  PersistentObserverNode::ptr runtime_observer;

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

APPTRAVERSE_REGISTER(PersistentObserverNode);
APPTRAVERSE_REGISTER(PersistentDoc);
APPTRAVERSE_REGISTER(PersistentDocChangedEvent);

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

}  // namespace

void RunModelPersistenceRuntimeRefTest() {
  ae::ObjId const kDocId{0x0A11AD01u};
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = PersistentDoc::ptr::Create(
      ae::CreateWith{domain}.with_id(kDocId));
  CHECK(doc->obj_id == kDocId);
  InitializeRuntimeNode(*doc);
  auto runtime = PersistentObserverNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*runtime, *doc);
  runtime->run_id = 1;
  doc->runtime_observer = runtime;
  doc->label = "linked";

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*doc, snapshot);
  CHECK(doc->runtime_observer.is_valid());
  CHECK(doc->label == "linked");

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_doc = PersistentDoc::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kDocId));
  InitializeRuntimeNode(*load_doc);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_doc);
  CHECK(load_doc->label == "linked");
  CHECK(load_doc->runtime_observer.is_valid());
  CHECK(StorageContainsObjectId(load_storage, load_doc->runtime_observer.id()));
  CHECK(load_doc->runtime_observer->run_id == runtime->run_id);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunModelPersistenceRuntimeRefTest();
  return 0;
}
