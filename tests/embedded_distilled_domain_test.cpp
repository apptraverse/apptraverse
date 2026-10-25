#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <utility>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (false)

struct LayerKey {
  ae::ObjId::Type object_id{};
  std::uint32_t class_id{};
  std::uint8_t version{};
};

bool LayerKeyLess(LayerKey const& a, LayerKey const& b) {
  if (a.object_id != b.object_id) {
    return a.object_id < b.object_id;
  }
  if (a.class_id != b.class_id) {
    return a.class_id < b.class_id;
  }
  return a.version < b.version;
}

ae::ObjectData const* FindLayerBytes(ae::RamDomainStorage const& storage,
                                      LayerKey const& key) {
  auto obj_it = storage.state.find(ae::ObjId{key.object_id});
  if (obj_it == storage.state.end() || !obj_it->second) {
    return nullptr;
  }
  auto class_it = obj_it->second->find(key.class_id);
  if (class_it == obj_it->second->end()) {
    return nullptr;
  }
  auto ver_it = class_it->second.find(key.version);
  if (ver_it == class_it->second.end()) {
    return nullptr;
  }
  return &ver_it->second;
}

struct OwnedEmbeddedGraph {
  std::vector<std::vector<std::uint8_t>> owned_bytes;
  std::vector<EmbeddedDistilledLayerRecord> layer_records;
  std::vector<ae::ObjId::Type> supply_id_values;
  EmbeddedDistilledGraph graph{};
};

OwnedEmbeddedGraph BuildEmbeddedGraphFromStorage(
    ae::RamDomainStorage const& storage, ae::ObjId root_id,
    std::vector<ae::ObjId::Type> supply_ids) {
  struct Row {
    LayerKey key;
    ae::ObjectData const* bytes{};
  };
  std::vector<Row> rows;
  for (auto const& [obj_id, class_map_opt] : storage.state) {
    if (!class_map_opt) {
      continue;
    }
    for (auto const& [class_id, version_map] : *class_map_opt) {
      for (auto const& [version, bytes] : version_map) {
        rows.push_back(Row{.key = {.object_id = obj_id.id(),
                                   .class_id = class_id,
                                   .version = version},
                           .bytes = &bytes});
      }
    }
  }
  std::sort(rows.begin(), rows.end(),
            [](Row const& a, Row const& b) { return LayerKeyLess(a.key, b.key); });

  OwnedEmbeddedGraph out;
  out.supply_id_values = std::move(supply_ids);
  out.layer_records.reserve(rows.size());
  out.owned_bytes.reserve(rows.size());
  for (Row const& row : rows) {
    out.owned_bytes.push_back(*row.bytes);
    std::vector<std::uint8_t> const& copy = out.owned_bytes.back();
    EmbeddedDistilledLayerRecord record{
        .object_id = row.key.object_id,
        .class_id = row.key.class_id,
        .version = row.key.version,
        .bytes = copy.data(),
        .size = copy.size()};
    out.layer_records.push_back(record);
  }
  out.graph.root_object_id = root_id.id();
  out.graph.supply_constant_ids.ids = out.supply_id_values.data();
  out.graph.supply_constant_ids.count = out.supply_id_values.size();
  out.graph.layers = out.layer_records.data();
  out.graph.layer_count = out.layer_records.size();
  return out;
}

std::size_t CountStoredObjects(ae::RamDomainStorage const& storage) {
  std::size_t count = 0;
  for (auto const& [obj_id, class_map_opt] : storage.state) {
    if (class_map_opt) {
      ++count;
    }
  }
  return count;
}

void TestLayerInstallation() {
  static std::uint8_t const kLayerA[] = {0x01, 0x02, 0x03};
  static std::uint8_t const kLayerB[] = {0x04};
  static std::uint8_t const kLayerC[] = {0x05, 0x06};
  static std::uint8_t const kZeroDummy = 0;

  ae::ObjId::Type const kObjA = 1001;
  ae::ObjId::Type const kObjB = 1002;
  ae::ObjId::Type const kObjC = 1003;
  std::uint32_t const kClassA = 0xE0010001u;
  std::uint32_t const kClassB = 0xE0010002u;
  std::uint32_t const kClassAAlt = 0xE0010003u;

  EmbeddedDistilledLayerRecord const layers[] = {
      {kObjA, kClassA, 0, kLayerA, sizeof(kLayerA)},
      {kObjB, kClassB, 0, kLayerB, sizeof(kLayerB)},
      {kObjA, kClassAAlt, 0, &kZeroDummy, 0},
      {kObjC, kClassB, 0, kLayerC, sizeof(kLayerC)},
  };
  ae::ObjId::Type const supply_ids[] = {kObjB, kObjC};
  EmbeddedDistilledGraph const graph{
      .root_object_id = kObjA,
      .supply_constant_ids = {supply_ids, 2},
      .layers = layers,
      .layer_count = 4,
  };

  std::set<ae::ObjId> const supply = EmbeddedSupplyConstantIdSet(graph);
  CHECK(supply.size() == 2);
  CHECK(supply.count(ae::ObjId{kObjB}) == 1);
  CHECK(supply.count(ae::ObjId{kObjC}) == 1);

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, graph);
  CHECK(CountStoredObjects(backing) == 3);

  ae::ObjectData const* a_v0 =
      FindLayerBytes(backing, {kObjA, kClassA, 0});
  ae::ObjectData const* a_alt =
      FindLayerBytes(backing, {kObjA, kClassAAlt, 0});
  ae::ObjectData const* b_v0 =
      FindLayerBytes(backing, {kObjB, kClassB, 0});
  ae::ObjectData const* c_v0 =
      FindLayerBytes(backing, {kObjC, kClassB, 0});
  CHECK(a_v0 != nullptr);
  CHECK(a_alt != nullptr);
  CHECK(b_v0 != nullptr);
  CHECK(c_v0 != nullptr);
  CHECK(a_v0->size() == 3 && (*a_v0)[0] == 0x01 && (*a_v0)[2] == 0x03);
  CHECK(b_v0->size() == 1 && (*b_v0)[0] == 0x04);
  CHECK(a_alt->empty());
  CHECK(c_v0->size() == 2 && (*c_v0)[1] == 0x06);
}

class EmbeddedDistilledLazyChild : public NodeFor<EmbeddedDistilledLazyChild> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::EmbeddedDistilledLazyChild",
                           EmbeddedDistilledLazyChild, Node, 0)

 protected:
  EmbeddedDistilledLazyChild() = default;

 public:
  explicit EmbeddedDistilledLazyChild(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(tag))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, tag);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, tag);
  }

  std::string tag;
};

class EmbeddedDistilledLazyRoot : public NodeFor<EmbeddedDistilledLazyRoot> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::EmbeddedDistilledLazyRoot",
                           EmbeddedDistilledLazyRoot, Node, 0)

 protected:
  EmbeddedDistilledLazyRoot() = default;

 public:
  explicit EmbeddedDistilledLazyRoot(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(lazy_child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, lazy_child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, lazy_child);
  }

  ae::ObjPtr<EmbeddedDistilledLazyChild> lazy_child;
};

APPTRAVERSE_REGISTER(EmbeddedDistilledLazyChild);
APPTRAVERSE_REGISTER(EmbeddedDistilledLazyRoot);

void TestDistilledGraphLoadFromEmbeddedBacking() {
  ae::ObjId const kRootId{0x09A10001u};
  ae::ObjId const kChildId{0x09A10002u};
  char const* const kChildTag = "embedded-lazy-child-v0";

  ae::RamDomainStorage source_storage;
  ae::Domain source_domain{source_storage};
  EmbeddedDistilledLazyRoot::ptr root_ref =
      EmbeddedDistilledLazyRoot::ptr::Create(
          ae::CreateWith{source_domain}.with_id(kRootId));
  ae::Ptr<EmbeddedDistilledLazyRoot> root = root_ref.Load();
  EmbeddedDistilledLazyChild::ptr child_ref =
      EmbeddedDistilledLazyChild::ptr::Create(
          ae::CreateWith{source_domain}
              .with_id(kChildId)
              .with_flags(ae::ObjFlags::kUnloadedByDefault));
  ae::Ptr<EmbeddedDistilledLazyChild> child = child_ref.Load();
  child->tag = kChildTag;
  root->lazy_child = child_ref;
  InitializeRuntimeNode(*child, *root);

  FinalizeDistilledGraph(*root);
  StabilizeDistilledPersistentSnapshot(*root);
  SaveDistilledRoot(*root);
  child_ref.Save();
  root->base.Save();
  child->base.Save();
  CHECK(child->base.is_valid());

  OwnedEmbeddedGraph owned = BuildEmbeddedGraphFromStorage(
      source_storage, kRootId, {kChildId.id()});

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, owned.graph);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;
  std::size_t const backing_object_count = CountStoredObjects(backing);

  ae::Domain load_domain{backing};
  EmbeddedDistilledLazyRoot::ptr loaded =
      LoadApplication<EmbeddedDistilledLazyRoot>(load_domain, kRootId);
  ae::Ptr<EmbeddedDistilledLazyRoot> loaded_root = loaded.Load();

  CHECK(loaded_root->obj_id == kRootId);
  CHECK(loaded_root->lazy_child.id() == kChildId);
  CHECK(!loaded_root->lazy_child.is_loaded());
  CHECK(CountStoredObjects(backing) == backing_object_count);
  CHECK(backing.state == backing_snapshot);

  ae::DomainQuery const child_query{kChildId,
                                    EmbeddedDistilledLazyChild::kClassId, 0};
  CHECK(backing.Load(child_query).result == ae::DomainLoadResult::kLoaded);

  ae::Ptr<EmbeddedDistilledLazyChild> loaded_child =
      loaded_root->lazy_child.Load();
  CHECK(loaded_child->tag == kChildTag);
  CHECK(loaded_child->base.is_valid());
  CHECK(CountStoredObjects(backing) == backing_object_count);
  CHECK(backing.state == backing_snapshot);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestLayerInstallation();
  apptraverse::test::TestDistilledGraphLoadFromEmbeddedBacking();
  return 0;
}
