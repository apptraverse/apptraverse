#include <cstdlib>
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
#include "apptraverse/presenter.h"
#include "apptraverse/runtime_lifecycle.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

class PersistentChild;
class PersistentShell;
class PersistentShellChangedEvent;
class TestPresenter;

class PersistentChild : public NodeFor<PersistentChild> {
  APPTRAVERSE_OBJECT(PersistentChild, Node, 0)

 protected:
  PersistentChild() = default;

 public:
  explicit PersistentChild(ae::ObjProp prop) : NodeFor{prop} {}

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
};

class TestPresenter : public Presenter {
  APPTRAVERSE_OBJECT(TestPresenter, Presenter, 0)

 protected:
  TestPresenter() = default;

 public:
  explicit TestPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT()

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }
};

class PersistentShell : public NodeFor<PersistentShell> {
  APPTRAVERSE_OBJECT(PersistentShell, Node, 0)

 protected:
  PersistentShell() = default;

 public:
  explicit PersistentShell(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presenter), AE_MMBR(runtime_observer),
                    AE_MMBR(persistent_child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, presenter, runtime_observer, persistent_child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, presenter, runtime_observer, persistent_child);
  }

  ae::ObjPtr<TestPresenter> presenter;
  NetworkState::ptr runtime_observer;
  PersistentChild::ptr persistent_child;

  void Apply(PersistentShellChangedEvent const& event);
};

class PersistentShellChangedEvent
    : public EventFor<PersistentShell, PersistentShellChangedEvent> {
  APPTRAVERSE_OBJECT(PersistentShellChangedEvent, Event, 0)

 protected:
  PersistentShellChangedEvent() = default;

 public:
  explicit PersistentShellChangedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class PersistentApplication : public ae::Obj {
  APPTRAVERSE_OBJECT(PersistentApplication, ae::Obj, 0)

 protected:
  PersistentApplication() = default;

 public:
  explicit PersistentApplication(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(shell))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, shell);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, shell);
  }

  PersistentShell::ptr shell;
};

APPTRAVERSE_REGISTER(PersistentChild);
APPTRAVERSE_REGISTER(TestPresenter);
APPTRAVERSE_REGISTER(PersistentShell);
APPTRAVERSE_REGISTER(PersistentShellChangedEvent);
APPTRAVERSE_REGISTER(PersistentApplication);

#include "apptraverse/object_persistence_macros.h"
APPTRAVERSE_REGISTER_PERSISTED_OBJPTR_WALKER(PersistentShell);

void PersistentShell::Apply(PersistentShellChangedEvent const& event) {
  if (persistent_child) {
    persistent_child->label = event.label;
  }
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

void CheckBaseCapturedExcludedRefs(PersistentShell& shell,
                                   TestPresenter& presenter,
                                   NetworkState& runtime) {
  CHECK(shell.base.is_valid());
  CHECK(shell.base.is_loaded());
  auto& base_shell = static_cast<PersistentShell&>(*shell.base);
  CHECK(base_shell.presenter.is_valid());
  CHECK(base_shell.presenter.id() == presenter.obj_id);
  CHECK(base_shell.runtime_observer.is_valid());
  CHECK(base_shell.runtime_observer.id() == runtime.obj_id);
  CHECK(base_shell.persistent_child.is_valid());
  CHECK(base_shell.persistent_child.id() == shell.persistent_child.id());
}

void RunOneRoundtrip() {
  ae::ObjId const kAppId{0xBA5E0001u};
  ae::ObjId const kShellId{0xBA5E0002u};
  ae::ObjId const kChildId{0xBA5E0003u};

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto app = PersistentApplication::ptr::Create(
      ae::CreateWith{domain}.with_id(kAppId));
  auto shell = PersistentShell::ptr::Create(ae::CreateWith{domain}.with_id(kShellId));
  auto child = PersistentChild::ptr::Create(ae::CreateWith{domain}.with_id(kChildId));
  child->label = "child";
  auto presenter = TestPresenter::ptr::Create(ae::CreateWith{domain});
  auto runtime = NetworkState::ptr::Create(ae::CreateWith{domain});

  shell->presenter = presenter;
  shell->runtime_observer = runtime;
  shell->persistent_child = child;
  app->shell = shell;

  InitializeRuntimeNode(*shell);
  CheckBaseCapturedExcludedRefs(*shell, *presenter, *runtime);

  auto event_a = PersistentShellChangedEvent::ptr::Create(ae::CreateWith{domain});
  event_a->label = "event_a";
  shell->Commit(event_a);
  auto event_b = PersistentShellChangedEvent::ptr::Create(ae::CreateWith{domain});
  event_b->label = "event_b";
  shell->Commit(event_b);

  ae::ObjId::Type const base_id = shell->base.id().id();
  std::size_t const journal_before = shell->journal.size();
  std::uint64_t const gen_before = shell->Generation();
  ae::ObjId const presenter_id = presenter->obj_id;
  ae::ObjId const runtime_id = runtime->obj_id;

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*app, snapshot);

  CHECK(shell->presenter.is_valid());
  CHECK(shell->presenter.id() == presenter_id);
  CHECK(shell->runtime_observer.is_valid());
  CHECK(shell->runtime_observer.id() == runtime_id);
  CHECK(shell->persistent_child.is_valid());
  CHECK(shell->journal.size() == journal_before);
  CHECK(shell->Generation() == gen_before);
  CheckBaseCapturedExcludedRefs(*shell, *presenter, *runtime);

  auto const snapshot_ids = SnapshotObjectIds(snapshot.bytes);
  CHECK(snapshot_ids.count(kAppId.id()) != 0);
  CHECK(snapshot_ids.count(kShellId.id()) != 0);
  CHECK(snapshot_ids.count(base_id) != 0);
  CHECK(snapshot_ids.count(kChildId.id()) != 0);
  CHECK(snapshot_ids.count(presenter_id.id()) == 0);
  CHECK(snapshot_ids.count(runtime_id.id()) == 0);

  ae::RamDomainStorage load_storage;
  ae::Domain load_domain{load_storage};
  auto load_app = PersistentApplication::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kAppId));
  auto load_shell = PersistentShell::ptr::Create(
      ae::CreateWith{load_domain}.with_id(kShellId));
  InitializeRuntimeNode(*load_shell);
  load_app->shell = load_shell;

  ByteSource in;
  in.data = snapshot.bytes.data();
  in.size = snapshot.bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, load_domain, load_storage, *load_app);

  CHECK(load_app->shell.is_valid());
  CHECK(!load_app->shell->presenter.is_valid());
  CHECK(!load_app->shell->runtime_observer.is_valid());
  CHECK(load_app->shell->persistent_child.is_valid());
  CHECK(load_app->shell->persistent_child->label == "event_b");
  CHECK(load_app->shell->base.is_valid());
  auto& load_base_shell = static_cast<PersistentShell&>(*load_app->shell->base);
  CHECK(!load_base_shell.presenter.is_valid());
  CHECK(!load_base_shell.runtime_observer.is_valid());
  CHECK(!StorageContainsClass(load_storage, TestPresenter::kClassId));
  CHECK(!StorageContainsClass(load_storage, NetworkState::kClassId));

  auto event_c = PersistentShellChangedEvent::ptr::Create(ae::CreateWith{load_domain});
  event_c->label = "event_c";
  load_app->shell->Commit(event_c);
  load_app->shell->CompactJournal(SystemUtcMicros());

  ByteSink snapshot2;
  SerializePersistentModelSnapshot(*load_app, snapshot2);
  ae::RamDomainStorage load_storage2;
  ae::Domain load_domain2{load_storage2};
  auto load_app2 = PersistentApplication::ptr::Create(
      ae::CreateWith{load_domain2}.with_id(kAppId));
  auto load_shell2 = PersistentShell::ptr::Create(
      ae::CreateWith{load_domain2}.with_id(kShellId));
  InitializeRuntimeNode(*load_shell2);
  load_app2->shell = load_shell2;
  ByteSource in2;
  in2.data = snapshot2.bytes.data();
  in2.size = snapshot2.bytes.size();
  in2.pos = 0;
  LoadPersistentModelSnapshot(in2, load_domain2, load_storage2, *load_app2);
  CHECK(load_app2->shell->persistent_child->label == "event_c");
}

}  // namespace

void RunModelPersistenceNodeBaseExcludedRefTest() {
  for (int i = 0; i < 20; ++i) {
    RunOneRoundtrip();
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::RunModelPersistenceNodeBaseExcludedRefTest();
  std::cout << "apptraverse_model_persistence_node_base_excluded_ref_test OK\n";
  return 0;
}
