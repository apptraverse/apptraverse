#include <cstdlib>
#include <cstring>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/graph_walk.h"
#include "apptraverse/model_persistence.h"
#include "apptraverse/object_macros.h"
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

}  // namespace

void RunNonmutatingSaveTest() {
  ae::ObjId const kDocId{0x0A11AD02u};
  ae::RamDomainStorage live_storage;
  ae::Domain live_domain{live_storage};
  auto doc = PersistentDoc::ptr::Create(
      ae::CreateWith{live_domain}.with_id(kDocId));
  InitializeRuntimeNode(*doc);
  auto runtime = NetworkState::ptr::Create(ae::CreateWith{live_domain});
  InitializeRuntimeNode(*runtime, *doc);
  doc->runtime_observer = runtime;
  doc->label = "before_save";
  static_cast<void>(CommitNetworkAvailable(*runtime, 1));
  std::size_t const journal_before = doc->journal.size();
  std::uint64_t const generation_before = doc->Generation();

  ae::RamDomainStorage durable_storage;
  WritePersistentModelSnapshotToStorage(*doc, durable_storage);

  CHECK(doc->label == "before_save");
  CHECK(doc->runtime_observer.is_valid());
  CHECK(doc->journal.size() == journal_before);
  CHECK(doc->Generation() == generation_before);
  CHECK(!durable_storage.state.empty());

  auto event = PersistentDocChangedEvent::ptr::Create(ae::CreateWith{live_domain});
  event->label = "after_save";
  doc->Commit(event);
  CHECK(doc->label == "after_save");

  ae::RamDomainStorage reload_storage;
  ae::Domain reload_domain{reload_storage};
  auto reloaded = PersistentDoc::ptr::Create(
      ae::CreateWith{reload_domain}.with_id(kDocId));
  ByteSink snap;
  SerializePersistentModelSnapshot(*doc, snap);
  ByteSource in{snap.bytes.data(), snap.bytes.size()};
  LoadPersistentModelSnapshot(in, reload_domain, reload_storage, *reloaded);
  CHECK(reloaded->label == "after_save");
  CHECK(!reloaded->runtime_observer.is_valid());
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunNonmutatingSaveTest();
  std::fprintf(stderr, "model_persistence_nonmutating_save_test OK\n");
  return 0;
}
