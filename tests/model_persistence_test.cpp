#include <cstdlib>
#include <iostream>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/model_persistence.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_lifecycle.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

class DocNode;
class DocChangedEvent;

class DocNode : public NodeFor<DocNode> {
  APPTRAVERSE_OBJECT(DocNode, Node, 0)

 protected:
  DocNode() = default;

 public:
  explicit DocNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, label);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, label);
  }

  std::string label;

  void Apply(DocChangedEvent const& event);
};

class DocChangedEvent : public EventFor<DocNode, DocChangedEvent> {
  APPTRAVERSE_OBJECT(DocChangedEvent, Event, 0)

 protected:
  DocChangedEvent() = default;

 public:
  explicit DocChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

APPTRAVERSE_REGISTER(DocNode);
APPTRAVERSE_REGISTER(DocChangedEvent);

void DocNode::Apply(DocChangedEvent const& event) {
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

bool StorageContainsClass(ae::RamDomainStorage const& storage,
                          std::uint32_t class_id) {
  for (auto const& [obj_id, class_map_opt] : storage.state) {
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

void RunModelPersistenceTest() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto doc = DocNode::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*doc);
  auto runtime = NetworkState::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*runtime, *doc);
  doc->label = "before";

  static_cast<void>(CommitNetworkAvailable(*runtime, 1));

  std::uint64_t const gen_before = doc->Generation();
  std::size_t const journal_before = doc->journal.size();

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*doc, snapshot);
  CHECK(doc->Generation() == gen_before);
  CHECK(doc->journal.size() == journal_before);
  CHECK(doc->label == "before");

  ae::RamDomainStorage scratch;
  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_doc =
      DocNode::ptr::Create(ae::CreateWith{load_domain}.with_id(doc->obj_id));
  InitializeRuntimeNode(*load_doc);
  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_doc);
  CHECK(load_doc->label == "before");
  CHECK(!StorageContainsClass(load_storage, NetworkState::kClassId));

  auto fresh_runtime = NetworkState::ptr::Create(ae::CreateWith{load_domain});
  InitializeRuntimeNode(*fresh_runtime, *load_doc);
  static_cast<void>(CommitNetworkInitializing(*fresh_runtime, 2));
  CHECK(fresh_runtime->GetAvailability() == NetworkAvailability::kInitializing);

  auto event = DocChangedEvent::ptr::Create(ae::CreateWith{load_domain});
  event->label = "after";
  load_doc->Commit(event);
  CHECK(load_doc->label == "after");
}

}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunModelPersistenceTest();
  return 0;
}
