#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/application/model_checkpoint_persistence.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/presenter.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_locale_settings.h"

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

ae::ObjId const kRootId{0x82000001u};
ae::ObjId const kNodeId{0x82000002u};
ae::ObjId const kLocaleId{0x82000003u};
ae::ObjId const kPresenterAId{0x82000004u};
ae::ObjId const kPresenterBId{0x82000005u};

class ObjRootGraph;
class ObjRootNode;
class ObjRootPresenter;
class ObjRootBumpEvent;

class ObjRootGraph : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ObjRootGraph", ObjRootGraph,
                           ae::Obj, 0)

 protected:
  ObjRootGraph() = default;

 public:
  explicit ObjRootGraph(ae::ObjProp prop) : ae::Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(node))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(node);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(node);
  }

  ae::ObjPtr<ObjRootNode> node;
};

class ObjRootNode : public NodeFor<ObjRootNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ObjRootNode", ObjRootNode, Node,
                           0)

 protected:
  ObjRootNode() = default;

 public:
  explicit ObjRootNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(app), AE_MMBR(locale), AE_MMBR(presenter_a),
                    AE_MMBR(presenter_b), AE_MMBR(bump_total))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, app, locale, presenter_a, presenter_b, bump_total);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, app, locale, presenter_a, presenter_b, bump_total);
  }

  void Apply(ObjRootBumpEvent const& event);

  ae::ObjPtr<ObjRootGraph> app;
  ui::UiLocaleSettings::ptr locale;
  ae::ObjPtr<ObjRootPresenter> presenter_a;
  ae::ObjPtr<ObjRootPresenter> presenter_b;
  std::int32_t bump_total{0};
};

class ObjRootBumpEvent : public EventFor<ObjRootNode, ObjRootBumpEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ObjRootBumpEvent",
                           ObjRootBumpEvent, Event, 0)

 protected:
  ObjRootBumpEvent() = default;

 public:
  explicit ObjRootBumpEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(delta))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(delta);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(delta);
  }

  std::int32_t delta{0};
};

class ObjRootPresenter : public Presenter {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ObjRootPresenter",
                           ObjRootPresenter, Presenter, 0)

 protected:
  ObjRootPresenter() = default;

 public:
  explicit ObjRootPresenter(ae::ObjProp prop) : Presenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(node), AE_MMBR(locale))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(node, locale);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(node, locale);
  }

  ae::ObjPtr<ObjRootNode> node;
  ui::UiLocaleSettings::ptr locale;
};

APPTRAVERSE_REGISTER(ObjRootGraph);
APPTRAVERSE_REGISTER(ObjRootNode);
APPTRAVERSE_REGISTER(ObjRootPresenter);
APPTRAVERSE_REGISTER(ObjRootBumpEvent);

void ObjRootNode::Apply(ObjRootBumpEvent const& event) {
  bump_total += event.delta;
}

struct BuiltObjRootGraph {
  ObjRootGraph::ptr root;
  ObjRootNode::ptr node;
  ui::UiLocaleSettings::ptr locale;
};

BuiltObjRootGraph BuildLiveGraph(ae::Domain& domain) {
  BuiltObjRootGraph g;
  g.root = ObjRootGraph::ptr::Create(ae::CreateWith{domain}.with_id(kRootId));
  g.node = ObjRootNode::ptr::Create(ae::CreateWith{domain}.with_id(kNodeId));
  InitializeRuntimeNode(*g.node);
  g.locale = ui::UiLocaleSettings::ptr::Create(
      ae::CreateWith{domain}.with_id(kLocaleId));
  InitializeRuntimeNode(*g.locale, *g.node);

  auto presenter_a = ObjRootPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kPresenterAId));
  auto presenter_b = ObjRootPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(kPresenterBId));
  presenter_a->node = g.node;
  presenter_a->locale = g.locale;
  presenter_b->node = g.node;
  presenter_b->locale = g.locale;

  g.root->node = g.node;
  g.node->app = g.root;
  g.node->locale = g.locale;
  g.node->presenter_a = presenter_a;
  g.node->presenter_b = presenter_b;

  auto bump = ObjRootBumpEvent::ptr::Create(ae::CreateWith{domain});
  bump->delta = 7;
  g.node->Commit(bump);
  CHECK(g.node->journal.size() == 1);
  CHECK(g.node->bump_total == 7);

  auto lang = ui::SetUiLanguageIndexEvent::ptr::Create(ae::CreateWith{domain});
  lang->language_index = 2;
  g.locale->Commit(lang);
  CHECK(g.locale->language_index() == 2);

  return g;
}

void ExpectLoadedGraph(ObjRootGraph& root) {
  CHECK(root.node.is_valid());
  CHECK(&*root.node->app == &root);
  CHECK(root.node->locale.is_valid());
  CHECK(root.node->locale->language_index() == 2);
  CHECK(root.node->presenter_a.is_valid());
  CHECK(root.node->presenter_b.is_valid());
  CHECK(&*root.node->presenter_a->locale == &*root.node->locale);
  CHECK(&*root.node->presenter_b->locale == &*root.node->locale);
  CHECK(&*root.node->presenter_a->node == &*root.node);
  CHECK(root.node->journal.size() == 1);
  CHECK(root.node->bump_total == 7);
  CHECK(root.node->base.is_valid());
}

void LoadSnapshotIntoFreshDomain(std::vector<std::uint8_t> const& bytes) {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto load_root = ObjRootGraph::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  ByteSource in;
  in.data = bytes.data();
  in.size = bytes.size();
  in.pos = 0;
  LoadPersistentModelSnapshot(in, domain, storage, *load_root);
  ExpectLoadedGraph(*load_root);
}

void LoadViaRuntimeCheckpointHelper(std::filesystem::path const& dir) {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto load_root = ObjRootGraph::ptr::Create(
      ae::CreateWith{domain}.with_id(kRootId));
  ModelCheckpointPaths const paths =
      ModelCheckpointPathsForStateDir(dir);
  CHECK(LoadModelCheckpoint(paths, domain, storage, *load_root));
  ExpectLoadedGraph(*load_root);
}

std::vector<std::uint8_t> ShuffleCheckpointLayerRecords(
    std::vector<std::uint8_t> const& snapshot) {
  ByteSource in;
  in.data = snapshot.data();
  in.size = snapshot.size();
  in.pos = 0;
  std::uint32_t root_id = 0;
  in.read(&root_id, sizeof(root_id));
  CHECK(in.ok);
  std::uint32_t layer_count = 0;
  in.read(&layer_count, sizeof(layer_count));
  CHECK(in.ok);
  struct LayerRecord {
    std::uint32_t obj_id;
    std::uint32_t class_id;
    std::uint8_t version;
    std::vector<std::uint8_t> payload;
  };
  std::vector<LayerRecord> layers;
  layers.reserve(layer_count);
  for (std::uint32_t i = 0; i < layer_count; ++i) {
    LayerRecord rec;
    std::uint32_t size = 0;
    in.read(&rec.obj_id, sizeof(rec.obj_id));
    in.read(&rec.class_id, sizeof(rec.class_id));
    in.read(&rec.version, sizeof(rec.version));
    in.read(&size, sizeof(size));
    CHECK(in.ok && in.pos + size <= in.size);
    rec.payload.assign(in.data + in.pos, in.data + in.pos + size);
    in.pos += size;
    layers.push_back(std::move(rec));
  }
  std::size_t const tail_pos = in.pos;
  std::vector<std::uint8_t> tail;
  tail.insert(tail.end(), in.data + tail_pos, in.data + in.size);

  std::mt19937 rng{12345u};
  std::shuffle(layers.begin(), layers.end(), rng);

  ByteSink out;
  out.write(&root_id, sizeof(root_id));
  std::uint32_t const count = static_cast<std::uint32_t>(layers.size());
  out.write(&count, sizeof(count));
  for (LayerRecord const& rec : layers) {
    std::uint32_t const oid = rec.obj_id;
    std::uint32_t const cid = rec.class_id;
    std::uint8_t const ver = rec.version;
    std::uint32_t const size = static_cast<std::uint32_t>(rec.payload.size());
    out.write(&oid, sizeof(oid));
    out.write(&cid, sizeof(cid));
    out.write(&ver, sizeof(ver));
    out.write(&size, sizeof(size));
    if (!rec.payload.empty()) {
      out.write(rec.payload.data(), rec.payload.size());
    }
  }
  out.bytes.insert(out.bytes.end(), tail.begin(), tail.end());
  return out.bytes;
}

void RunObjRootGraphPersistenceTest() {
  EnableNoninteractiveCrt();
  EnsureObjectRegistration();
  (void)&g_apptraverse_registrar_ObjRootGraph;
  (void)&g_apptraverse_registrar_ObjRootNode;
  (void)&g_apptraverse_registrar_ObjRootPresenter;
  (void)&g_apptraverse_registrar_ObjRootBumpEvent;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  BuiltObjRootGraph const live = BuildLiveGraph(domain);

  ByteSink snapshot;
  SerializePersistentModelSnapshot(*live.root, snapshot);
  CHECK(live.root->node->journal.size() == 1);

  storage = ae::RamDomainStorage{};
  LoadSnapshotIntoFreshDomain(snapshot.bytes);

  auto shuffled = ShuffleCheckpointLayerRecords(snapshot.bytes);
  storage = ae::RamDomainStorage{};
  LoadSnapshotIntoFreshDomain(shuffled);

  auto const dir =
      std::filesystem::temp_directory_path() /
      "apptraverse_checkpoint_obj_root_graph_persistence";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);
  ModelCheckpointPaths paths = ModelCheckpointPathsForStateDir(dir);
  CHECK(CommitModelCheckpoint(*live.root, paths));
  storage = ae::RamDomainStorage{};
  LoadViaRuntimeCheckpointHelper(dir);
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::RunObjRootGraphPersistenceTest();
  std::cout << "checkpoint_obj_root_graph_persistence_test OK\n";
  return 0;
}
