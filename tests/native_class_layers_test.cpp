#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"
#include "aether-objects/obj/obj_id.h"
#include "aether-objects/obj/obj_ptr.h"
#include "aether-objects/ptr/ptr.h"

#include "apptraverse/link.h"
#include "apptraverse/node.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_network_graph.h"
#include "apptraverse/shared_node.h"
#include "shared_node_demo_model.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

// 1. Correct native class-layer dispatch pattern
class LayerBase : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::LayerBase", LayerBase, ae::Obj, 0)
 protected:
  LayerBase() = default;

 public:
  explicit LayerBase(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base_value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, base_value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, base_value);
  }

  std::uint32_t base_value{0};
};

class LayerDerived : public LayerBase {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::LayerDerived", LayerDerived,
                           LayerBase, 0)
 protected:
  LayerDerived() = default;

 public:
  explicit LayerDerived(ae::ObjProp prop) : LayerBase{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(derived_value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, derived_value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, derived_value);
  }

  std::uint32_t derived_value{0};
};

APPTRAVERSE_REGISTER(LayerBase);
APPTRAVERSE_REGISTER(LayerDerived);

// 2. Broken pattern: direct ancestor Save/Load call instead of dnv(base_)
class BrokenBase : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::BrokenBase", BrokenBase, ae::Obj, 0)
 protected:
  BrokenBase() = default;

 public:
  explicit BrokenBase(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base_value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_value);
  }

  std::uint32_t base_value{0};
};

class BrokenDerived : public BrokenBase {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::BrokenDerived", BrokenDerived,
                           BrokenBase, 0)
 protected:
  BrokenDerived() = default;

 public:
  explicit BrokenDerived(ae::ObjProp prop) : BrokenBase{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(derived_value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    BrokenBase::Load(ae::Version<0>{}, dnv);
    dnv(derived_value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    BrokenBase::Save(ae::Version<0>{}, dnv);
    dnv(derived_value);
  }

  std::uint32_t derived_value{0};
};

APPTRAVERSE_REGISTER(BrokenBase);
APPTRAVERSE_REGISTER(BrokenDerived);

void TestNativeLayerDispatch() {
  ae::RamDomainStorage storage;
  ae::ObjId target_id{42001};

  // 1. Create and save LayerDerived
  {
    ae::Domain domain{storage};
    auto derived_ptr = ae::MakePtr<LayerDerived>();
    domain.AddObject(target_id, derived_ptr);
    derived_ptr->domain = &domain;
    derived_ptr->obj_id = target_id;
    derived_ptr->base_value = 11;
    derived_ptr->derived_value = 22;

    ae::DomainGraph graph{&domain};
    graph.SaveRoot(derived_ptr, target_id);
  }

  // 2. Query storage directly using native storage queries and readers
  // A. Check that LayerBase layer exists and contains base_value == 11
  {
    auto base_load = storage.Load(
        ae::DomainQuery{target_id, LayerBase::kClassId, 0});
    CHECK(base_load.result == ae::DomainLoadResult::kLoaded);
    CHECK(base_load.reader != nullptr);

    ae::seri::BinaryArchive base_arch{ae::DomainBuffer{
        .id = target_id,
        .domain_graph = nullptr,
        .writer = nullptr,
        .reader = base_load.reader.get(),
    }};
    std::uint32_t loaded_base_val = 0;
    auto res = base_arch.Load(loaded_base_val);
    CHECK(res);
    CHECK(loaded_base_val == 11);
  }

  // B. Check that LayerDerived layer exists and contains derived_value == 22
  {
    auto derived_load = storage.Load(
        ae::DomainQuery{target_id, LayerDerived::kClassId, 0});
    CHECK(derived_load.result == ae::DomainLoadResult::kLoaded);
    CHECK(derived_load.reader != nullptr);

    ae::seri::BinaryArchive derived_arch{ae::DomainBuffer{
        .id = target_id,
        .domain_graph = nullptr,
        .writer = nullptr,
        .reader = derived_load.reader.get(),
    }};
    std::uint32_t loaded_derived_val = 0;
    auto res = derived_arch.Load(loaded_derived_val);
    CHECK(res);
    CHECK(loaded_derived_val == 22);
  }

  // C. Verify Enumerate returns both class layers for target_id
  {
    auto classes = storage.Enumerate(target_id);
    bool has_base = false;
    bool has_derived = false;
    for (auto c : classes) {
      if (c == LayerBase::kClassId) has_base = true;
      if (c == LayerDerived::kClassId) has_derived = true;
    }
    CHECK(has_base);
    CHECK(has_derived);
  }

  // 3. Load into a brand new Domain and verify both values restored
  {
    ae::Domain domain2{storage};
    ae::DomainGraph graph2{&domain2};
    auto loaded_root = graph2.LoadRoot(target_id);
    CHECK(loaded_root);
    CHECK(loaded_root->GetClassId() == LayerDerived::kClassId);

    auto derived2 = ae::Ptr<LayerDerived>{loaded_root};
    CHECK(derived2->base_value == 11);
    CHECK(derived2->derived_value == 22);
  }
}

void TestBrokenDirectCallPattern() {
  ae::RamDomainStorage storage;
  ae::ObjId target_id{42002};

  // Create and save BrokenDerived using direct ancestor call
  {
    ae::Domain domain{storage};
    auto broken_ptr = ae::MakePtr<BrokenDerived>();
    domain.AddObject(target_id, broken_ptr);
    broken_ptr->domain = &domain;
    broken_ptr->obj_id = target_id;
    broken_ptr->base_value = 11;
    broken_ptr->derived_value = 22;

    ae::DomainGraph graph{&domain};
    graph.SaveRoot(broken_ptr, target_id);
  }

  // Storage check: BrokenDerived class layer exists
  auto derived_load = storage.Load(
      ae::DomainQuery{target_id, BrokenDerived::kClassId, 0});
  CHECK(derived_load.result == ae::DomainLoadResult::kLoaded);

  // BUT BrokenBase layer DOES NOT exist! It is kEmpty because Base::Save was
  // called directly and flattened its bytes into BrokenDerived's buffer!
  auto base_load = storage.Load(
      ae::DomainQuery{target_id, BrokenBase::kClassId, 0});
  CHECK(base_load.result == ae::DomainLoadResult::kEmpty);

  // Enumerate only contains BrokenDerived, not BrokenBase
  auto classes = storage.Enumerate(target_id);
  bool has_base = false;
  bool has_derived = false;
  for (auto c : classes) {
    if (c == BrokenBase::kClassId) has_base = true;
    if (c == BrokenDerived::kClassId) has_derived = true;
  }
  CHECK(!has_base);
  CHECK(has_derived);
}

void TestRealModelLayers_SharedValueNode() {
  using example::shared_node::SharedValueNode;

  ae::RamDomainStorage storage;
  ae::ObjId target_id{50001};

  {
    ae::Domain domain{storage};
    auto node = ae::MakePtr<SharedValueNode>();
    domain.AddObject(target_id, node);
    node->domain = &domain;
    node->obj_id = target_id;
    node->value = 12345;

    ae::DomainGraph graph{&domain};
    graph.SaveRoot(node, target_id);
  }

  // 1. Storage must contain separate layers for Node, SharedNode, SharedValueNode
  auto node_load = storage.Load(
      ae::DomainQuery{target_id, Node::kClassId, 0});
  CHECK(node_load.result == ae::DomainLoadResult::kLoaded);

  auto shared_node_load = storage.Load(
      ae::DomainQuery{target_id, SharedNode::kClassId, 0});
  CHECK(shared_node_load.result == ae::DomainLoadResult::kLoaded);

  auto svn_load = storage.Load(
      ae::DomainQuery{target_id, SharedValueNode::kClassId, 0});
  CHECK(svn_load.result == ae::DomainLoadResult::kLoaded);

  // 2. Verify no duplicate copies: SharedValueNode layer has only its own fields (value)
  {
    ae::seri::BinaryArchive arch{ae::DomainBuffer{
        .id = target_id,
        .domain_graph = nullptr,
        .writer = nullptr,
        .reader = svn_load.reader.get(),
    }};
    std::int32_t val = 0;
    auto res = arch.Load(val);
    CHECK(res);
    CHECK(val == 12345);
  }

  // 3. Load into brand new Domain
  {
    ae::Domain domain2{storage};
    ae::DomainGraph graph2{&domain2};
    auto root = graph2.LoadRoot(target_id);
    CHECK(root);
    CHECK(root->GetClassId() == SharedValueNode::kClassId);

    auto loaded_svn = ae::Ptr<SharedValueNode>{root};
    CHECK(loaded_svn->value == 12345);
  }
}

void TestRealModelLayers_MemoryLink() {
  ae::RamDomainStorage storage;
  ae::ObjId target_id{50002};

  {
    ae::Domain domain{storage};
    auto link = ae::MakePtr<MemoryLink>();
    domain.AddObject(target_id, link);
    link->domain = &domain;
    link->obj_id = target_id;
    link->endpoint_uid = "ep_alpha";
    link->heartbeat_interval_ms = 5000;

    ae::DomainGraph graph{&domain};
    graph.SaveRoot(link, target_id);
  }

  // 1. Storage must contain separate layers for Node, Link, MemoryLink
  auto node_load = storage.Load(
      ae::DomainQuery{target_id, Node::kClassId, 0});
  CHECK(node_load.result == ae::DomainLoadResult::kLoaded);

  auto link_load = storage.Load(
      ae::DomainQuery{target_id, Link::kClassId, 0});
  CHECK(link_load.result == ae::DomainLoadResult::kLoaded);

  auto mem_load = storage.Load(
      ae::DomainQuery{target_id, MemoryLink::kClassId, 0});
  CHECK(mem_load.result == ae::DomainLoadResult::kLoaded);

  // 2. Verify no duplicate copies: MemoryLink layer contains endpoint_uid and heartbeat_interval_ms
  {
    ae::seri::BinaryArchive arch{ae::DomainBuffer{
        .id = target_id,
        .domain_graph = nullptr,
        .writer = nullptr,
        .reader = mem_load.reader.get(),
    }};
    std::string ep;
    std::uint32_t hb = 0;
    auto res1 = arch.Load(ep);
    CHECK(res1);
    CHECK(ep == "ep_alpha");
    auto res2 = arch.Load(hb);
    CHECK(res2);
    CHECK(hb == 5000);
  }

  // 3. Load into brand new Domain
  {
    ae::Domain domain2{storage};
    ae::DomainGraph graph2{&domain2};
    auto root = graph2.LoadRoot(target_id);
    CHECK(root);
    CHECK(root->GetClassId() == MemoryLink::kClassId);

    auto loaded_link = ae::Ptr<MemoryLink>{root};
    CHECK(loaded_link->endpoint_uid == "ep_alpha");
    CHECK(loaded_link->heartbeat_interval_ms == 5000);
  }
}

void TestRealModelLayers_LinkSyncState() {
  ae::RamDomainStorage storage;
  ae::ObjId target_id{50003};

  {
    ae::Domain domain{storage};
    auto state = ae::MakePtr<LinkSyncState>();
    domain.AddObject(target_id, state);
    state->domain = &domain;
    state->obj_id = target_id;
    state->share_id = ae::ObjId{999};
    state->initial_sync_phase =
        static_cast<std::uint8_t>(InitialSyncPhase::Complete);

    ae::DomainGraph graph{&domain};
    graph.SaveRoot(state, target_id);
  }

  // 1. Storage must contain separate layers for Node, LinkSyncState
  auto node_load = storage.Load(
      ae::DomainQuery{target_id, Node::kClassId, 0});
  CHECK(node_load.result == ae::DomainLoadResult::kLoaded);

  auto lss_load = storage.Load(
      ae::DomainQuery{target_id, LinkSyncState::kClassId, 0});
  CHECK(lss_load.result == ae::DomainLoadResult::kLoaded);

  // 2. Verify no duplicate copies: LinkSyncState layer has share_id first
  {
    ae::seri::BinaryArchive arch{ae::DomainBuffer{
        .id = target_id,
        .domain_graph = nullptr,
        .writer = nullptr,
        .reader = lss_load.reader.get(),
    }};
    ae::ObjId sid;
    auto res = arch.Load(sid);
    CHECK(res);
    CHECK(sid == ae::ObjId{999});
  }

  // 3. Load into brand new Domain
  {
    ae::Domain domain2{storage};
    ae::DomainGraph graph2{&domain2};
    auto root = graph2.LoadRoot(target_id);
    CHECK(root);
    CHECK(root->GetClassId() == LinkSyncState::kClassId);

    auto loaded_state = ae::Ptr<LinkSyncState>{root};
    CHECK(loaded_state->share_id == ae::ObjId{999});
    CHECK(loaded_state->GetInitialSyncPhase() == InitialSyncPhase::Complete);
  }
}

void TestBaseSnapshotAndMidJournalReplay() {
  using example::shared_node::SharedValueNode;
  using example::shared_node::SetValueEvent;

  ae::RamDomainStorage storage;
  ae::ObjId target_id{60001};

  ae::Domain domain{storage};
  auto node = SharedValueNode::ptr::Create(
      ae::CreateWith{domain}.with_id(target_id));
  node->value = 100;

  // Initialize runtime node: captures base state
  InitializeRuntimeNode(*node);
  CHECK(node->base.is_valid());
  CHECK(node->base.is_loaded());
  auto base_svn = SharedValueNode::ptr{node->base};
  CHECK(base_svn->value == 100);

  // Apply multiple events in mid-journal replay test
  for (int i = 1; i <= 5; ++i) {
    auto ev = SetValueEvent::ptr::Create(ae::CreateWith{domain});
    ev->value = 100 + i * 10;
    node->Commit(ev);
  }

  CHECK(node->value == 150);
  CHECK(node->journal.size() == 5);

  // Base snapshot remains unchanged at initial value 100
  CHECK(base_svn->value == 100);

  // Compact journal partially
  node->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 2});
  node->CompactJournal(1000);
  CHECK(node->journal.size() == 2);
  // After compaction, base snapshot updated to value 130
  CHECK(base_svn->value == 130);
  CHECK(node->value == 150);

  // Replay from base restores exact state
  bool ok = node->TryReplayFromBase();
  CHECK(ok);
  CHECK(node->value == 150);
}

void TestLinkSyncStatePersistenceAcrossBusinessReplay() {
  using example::shared_node::SharedValueNode;
  using example::shared_node::SetValueEvent;

  ae::RamDomainStorage storage;
  ae::ObjId target_id{60002};
  ae::ObjId link_id{60003};

  ae::Domain domain{storage};
  auto node = SharedValueNode::ptr::Create(
      ae::CreateWith{domain}.with_id(target_id));
  node->value = 50;

  auto link = MemoryLink::ptr::Create(
      ae::CreateWith{domain}.with_id(link_id));
  link->endpoint_uid = "ep_replay";

  InitializeRuntimeNode(*node);

  // Add share creates a LocalPtr<LinkSyncState> on node
  node->AddShare(link, ShareAccess::ReadWrite);
  CHECK(!node->link_sync_states.empty());
  CHECK(node->link_sync_states[0].is_valid());
  node->SetInitialSyncPhase(link, InitialSyncPhase::Complete);
  CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);

  // Add business events
  auto ev = SetValueEvent::ptr::Create(ae::CreateWith{domain});
  ev->value = 99;
  node->Commit(ev);

  CHECK(node->value == 99);

  // Business replay from base
  bool ok = node->TryReplayFromBase();
  CHECK(ok);
  CHECK(node->value == 99);

  // Verify LinkSyncState is preserved and phase is still Complete
  CHECK(!node->link_sync_states.empty());
  CHECK(node->GetInitialSyncPhase(link) == InitialSyncPhase::Complete);
}

void TestUnsupportedSchemaRejected() {
  using example::shared_node::SharedValueNode;

  auto check_rejected = []<typename T>(ae::ObjId target_id,
                                      std::uint32_t layer_class_id) {
    ae::RamDomainStorage storage;
    {
      ae::Domain domain{storage};
      auto source = ae::MakePtr<T>();
      domain.AddObject(target_id, source);
      source->domain = &domain;
      source->obj_id = target_id;
      ae::DomainGraph graph{&domain};
      CHECK(graph.SaveRoot(source, target_id));
    }

    // Prove that this complete graph loads before changing only its schema key.
    auto const valid_state = storage.state;
    {
      ae::Domain domain{storage};
      ae::DomainGraph graph{&domain};
      auto root = graph.LoadRoot(target_id);
      CHECK(root);
      CHECK(root->GetClassId() == T::kClassId);
      CHECK(root->obj_id == target_id);
    }
    CHECK(storage.state == valid_state);

    auto& versions = storage.state.at(target_id)->at(layer_class_id);
    CHECK(versions.size() == 1);
    auto layer = versions.extract(0);
    CHECK(!layer.empty());
    layer.key() = 1;
    versions.insert(std::move(layer));
    auto const incompatible_state = storage.state;

    {
      ae::Domain domain{storage};
      ae::DomainGraph graph{&domain};
      ae::Ptr<ae::Obj> root;
      auto result = graph.LoadRoot(target_id, root);
      CHECK(result.IsErr());
      CHECK(!root);
      CHECK(!domain.Find(target_id));
    }
    CHECK(storage.state == incompatible_state);
  };

  check_rejected.operator()<SharedValueNode>(ae::ObjId{80001}, Node::kClassId);
  check_rejected.operator()<SharedValueNode>(ae::ObjId{80002},
                                            SharedValueNode::kClassId);
  check_rejected.operator()<MemoryLink>(ae::ObjId{80003}, MemoryLink::kClassId);
  check_rejected.operator()<LinkSyncState>(ae::ObjId{80004},
                                          LinkSyncState::kClassId);
}


}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::example::shared_node::EnsureSharedNodeDemoRegistration();

  apptraverse::test::TestNativeLayerDispatch();
  apptraverse::test::TestBrokenDirectCallPattern();
  apptraverse::test::TestRealModelLayers_SharedValueNode();
  apptraverse::test::TestRealModelLayers_MemoryLink();
  apptraverse::test::TestRealModelLayers_LinkSyncState();
  apptraverse::test::TestBaseSnapshotAndMidJournalReplay();
  apptraverse::test::TestLinkSyncStatePersistenceAcrossBusinessReplay();
  apptraverse::test::TestUnsupportedSchemaRejected();

  std::cout << "apptraverse_native_class_layers_test: PASS\n";
  return 0;
}
