#include <cstdlib>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
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
  ae::ObjId observer_id;
  ae::ObjId doc_base_id;

  {
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
    observer_id = runtime.id();
    CHECK(doc->base.is_valid());
    doc_base_id = doc->base->obj_id;

    ae::DomainGraph save_graph{&domain};
    ae::Ptr<ae::Obj> save_root = domain.Find(kDocId);
    CHECK(save_root);
    CHECK(save_graph.SaveRoot(save_root, kDocId).IsOk());
    CHECK(doc->runtime_observer.is_valid());
    CHECK(doc->label == "linked");
    CHECK(doc->runtime_observer.id() == observer_id);
  }

  {
    ae::Domain load_domain{storage};
    ae::DomainGraph load_graph{&load_domain};
    ae::Ptr<ae::Obj> loaded_obj;
    CHECK(load_graph.LoadRoot(kDocId, loaded_obj).IsOk());
    ae::Ptr<PersistentDoc> load_doc{loaded_obj};

    CHECK(load_doc->obj_id == kDocId);
    CHECK(load_doc->label == "linked");
    CHECK(load_doc->runtime_observer.is_valid());
    CHECK(load_doc->runtime_observer.id() == observer_id);
    CHECK(StorageContainsObjectId(storage, observer_id));
    CHECK(load_doc->runtime_observer->run_id == 1);

    ae::Ptr<ae::Obj> const found_observer = load_domain.Find(observer_id);
    CHECK(found_observer);
    CHECK(found_observer.get() == &*load_doc->runtime_observer);

    CHECK(load_doc->base.is_valid());
    CHECK(load_doc->base->obj_id == doc_base_id);
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunModelPersistenceRuntimeRefTest();
  return 0;
}
