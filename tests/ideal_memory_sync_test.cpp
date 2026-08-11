#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/ideal_memory_sync.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_state.h"
#include "apptraverse/shared_graph.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class SyncLeaf;
class SyncLocal;
class SyncRoot;
class SyncOwned;
class SyncBumpEvent;

class SyncOwned : public ae::Obj {
  APPTRAVERSE_OBJECT(SyncOwned, ae::Obj, 0)

 protected:
  SyncOwned() = default;

 public:
  explicit SyncOwned(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class SyncLeaf : public NodeFor<SyncLeaf> {
  APPTRAVERSE_OBJECT(SyncLeaf, Node, 0)

 protected:
  SyncLeaf() = default;

 public:
  explicit SyncLeaf(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class SyncLocal : public NodeFor<SyncLocal> {
  APPTRAVERSE_OBJECT(SyncLocal, Node, 0)

 protected:
  SyncLocal() = default;

 public:
  explicit SyncLocal(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class SyncRoot : public NodeFor<SyncRoot> {
  APPTRAVERSE_OBJECT(SyncRoot, Node, 0)

 protected:
  SyncRoot() = default;

 public:
  explicit SyncRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(owned), AE_MMBR(leaf),
                    AE_MMBR(local_peer), AE_MMBR(counter))

  std::string label;
  ae::ObjPtr<SyncOwned> owned;
  SharedPtr<SyncLeaf> leaf;
  LocalPtr<SyncLocal> local_peer;
  std::int32_t counter{0};

  void Apply(SyncBumpEvent const& event);
};

class SyncBumpEvent : public EventFor<SyncRoot, SyncBumpEvent> {
  APPTRAVERSE_OBJECT(SyncBumpEvent, Event, 0)

 protected:
  SyncBumpEvent() = default;

 public:
  explicit SyncBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

void SyncRoot::Apply(SyncBumpEvent const& event) { counter += event.delta; }

APPTRAVERSE_REGISTER(SyncOwned);
APPTRAVERSE_REGISTER(SyncLeaf);
APPTRAVERSE_REGISTER(SyncLocal);
APPTRAVERSE_REGISTER(SyncRoot);
APPTRAVERSE_REGISTER(SyncBumpEvent);

bool ContainsId(std::vector<Node::ptr> const& nodes, ae::ObjId id) {
  for (auto const& node : nodes) {
    if (node.id() == id) {
      return true;
    }
  }
  return false;
}

void TestMissingRootOneWaySync() {
  ae::RamDomainStorage source_storage;
  ae::Domain source_domain{ae::Now(), source_storage};

  auto root_base =
      SyncRoot::ptr::Create(ae::CreateWith{source_domain}.with_id(1));
  auto root = SyncRoot::ptr::Create(ae::CreateWith{source_domain}.with_id(2));
  auto leaf_base =
      SyncLeaf::ptr::Create(ae::CreateWith{source_domain}.with_id(3));
  auto leaf = SyncLeaf::ptr::Create(ae::CreateWith{source_domain}.with_id(4));
  auto local =
      SyncLocal::ptr::Create(ae::CreateWith{source_domain}.with_id(5));
  auto owned =
      SyncOwned::ptr::Create(ae::CreateWith{source_domain}.with_id(6));

  root->label = "root";
  leaf->label = "leaf";
  local->label = "local";
  owned->label = "owned";
  root->owned = owned;
  root->leaf = leaf;
  root->local_peer = local;
  root->base = root_base;
  leaf->base = leaf_base;
  root->CaptureBaseState();
  leaf->CaptureBaseState();

  auto bump =
      SyncBumpEvent::ptr::Create(ae::CreateWith{source_domain}.with_id(7));
  bump->delta = 9;
  root->Commit(bump);
  CHECK(root->counter == 9);
  CHECK(root->journal.size() == 1);

  ae::RamDomainStorage target_storage;
  ae::Domain target_domain{ae::Now(), target_storage};
  MemoryReplica source{source_domain, source_storage, root.id()};
  MemoryReplica target{target_domain, target_storage, root.id()};

  auto const result = SynchronizeSharedGraphOneWay(source, target);
  CHECK(result.nodes_imported == 2);  // root + leaf
  CHECK(result.events_imported == 0);  // journal arrived with full root state

  auto loaded_root =
      SyncRoot::ptr::Declare(ae::CreateWith{target_domain}.with_id(2));
  loaded_root.Load();
  CHECK(loaded_root.is_loaded());
  CHECK(loaded_root->GetClassId() == SyncRoot::kClassId);
  CHECK(loaded_root->label == "root");
  CHECK(loaded_root->counter == 9);
  CHECK(loaded_root->journal.size() == 1);
  loaded_root->owned.Load();
  CHECK(loaded_root->owned->label == "owned");
  CHECK(loaded_root->leaf.id().id() == 4);
  CHECK(loaded_root->leaf.domain() == &target_domain);
  loaded_root->leaf.Load();
  CHECK(loaded_root->leaf->label == "leaf");
  CHECK(loaded_root->leaf->GetClassId() == SyncLeaf::kClassId);
  CHECK(!loaded_root->local_peer.is_valid());

  auto discovered = DiscoverSharedGraph(loaded_root);
  CHECK(discovered.size() == 2);
  CHECK(ContainsId(discovered, root.id()));
  CHECK(ContainsId(discovered, leaf.id()));
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestMissingRootOneWaySync();
  return 0;
}
