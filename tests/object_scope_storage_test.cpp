#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/object_clone.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/partitioned_model_domain_storage.h"

namespace apptraverse::test {
namespace {
#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "object_scope_storage_test: " #condition << " line " << __LINE__ << '\n'; \
  std::exit(1); } } while (false)

class ScopeProbe : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::ScopeProbe", ScopeProbe, ae::Obj, 0)
 protected:
  ScopeProbe() = default;
 public:
  explicit ScopeProbe(ae::ObjProp prop) : Obj{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(child), AE_MMBR(back), AE_MMBR(external), AE_MMBR(constant))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, value, child, back, external, constant);
  }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, value, child, back, external, constant);
  }
  int value{};
  ScopeProbe::ptr child, back, external, constant;
};
APPTRAVERSE_REGISTER(ScopeProbe);

struct TemporaryTree {
  TemporaryTree() : path{std::filesystem::temp_directory_path() /
      ("apptraverse_scope_" + std::to_string(ae::ObjId::GenerateUnique().id()))} {
    CHECK(std::filesystem::create_directory(path));
  }
  ~TemporaryTree() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
  std::filesystem::path path;
};

void Put(ae::IDomainStorage& storage, ae::DomainQuery query,
         ae::ObjectData const& bytes) {
  auto writer = storage.Store(query);
  CHECK(writer != nullptr);
  CHECK(writer->Write(ae::seri::DataWriteTag{bytes.data(), bytes.size()}).IsOk());
}

void TestScopeStorageOverlayAndWrappers() {
  TemporaryTree tree;
  ae::RamDomainStorage backing;
  DirectoryDomainStorage user{tree.path};
  InstrumentedDomainStorage instrumented{user};
  PartitionedModelDomainStorage forwarded{instrumented};
  OverlayDomainStorage overlay{forwarded, backing};
  ae::ObjId const id{4001u};
  ae::ObjectScope const scope{ae::ObjId{4000u}, ae::ObjId{3999u}, true};
  ae::DomainQuery obj{id, ae::Obj::kClassId, 0};
  obj.object_scope = scope;
  ae::ObjectData const bytes{1, 2, 3, 4};
  backing.SaveData(obj, ae::ObjectData{bytes});
  ae::DomainQuery derived{id, ScopeProbe::kClassId, 0};
  derived.object_scope = scope;
  backing.SaveData(derived, ae::ObjectData{5});

  // An ordinary derived-layer override must inherit scope from BACKING when
  // its unchanged Obj layer is elided. Layer overlay is not a child domain.
  Put(overlay, obj, bytes);
  Put(overlay, derived, {6});
  CHECK(!user.ReadObjectScope(id));
  CHECK(overlay.ReadObjectScope(id) == scope);
  CHECK(overlay.IsConstantObject(id));

  // A scope-only reset must survive even though all Obj bytes are unchanged.
  obj.object_scope = ae::ObjectScope{};
  user.BeginSaveSession();
  Put(overlay, obj, bytes);
  CHECK(user.ReadObjectScope(id).has_value());
  CHECK(*user.ReadObjectScope(id) == ae::ObjectScope{});
  CHECK(!overlay.IsConstantObject(id));
  CHECK(forwarded.ReadObjectScope(id) == ae::ObjectScope{});
  CHECK(!instrumented.IsConstantObject(id));
  CHECK(user.SnapshotSaveSessionStats().scope_metadata_written == 1);

  // The next equal explicit Save must not rewrite the metadata file.
  user.BeginSaveSession();
  Put(overlay, obj, bytes);
  CHECK(user.SnapshotSaveSessionStats().scope_metadata_written == 0);

  // Adding scope to legacy BACKING (which has no metadata) is also a change.
  ae::ObjId const legacy_id{4002u};
  ae::DomainQuery legacy{legacy_id, ae::Obj::kClassId, 0};
  backing.SaveData(legacy, ae::ObjectData{bytes});
  legacy.object_scope = ae::ObjectScope{legacy_id, {}, false};
  Put(overlay, legacy, bytes);
  CHECK(user.ReadObjectScope(legacy_id) == legacy.object_scope);
  ae::DomainQuery unchanged_derived{legacy_id, ScopeProbe::kClassId, 0};
  backing.SaveData(unchanged_derived, ae::ObjectData{7});
  unchanged_derived.object_scope = legacy.object_scope;
  Put(overlay, unchanged_derived, {7});
  CHECK(!DirectoryDomainStorageLayerFileExists(tree.path, unchanged_derived));

  // Default top-level mutable objects need no metadata file.
  ae::ObjId const plain_id{4003u};
  ae::DomainQuery plain{plain_id, ae::Obj::kClassId, 0};
  plain.object_scope = ae::ObjectScope{};
  user.BeginSaveSession();
  Put(user, plain, bytes);
  CHECK(!user.ReadObjectScope(plain_id));
  CHECK(user.SnapshotSaveSessionStats().scope_metadata_written == 0);
  CHECK(!user.Enumerate(legacy_id).empty()); // .scope is not a class directory.
}

void TestScopeMetadataWriteFailurePreservesPreviousState() {
  TemporaryTree tree;
  DirectoryDomainStorage storage{tree.path};
  ae::ObjId const id{4200u};
  ae::ObjectScope const original_scope{ae::ObjId{4199u}, {}, false};
  ae::DomainQuery query{id, ae::Obj::kClassId, 0};
  query.object_scope = original_scope;
  ae::ObjectData const original_bytes{7, 8, 9};
  Put(storage, query, original_bytes);
  CHECK(storage.ReadObjectScope(id) == original_scope);
  auto const layer_path = DirectoryDomainStorageLayerPath(tree.path, query);
  auto read_layer = [&] {
    std::ifstream input{layer_path, std::ios::binary};
    CHECK(input);
    return ae::ObjectData{std::istreambuf_iterator<char>{input},
                          std::istreambuf_iterator<char>{}};
  };
  CHECK(read_layer() == original_bytes);

  // Match the existing layer-write failure fixture: a directory cannot be
  // opened as the temporary metadata file, regardless of file permissions.
  auto const blocked_temp = tree.path / std::to_string(id.id()) / ".scope.tmp";
  CHECK(std::filesystem::create_directory(blocked_temp));
  query.object_scope = ae::ObjectScope{id, {}, false};
  storage.BeginSaveSession();
  Put(storage, query, {99}); // Commit failure occurs when the writer closes.
  auto const stats = storage.SnapshotSaveSessionStats();
  CHECK(storage.SaveSessionHadFailure());
  CHECK(stats.layers_failed_commits == 1);
  CHECK(stats.layers_written == 0);
  CHECK(stats.scope_metadata_written == 0);
  CHECK(stats.scope_metadata_bytes == 0);
  CHECK(storage.ReadObjectScope(id) == original_scope);
  CHECK(read_layer() == original_bytes);
  CHECK(std::filesystem::is_directory(blocked_temp));
}

void TestColdDirectoryCloneRetainsScope() {
  TemporaryTree tree;
  ae::ObjId const root_id{5100u}, child_id{5101u}, external_id{5102u}, constant_id{5103u};
  {
    DirectoryDomainStorage storage{tree.path};
    InstrumentedDomainStorage checked{storage};
    ae::Domain parent{checked}, source{checked};
    source.set_parent_domain(&parent);
    source.SetObjectScope(root_id);
    auto external = ScopeProbe::ptr::Create(ae::CreateWith{parent}.with_id(external_id));
    auto root = ScopeProbe::ptr::Create(ae::CreateWith{source}.with_id(root_id));
    auto child = ScopeProbe::ptr::Create(ae::CreateWith{source}.with_id(child_id));
    auto constant = ScopeProbe::ptr::Create(ae::CreateWith{source}.with_id(constant_id));
    external->value = 7;
    root->value = 11;
    child->value = 13;
    constant->value = 17;
    auto immutable = constant->object_scope();
    immutable.is_constant = true;
    constant->SetObjectScope(immutable);
    root->child = child;
    child->back = root;
    root->external = external;
    root->constant = constant;
    constant->external = external;
    CHECK(checked.SnapshotStats().store_calls == 0);
    CHECK(std::filesystem::is_empty(tree.path));
    CHECK(root->object_scope().root_id == root_id);
    CHECK(child->object_scope().root_id == root_id);
    CHECK(ae::DomainGraph{&source}.SaveRoot(root.Load(), root_id).IsOk());
    CHECK(storage.IsConstantObject(constant_id));
    CHECK(!storage.ReadObjectScope(external_id));
  }
  ae::ObjId first_clone_id{}, first_clone_child{};
  {
    DirectoryDomainStorage storage{tree.path};
    InstrumentedDomainStorage checked{storage};
    ae::Domain cold{checked};
    auto root = ScopeProbe::ptr::Declare(ae::CreateWith{cold}.with_id(root_id));
    CHECK(root.Load());
    CHECK(root->object_scope().root_id == root_id);
    CHECK(root->child->object_scope().root_id == root_id);
    CHECK(root->constant->object_scope().is_constant);
    CloneOptions options{};
    options.stable_origin = root_id;
    options.stable_prefab_ids = {child_id};
    auto const stores_before = checked.SnapshotStats().store_calls;
    auto result = CloneCanonicalRoot(cold, *root, cold, options);
    CHECK(result);
    ae::Ptr<ScopeProbe> clone{ae::Ptr<ae::Obj>{result.root.Load()}};
    CHECK(clone->child.id() != child_id);
    CHECK(clone->child->back.Load().get() == clone.get());
    CHECK(clone->external.id() == external_id);
    CHECK(clone->constant.id() == constant_id);
    CHECK(clone->external.Load().get() == root->external.Load().get());
    CHECK(clone->constant.Load().get() == root->constant.Load().get());
    CHECK(clone->object_scope().root_id == clone->obj_id);
    CHECK(clone->child->object_scope().root_id == clone->obj_id);
    CHECK(checked.SnapshotStats().store_calls == stores_before);
    first_clone_id = clone->obj_id;
    first_clone_child = clone->child.id();
    CHECK(ae::DomainGraph{&cold}.SaveRoot(clone, first_clone_id).IsOk());
  }
  {
    DirectoryDomainStorage storage{tree.path};
    InstrumentedDomainStorage checked{storage};
    ae::Domain cold{checked};
    auto root = ScopeProbe::ptr::Declare(ae::CreateWith{cold}.with_id(first_clone_id));
    CHECK(root.Load());
    CHECK(root->object_scope().root_id == first_clone_id);
    CloneOptions options{};
    options.stable_origin = root->origin_id();
    auto const stores_before = checked.SnapshotStats().store_calls;
    auto result = CloneCanonicalRoot(cold, *root, cold, options);
    CHECK(result);
    ae::Ptr<ScopeProbe> clone{ae::Ptr<ae::Obj>{result.root.Load()}};
    CHECK(clone->child.id() != first_clone_child);
    CHECK(clone->child->back.Load().get() == clone.get());
    CHECK(clone->external.id() == external_id && clone->constant.id() == constant_id);
    CHECK(clone->object_scope().root_id == clone->obj_id);
    CHECK(clone->child->object_scope().root_id == clone->obj_id);
    CHECK(checked.SnapshotStats().store_calls == stores_before);
  }
}

void TestEmbeddedScopeTransport() {
  TemporaryTree tree;
  ae::RamDomainStorage source;
  ae::Domain outer{source};
  ae::Domain domain{source};
  ae::ObjId const root_id{6100u};
  domain.SetObjectScope(root_id);
  auto root = ScopeProbe::ptr::Create(ae::CreateWith{domain}.with_id(root_id));
  root->value = 27;
  auto child = ScopeProbe::ptr::Create(ae::CreateWith{domain});
  auto immutable = child->object_scope();
  immutable.is_constant = true;
  child->SetObjectScope(immutable);
  root->constant = child;
  // An explicit top-level membership overrides an inherited scope. Its
  // presence must survive export, even though its field values are default.
  auto external = ScopeProbe::ptr::Create(ae::CreateWith{outer});
  root->external = external;
  CHECK(ae::DomainGraph{&domain}.SaveRoot(root.Load(), root_id).IsOk());
  CHECK(source.ReadObjectScope(external.id()) == ae::ObjectScope{});
  std::vector<EmbeddedDistilledLayerRecord> layers;
  std::vector<EmbeddedObjectScopeRecord> scopes;
  for (auto const& [id, classes] : source.state) {
    if (!classes) continue;
    for (auto const& [class_id, versions] : *classes) {
      for (auto const& [version, bytes] : versions) {
        layers.push_back({id.id(), class_id, version, bytes.data(), bytes.size()});
      }
    }
  }
  for (auto const& [id, scope] : source.object_scopes) {
    scopes.push_back({id.id(), scope.root_id.id(), scope.parent_root_id.id(), scope.is_constant});
  }
  EmbeddedDistilledGraph graph{root_id.id(), {}, layers.data(), layers.size(), scopes.data(), scopes.size()};
  ae::RamDomainStorage embedded;
  InstallEmbeddedDistilledBacking(embedded, graph);
  CHECK(embedded.object_scopes == source.object_scopes);
  CHECK(embedded.IsConstantObject(child.id()));
  CHECK(WriteEmbeddedDistilledGraphToDirectory(tree.path / "backing", graph));
  ae::RamDomainStorage roundtrip;
  CHECK(LoadDirectoryDomainStorageIntoRam(tree.path / "backing", roundtrip));
  CHECK(roundtrip.state == source.state);
  CHECK(roundtrip.object_scopes == source.object_scopes);
  DirectoryDomainStorage disk{tree.path / "backing"};
  CHECK(disk.ReadObjectScope(external.id()) == ae::ObjectScope{});
  ae::Domain cold{disk};
  auto loaded = ScopeProbe::ptr::Declare(ae::CreateWith{cold}.with_id(root_id));
  CHECK(loaded.Load() && loaded->value == 27);
  CHECK(loaded->object_scope().root_id == root_id);
  CHECK(loaded->constant->object_scope().is_constant);
  auto const header = tree.path / "embedded.h";
  CHECK(WriteEmbeddedDistilledSupplyHeader(header, "scope_test", "SCOPE_TEST_H_",
        root_id, {}, source, CollectStoredObjectIds(source)));
  std::ifstream input{header};
  std::string text{std::istreambuf_iterator<char>{input}, {}};
  CHECK(text.find("EmbeddedObjectScopeRecord kObjectScopes[]") != std::string::npos);
  CHECK(text.find("kObjectScopes, 3") != std::string::npos);
}
} // namespace
} // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestScopeStorageOverlayAndWrappers();
  apptraverse::test::TestScopeMetadataWriteFailurePreservesPreviousState();
  apptraverse::test::TestColdDirectoryCloneRetainsScope();
  apptraverse::test::TestEmbeddedScopeTransport();
  std::cout << "object_scope_storage_test OK\n";
}
