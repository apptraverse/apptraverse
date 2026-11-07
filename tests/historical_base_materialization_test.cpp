#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <type_traits>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/event_for.h"
#include "apptraverse/journal_retention_policy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/publication_serialization_scope.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse::test {

class DerivedUiLocalizationString
    : public NodeFor<DerivedUiLocalizationString,
                     ui::UiLocalizationString> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::test::DerivedUiLocalizationString",
      DerivedUiLocalizationString, ui::UiLocalizationString, 0)

 protected:
  DerivedUiLocalizationString() = default;

 public:
  explicit DerivedUiLocalizationString(ae::ObjProp prop) : NodeFor{prop} {}
};

APPTRAVERSE_REGISTER(DerivedUiLocalizationString);

static_assert(
    !std::is_same_v<ui::UiLocalizationString::CompactionOperationScratch,
                    Node::CompactionOperationScratch>);
static_assert(
    std::is_same_v<DerivedUiLocalizationString::CompactionOperationScratch,
                   ui::UiLocalizationString::CompactionOperationScratch>);
static_assert(
    sizeof(DerivedUiLocalizationString::CompactionOperationScratch) ==
    sizeof(ui::UiLocalizationString::CompactionOperationScratch));

class HistorizedBumpEvent;
class HistorizedSetUtf8Event;

class HistorizedStringLeaf : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HistorizedStringLeaf",
                           HistorizedStringLeaf, ae::Obj, 0)

 protected:
  HistorizedStringLeaf() = default;

 public:
  explicit HistorizedStringLeaf(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(utf8))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, utf8);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, utf8);
  }

  std::string utf8;
};

class HistorizedStringOwner : public NodeFor<HistorizedStringOwner> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HistorizedStringOwner",
                           HistorizedStringOwner, Node, 0)

 protected:
  HistorizedStringOwner() = default;

 public:
  explicit HistorizedStringOwner(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, text);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, text);
  }

  HistorizedStringLeaf::ptr text;

  void Apply(HistorizedSetUtf8Event const& event);

  void PrepareReachableMutableLeavesForHistoricalCapture() override;
};

class HistorizedSetUtf8Event
    : public EventFor<HistorizedStringOwner, HistorizedSetUtf8Event> {
  APPTRAVERSE_OBJECT(HistorizedSetUtf8Event, Event, 0)

 protected:
  HistorizedSetUtf8Event() = default;

 public:
  explicit HistorizedSetUtf8Event(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(utf8))

  std::string utf8;
};

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

class HistorizedParentWithChild : public NodeFor<HistorizedParentWithChild> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::HistorizedParentWithChild",
                           HistorizedParentWithChild, Node, 0)

 protected:
  HistorizedParentWithChild() = default;

 public:
  explicit HistorizedParentWithChild(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, child);
  }

  HistorizedValueNode::ptr child;
};

void HistorizedStringOwner::PrepareReachableMutableLeavesForHistoricalCapture() {
  if (text.is_valid() && !text.is_loaded()) {
    text.Load();
  }
}

void HistorizedStringOwner::Apply(HistorizedSetUtf8Event const& event) {
  if (!text.is_valid()) {
    return;
  }
  if (!text.is_loaded()) {
    text.Load();
  }
  text->utf8 = event.utf8;
  NoteMaterializedChange();
}

APPTRAVERSE_REGISTER(HistorizedStringLeaf);
APPTRAVERSE_REGISTER(HistorizedStringOwner);
APPTRAVERSE_REGISTER(HistorizedSetUtf8Event);
APPTRAVERSE_REGISTER(HistorizedValueNode);
APPTRAVERSE_REGISTER(HistorizedBumpEvent);
APPTRAVERSE_REGISTER(HistorizedSupplyPayload);
APPTRAVERSE_REGISTER(HistorizedConstHostNode);
APPTRAVERSE_REGISTER(HistorizedParentWithChild);

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

std::filesystem::path HistoricalStringTestDir(char const* tag) {
  auto dir = std::filesystem::temp_directory_path() / "apptraverse_test" / tag;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::string Utf8OnDisk(DirectoryDomainStorage& storage, ae::ObjId leaf_id) {
  ae::Domain domain{storage};
  auto leaf = HistorizedStringLeaf::ptr::Declare(
      ae::CreateWith{domain}.with_id(leaf_id));
  leaf.Load();
  return leaf->utf8;
}

std::string OwnerResolvedUtf8(HistorizedStringOwner& owner) {
  if (!owner.text.is_valid()) {
    return {};
  }
  if (!owner.text.is_loaded()) {
    owner.text.Load();
  }
  return owner.text->utf8;
}

void CommitUtf8(HistorizedStringOwner& owner, ae::Domain& domain,
                char const* utf8) {
  auto ev = HistorizedSetUtf8Event::ptr::Create(ae::CreateWith{domain});
  ev->utf8 = utf8;
  owner.Commit(std::move(ev));
}

void TestHistoricalStringLeafBootstrapAfterOwnerSave() {
  auto const dir = HistoricalStringTestDir("historical_string_bootstrap_save");
  ae::ObjId const owner_id{0x09C20001u};
  ae::ObjId base_id{};
  ae::ObjId live_leaf_id{};

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto owner = HistorizedStringOwner::ptr::Create(
        ae::CreateWith{domain}.with_id(owner_id));
    auto leaf = HistorizedStringLeaf::ptr::Create(ae::CreateWith{domain});
    leaf->utf8 = "A";
    leaf.Save();
    live_leaf_id = leaf->obj_id;
    owner->text = HistorizedStringLeaf::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(live_leaf_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    InitializeRuntimeNode(*owner);
    base_id = owner->base.id();
    CommitUtf8(*owner, domain, "B");
    storage.BeginSaveSession();
    HistorizedStringOwner::ptr::MakeFromThis(&*owner).Save();
    CHECK(!storage.SaveSessionHadFailure());
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto historical = HistorizedStringOwner::ptr::Declare(
        ae::CreateWith{domain}.with_id(base_id));
    historical.Load();
    CHECK(OwnerResolvedUtf8(*historical) == "A");
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto owner = HistorizedStringOwner::ptr::Declare(
        ae::CreateWith{domain}.with_id(owner_id));
    owner.Load();
    CHECK(owner->TryReplayFromBase());
    CHECK(OwnerResolvedUtf8(*owner) == "B");
  }
}

void TestHistoricalStringLeafCompactionBeforeSecondSave() {
  auto const dir =
      HistoricalStringTestDir("historical_string_compact_second_save");
  ae::ObjId const owner_id{0x09C20002u};
  ae::ObjId base_id{};
  ae::ObjId live_leaf_id{};

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto owner = HistorizedStringOwner::ptr::Create(
        ae::CreateWith{domain}.with_id(owner_id));
    auto leaf = HistorizedStringLeaf::ptr::Create(ae::CreateWith{domain});
    leaf->utf8 = "A";
    leaf.Save();
    live_leaf_id = leaf->obj_id;
    owner->text = HistorizedStringLeaf::ptr::Declare(
        ae::CreateWith{domain}
            .with_id(live_leaf_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    InitializeRuntimeNode(*owner);
    CommitUtf8(*owner, domain, "B");
    CHECK(OwnerResolvedUtf8(*owner) == "B");
    storage.BeginSaveSession();
    HistorizedStringOwner::ptr::MakeFromThis(&*owner).Save();
    CHECK(!storage.SaveSessionHadFailure());
    CHECK(!owner->journal.empty());
    CHECK(Utf8OnDisk(storage, live_leaf_id) == "B");

    owner->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    owner->CompactJournal(SystemUtcMicros());
    CHECK(owner->journal.empty());
    base_id = owner->base.id();

    CommitUtf8(*owner, domain, "C");
    storage.BeginSaveSession();
    HistorizedStringOwner::ptr::MakeFromThis(&*owner).Save();
    CHECK(!storage.SaveSessionHadFailure());
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto historical = HistorizedStringOwner::ptr::Declare(
        ae::CreateWith{domain}.with_id(base_id));
    historical.Load();
    CHECK(OwnerResolvedUtf8(*historical) == "B");
  }

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto owner = HistorizedStringOwner::ptr::Declare(
        ae::CreateWith{domain}.with_id(owner_id));
    owner.Load();
    CHECK(owner->TryReplayFromBase());
    CHECK(OwnerResolvedUtf8(*owner) == "C");
  }
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

void TestCaptureParentOmitsChildNodeHistory() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  HistorizedParentWithChild::ptr parent_ref =
      HistorizedParentWithChild::ptr::Create(
          ae::CreateWith{domain}.with_id(ae::ObjId{89050}));
  ae::Ptr<HistorizedParentWithChild> parent = parent_ref.Load();
  InitializeRuntimeNode(*parent);
  HistorizedValueNode::ptr child_ref = HistorizedValueNode::ptr::Create(
      ae::CreateWith{domain}.with_id(ae::ObjId{89051}));
  ae::Ptr<HistorizedValueNode> child = child_ref.Load();
  InitializeRuntimeNode(*child);
  parent->child = child_ref;
  for (int i = 0; i < 100; ++i) {
    CommitBump(*child, domain, 1);
  }
  CHECK(child->journal.size() == 100);
  CHECK(parent->journal.empty());
  ResetPublicationSerializationStats();
  parent->CaptureBaseState();
  CHECK(ActivePublicationSerializationStats().journal_entries_serialized == 0);
  CHECK(ActivePublicationSerializationStats().node_base_snapshots_serialized ==
        0);
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

void CommitLocalizationUtf8(ui::UiLocalizationString& localized,
                            ae::Domain& domain, int slot, char const* utf8) {
  auto event = ui::SetUiLocalizationSlotUtf8Event::ptr::Create(
      ae::CreateWith{domain});
  event->slot_index = slot;
  event->utf8 = utf8;
  localized.Commit(std::move(event));
}

void TestDerivedUiLocalizationCompactionScratchInheritance() {
  ae::RamDomainStorage storage_full;
  ae::RamDomainStorage storage_partial;
  ae::ObjId const root_id_full{501};
  ae::ObjId const root_id_partial{502};
  char const* const kEnBootstrap = "Derived EN base";
  char const* const kRuBootstrap = "Derived RU base";

  auto bootstrap = [&](ae::RamDomainStorage& ram_storage, ae::ObjId root_id) {
    ae::Domain domain{ram_storage};
    auto localized = DerivedUiLocalizationString::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    InitializeRuntimeNode(*localized);

    auto en = ui::UiString::ptr::Create(ae::CreateWith{domain});
    en->utf8 = kEnBootstrap;
    auto ru = ui::UiString::ptr::Create(ae::CreateWith{domain});
    ru->utf8 = kRuBootstrap;
    ae::ObjId const en_string_id = en->obj_id;
    ae::ObjId const ru_string_id = ru->obj_id;
    en.Save();
    ru.Save();

    localized->strings = {
        ui::UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(en_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
        ui::UiString::ptr::Declare(
            ae::CreateWith{domain}
                .with_id(ru_string_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault)),
    };
    DerivedUiLocalizationString::ptr::MakeFromThis(&*localized).Save();
  };

  bootstrap(storage_full, root_id_full);
  bootstrap(storage_partial, root_id_partial);

  ae::ObjId en_slot_id;
  ae::ObjId ru_slot_id;
  {
    ae::Domain domain{storage_full};
    auto localized = DerivedUiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id_full));
    localized.Load();
    localized->CaptureBaseState();
    localized->MaterializeLanguage(0);
    CommitLocalizationUtf8(*localized, domain, 0, "Derived EN v1");
    CommitLocalizationUtf8(*localized, domain, 1, "Derived RU v1");
    CommitLocalizationUtf8(*localized, domain, 0, "Derived EN v2");
    en_slot_id = localized->strings[0].id();
    ru_slot_id = localized->strings[1].id();

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 0});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.empty());
    localized->MaterializeLanguage(0);
    CHECK(localized->Resolve(0) == "Derived EN v2");
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Derived RU v1");
    CHECK(localized->strings[0].id() == en_slot_id);
    CHECK(localized->strings[1].id() == ru_slot_id);
    DerivedUiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    localized.Reset();
  }

  {
    ae::Domain cold_domain{storage_full};
    auto cold = DerivedUiLocalizationString::ptr::Declare(
        ae::CreateWith{cold_domain}.with_id(root_id_full));
    cold.Load();
    CHECK(cold->TryReplayFromBase());
    cold->MaterializeLanguage(0);
    CHECK(cold->Resolve(0) == "Derived EN v2");
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Derived RU v1");
    cold.Reset();
  }

  ae::ObjId ru_slot_id_partial;
  {
    ae::Domain domain{storage_partial};
    auto localized = DerivedUiLocalizationString::ptr::Declare(
        ae::CreateWith{domain}.with_id(root_id_partial));
    localized.Load();
    localized->CaptureBaseState();
    localized->MaterializeLanguage(0);
    CommitLocalizationUtf8(*localized, domain, 0, "Partial EN v1");
    CommitLocalizationUtf8(*localized, domain, 1, "Partial RU v1");
    CommitLocalizationUtf8(*localized, domain, 0, "Partial EN v2");
    ru_slot_id_partial = localized->strings[1].id();
    localized->MaterializeLanguage(0);

    localized->SetJournalRetentionPolicy(JournalRetentionPolicy{.max_events = 1});
    localized->CompactJournal(SystemUtcMicros());
    CHECK(localized->journal.size() == 1);
    localized->MaterializeLanguage(1);
    CHECK(localized->Resolve(1) == "Partial RU v1");
    DerivedUiLocalizationString::ptr::MakeFromThis(&*localized).Save();
    localized.Reset();
  }

  {
    ae::Domain cold_domain{storage_partial};
    auto cold = DerivedUiLocalizationString::ptr::Declare(
        ae::CreateWith{cold_domain}.with_id(root_id_partial));
    cold.Load();
    CHECK(cold->TryReplayFromBase());
    cold->MaterializeLanguage(0);
    CHECK(cold->Resolve(0) == "Partial EN v2");
    cold->MaterializeLanguage(1);
    CHECK(cold->Resolve(1) == "Partial RU v1");
    CHECK(cold->strings[1].id() == ru_slot_id_partial);
    CHECK(cold->base.is_valid() && cold->base.is_loaded());
    CHECK(cold->base.id() != cold->obj_id);
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestQueryLevelForceMaterialize();
  apptraverse::test::TestDomainGraphForcePropagation();
  apptraverse::test::TestHistoricalBaseUserMaterialization();
  apptraverse::test::TestHistoricalBaseSurvivesBackingSwap();
  apptraverse::test::TestCaptureParentOmitsChildNodeHistory();
  apptraverse::test::TestMaterializedPublicationOmitsHistory();
  apptraverse::test::TestSupplyConstantWithHistoricalBaseSave();
  apptraverse::test::TestHistoricalStringLeafBootstrapAfterOwnerSave();
  apptraverse::test::TestHistoricalStringLeafCompactionBeforeSecondSave();
  apptraverse::test::TestDerivedUiLocalizationCompactionScratchInheritance();
  std::cout << "historical_base_materialization_test OK\n";
  return 0;
}
