#include <cstdlib>
#include <iostream>
#include <set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {

class HistorizedBumpEvent;

class HistorizedValueNode : public NodeFor<HistorizedValueNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HistorizedValueNode",
                           HistorizedValueNode, Node, 0)

 protected:
  HistorizedValueNode() = default;

 public:
  explicit HistorizedValueNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::int32_t value{0};

  void Apply(HistorizedBumpEvent const& event);
};

class HistorizedBumpEvent
    : public EventFor<HistorizedValueNode, HistorizedBumpEvent> {
  APPTRAVERSE_OBJECT(HistorizedBumpEvent, Event, 0)

 protected:
  HistorizedBumpEvent() = default;

 public:
  explicit HistorizedBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  std::int32_t delta{0};
};

class HistorizedSupplyPayload : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HistorizedSupplyPayload",
                           HistorizedSupplyPayload, ae::Obj, 0)

 protected:
  HistorizedSupplyPayload() = default;

 public:
  explicit HistorizedSupplyPayload(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag);
  }

  std::uint32_t tag{0};
};

class HistorizedConstHostNode : public HistorizedValueNode {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HistorizedConstHostNode",
                           HistorizedConstHostNode, HistorizedValueNode, 0)

 protected:
  HistorizedConstHostNode() = default;

 public:
  explicit HistorizedConstHostNode(ae::ObjProp prop)
      : HistorizedValueNode{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(constant_child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, constant_child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, constant_child);
  }

  HistorizedSupplyPayload::ptr constant_child;
};

APPTRAVERSE_REGISTER(HistorizedValueNode);
APPTRAVERSE_REGISTER(HistorizedBumpEvent);
APPTRAVERSE_REGISTER(HistorizedSupplyPayload);
APPTRAVERSE_REGISTER(HistorizedConstHostNode);

void HistorizedValueNode::Apply(HistorizedBumpEvent const& event) {
  value += event.delta;
  NoteMaterializedChange();
}

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

void WriteBytes(ae::IDomainStorage& storage, ae::DomainQuery const& query,
                std::vector<std::uint8_t> const& bytes) {
  auto writer = storage.Store(query);
  CHECK(writer);
  CHECK(writer->Write(ae::seri::DataWriteTag{bytes.data(), bytes.size()}));
}

bool HasRamUserLayer(ae::RamDomainStorage const& user, ae::ObjId id,
                     std::uint32_t class_id, std::uint8_t version) {
  auto const obj_it = user.state.find(id);
  if (obj_it == user.state.end() || !obj_it->second.has_value()) {
    return false;
  }
  auto const class_it = obj_it->second->find(class_id);
  if (class_it == obj_it->second->end()) {
    return false;
  }
  return class_it->second.count(version) != 0;
}

void CommitBump(HistorizedValueNode& node, ae::Domain& domain,
                std::int32_t delta) {
  auto ev = HistorizedBumpEvent::ptr::Create(ae::CreateWith{domain});
  ev->delta = delta;
  node.Commit(std::move(ev));
}

void TestQueryLevelForceMaterialize() {
  ae::RamDomainStorage backing;
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, backing};
  std::set<ae::ObjId> supply{ae::ObjId{77001}};
  OverlayDomainStorage supply_overlay{user, backing, supply};

  ae::ObjId const dynamic_id{77002};
  ae::ObjId const const_id{77001};
  ae::DomainQuery const dyn_q{dynamic_id, 42, 0};
  ae::DomainQuery const const_q{const_id, 42, 0};

  WriteBytes(backing, dyn_q, {1, 2});
  WriteBytes(backing, const_q, {3, 4});

  ae::DomainQuery same_as_backing = dyn_q;
  WriteBytes(overlay, same_as_backing, {1, 2});
  CHECK(user.state.empty());

  ae::DomainQuery force_q = dyn_q;
  force_q.force_materialize = true;
  WriteBytes(overlay, force_q, {1, 2});
  CHECK(HasRamUserLayer(user, dynamic_id, 42, 0));

  ae::DomainQuery force_zero = dyn_q;
  force_zero.force_materialize = true;
  WriteBytes(overlay, force_zero, {});
  CHECK(HasRamUserLayer(user, dynamic_id, 42, 0));

  ae::DomainQuery force_const = const_q;
  force_const.force_materialize = true;
  WriteBytes(supply_overlay, force_const, {9, 9});
  CHECK(!HasRamUserLayer(user, const_id, 42, 0));
  CHECK(backing.state.at(dynamic_id) == backing.state.at(dynamic_id));
}

class ForceGraphPayload : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ForceGraphPayload",
                           ForceGraphPayload, ae::Obj, 0)

 protected:
  ForceGraphPayload() = default;

 public:
  explicit ForceGraphPayload(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value);
  }

  std::uint32_t value{0};
};

APPTRAVERSE_REGISTER(ForceGraphPayload);

void TestDomainGraphForcePropagation() {
  ae::RamDomainStorage backing;
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, backing};
  ae::ObjId const id{88001};

  {
    ae::Domain domain{backing};
    auto created = ForceGraphPayload::ptr::Create(
        ae::CreateWith{domain}.with_id(id));
    ae::Ptr<ForceGraphPayload> node = created.Load();
    node->value = 5;
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(node, id).IsOk());
  }

  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(id, loaded).IsOk());
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(loaded, id).IsOk());
    CHECK(user.state.empty());
  }

  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(id, loaded).IsOk());
    ae::DomainGraph save_graph{&domain};
    save_graph.force_materialize = true;
    CHECK(save_graph.SaveRoot(loaded, id).IsOk());
    CHECK(HasRamUserLayer(user, id, ForceGraphPayload::kClassId, 0));
  }
}

ae::ObjId BuildHistorizedBacking(ae::RamDomainStorage& backing,
                                 ae::ObjId const node_id,
                                 ae::ObjId const sibling_id) {
  ae::Domain domain{backing};
  HistorizedValueNode::ptr main_ref = HistorizedValueNode::ptr::Create(
      ae::CreateWith{domain}.with_id(node_id));
  ae::Ptr<HistorizedValueNode> main = main_ref.Load();
  InitializeRuntimeNode(*main);
  main->value = 100;
  main->CaptureBaseState();
  CommitBump(*main, domain, 10);

  HistorizedValueNode::ptr sibling_ref = HistorizedValueNode::ptr::Create(
      ae::CreateWith{domain}.with_id(sibling_id));
  ae::Ptr<HistorizedValueNode> sibling = sibling_ref.Load();
  InitializeRuntimeNode(*sibling);
  sibling->value = 7;

  ae::DomainGraph graph{&domain};
  CHECK(graph.SaveRoot(main, node_id).IsOk());
  CHECK(graph.SaveRoot(sibling, sibling_id).IsOk());
  return main->base.id();
}

void TestHistoricalBaseUserMaterialization() {
  ae::RamDomainStorage backing;
  ae::ObjId const node_id{0x09C10001u};
  ae::ObjId const sibling_id{0x09C10002u};
  ae::ObjId const base_id =
      BuildHistorizedBacking(backing, node_id, sibling_id);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, backing};
  bool graph_force_after_save = true;
  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(node_id, loaded).IsOk());
    ae::Ptr<HistorizedValueNode> main{loaded};
    CHECK(!main->journal.empty());
    CHECK(main->base.is_valid());
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(main, node_id).IsOk());
    graph_force_after_save = save_graph.force_materialize;
    CHECK(HasRamUserLayer(user, base_id, Node::kClassId, 0));
    CHECK(!HasRamUserLayer(user, sibling_id, HistorizedValueNode::kClassId, 0));
  }
  CHECK(!graph_force_after_save);
  CHECK(backing.state == backing_snapshot);
}

void TestHistoricalBaseSurvivesBackingSwap() {
  ae::RamDomainStorage backing_a;
  ae::ObjId const node_id{0x09C10011u};
  ae::ObjId const sibling_id{0x09C10012u};
  ae::ObjId const base_id =
      BuildHistorizedBacking(backing_a, node_id, sibling_id);

  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, backing_a};
  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(node_id, loaded).IsOk());
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(loaded, node_id).IsOk());
  }

  ae::RamDomainStorage backing_b = backing_a;
  {
    ae::Domain domain{backing_b};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> base_loaded;
    CHECK(load_graph.LoadRoot(base_id, base_loaded).IsOk());
    ae::Ptr<HistorizedValueNode> base_node{base_loaded};
    base_node->value = 999;
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(base_node, base_id).IsOk());
  }

  OverlayDomainStorage swapped_overlay{user, backing_b};
  {
    ae::Domain domain{swapped_overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(node_id, loaded).IsOk());
    ae::Ptr<HistorizedValueNode> main{loaded};
    main->ReplayFromBase();
    CHECK(main->value == 110);
  }
}

void TestMaterializedPublicationOmitsHistory() {
  ae::RamDomainStorage backing;
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, backing};
  ae::ObjId const id{89001};

  ae::ObjId base_id{};
  {
    ae::Domain domain{backing};
    HistorizedValueNode::ptr ref = HistorizedValueNode::ptr::Create(
        ae::CreateWith{domain}.with_id(id));
    ae::Ptr<HistorizedValueNode> node = ref.Load();
    InitializeRuntimeNode(*node);
    node->value = 3;
    node->CaptureBaseState();
    CommitBump(*node, domain, 4);
    base_id = node->base.id();
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(node, id).IsOk());
  }

  {
    ae::Domain overlay_domain{overlay};
    ae::DomainGraph load_graph{&overlay_domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(id, loaded).IsOk());
    ae::DomainGraph pub_graph{&overlay_domain};
    pub_graph.serialization_scope =
        ae::GraphSerializationScope::MaterializedPublication;
    CHECK(pub_graph.SaveRoot(loaded, id).IsOk());
    CHECK(!HasRamUserLayer(user, base_id, Node::kClassId, 0));
  }
}

void TestSupplyConstantWithHistoricalBaseSave() {
  ae::RamDomainStorage backing;
  ae::ObjId const root_id{0x09C10021u};
  ae::ObjId const const_id{0x09C10022u};
  std::set<ae::ObjId> supply{const_id};

  {
    ae::Domain domain{backing};
    HistorizedConstHostNode::ptr root_ref =
        HistorizedConstHostNode::ptr::Create(
            ae::CreateWith{domain}.with_id(root_id));
    ae::Ptr<HistorizedConstHostNode> root = root_ref.Load();
    InitializeRuntimeNode(*root);
    HistorizedSupplyPayload::ptr child_ref =
        HistorizedSupplyPayload::ptr::Create(
            ae::CreateWith{domain}.with_id(const_id));
    ae::Ptr<HistorizedSupplyPayload> child = child_ref.Load();
    child->tag = 55;
    root->constant_child = child_ref;
    root->CaptureBaseState();
    auto ev = HistorizedBumpEvent::ptr::Create(ae::CreateWith{domain});
    ev->delta = 1;
    root->Commit(std::move(ev));
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(root, root_id).IsOk());
  }

  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, backing, supply};
  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(loaded, root_id).IsOk());
    CHECK(!HasRamUserLayer(user, const_id, HistorizedSupplyPayload::kClassId,
                           0));
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestQueryLevelForceMaterialize();
  apptraverse::test::TestDomainGraphForcePropagation();
  apptraverse::test::TestHistoricalBaseUserMaterialization();
  apptraverse::test::TestHistoricalBaseSurvivesBackingSwap();
  apptraverse::test::TestMaterializedPublicationOmitsHistory();
  apptraverse::test::TestSupplyConstantWithHistoricalBaseSave();
  std::cout << "historical_base_materialization_test OK\n";
  return 0;
}
