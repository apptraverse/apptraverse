#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse::test {

class UserBackingPayload : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::UserBackingPayload",
                           UserBackingPayload, ae::Obj, 0)

 protected:
  UserBackingPayload() = default;

 public:
  explicit UserBackingPayload(ae::ObjProp prop) : Obj{prop} {}

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

class UserBackingRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::UserBackingRoot", UserBackingRoot,
                           ae::Obj, 0)

 protected:
  UserBackingRoot() = default;

 public:
  explicit UserBackingRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(constant_child), AE_MMBR(dynamic_child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, constant_child, dynamic_child);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, constant_child, dynamic_child);
  }

  UserBackingPayload::ptr constant_child;
  UserBackingPayload::ptr dynamic_child;
};

APPTRAVERSE_REGISTER(UserBackingPayload);
APPTRAVERSE_REGISTER(UserBackingRoot);

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

std::vector<std::uint8_t> ReadAll(ae::DomainLoad load) {
  CHECK(load.result == ae::DomainLoadResult::kLoaded);
  CHECK(load.reader);
  std::vector<std::uint8_t> out;
  for (;;) {
    std::uint8_t byte = 0;
    auto const result = load.reader->Read(ae::seri::DataReadTag{&byte, 1});
    if (!result) {
      break;
    }
    out.push_back(byte);
  }
  return out;
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

bool LayerFileExists(std::filesystem::path const& root,
                     ae::DomainQuery const& query) {
  std::error_code ec;
  return std::filesystem::exists(DirectoryDomainStorageLayerPath(root, query),
                                ec);
}

void TestSourcePrecedenceAndCleanup() {
  ae::RamDomainStorage backing;
  ae::RamDomainStorage user;
  OverlayDomainStorage overlay{user, backing};

  ae::ObjId const id{42};
  ae::DomainQuery const backing_only{id, 7, 0};
  ae::DomainQuery const dynamic{id, 9, 0};

  WriteBytes(backing, backing_only, {1, 2, 3});
  WriteBytes(backing, dynamic, {10, 11});
  WriteBytes(overlay, dynamic, {20, 21, 22});

  CHECK(ReadAll(overlay.Load(backing_only)) ==
        (std::vector<std::uint8_t>{1, 2, 3}));
  CHECK(ReadAll(overlay.Load(dynamic)) ==
        (std::vector<std::uint8_t>{20, 21, 22}));

  auto classes = overlay.Enumerate(id);
  std::set<std::uint32_t> unique{classes.begin(), classes.end()};
  CHECK(unique.count(7) == 1);
  CHECK(unique.count(9) == 1);

  overlay.Remove(id);
  CHECK(overlay.Load(dynamic).result == ae::DomainLoadResult::kRemoved);

  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;
  overlay.CleanUp();
  CHECK(user.state.empty());
  CHECK(backing.state == backing_snapshot);
  CHECK(ReadAll(overlay.Load(dynamic)) ==
        (std::vector<std::uint8_t>{10, 11}));
}

void TestSupplyConstantVsDynamicSameClass() {
  ae::RamDomainStorage backing;
  ae::ObjId const constant_id{88001};
  ae::ObjId const dynamic_id{88002};
  std::set<ae::ObjId> supply{constant_id};
  OverlayDomainStorage overlay{backing, std::move(supply)};

  ae::DomainQuery const layer{constant_id, 9001, 0};
  ae::DomainQuery const dyn_layer{dynamic_id, 9001, 0};

  WriteBytes(backing, layer, {1, 2});
  WriteBytes(backing, dyn_layer, {3, 4});
  WriteBytes(overlay.overlay(), layer, {9, 9, 9});

  CHECK(ReadAll(overlay.Load(layer)) == ReadAll(backing.Load(layer)));
  CHECK(overlay.Enumerate(constant_id) ==
        backing.Enumerate(constant_id));

  overlay.CleanUp();
  WriteBytes(overlay, layer, {8, 8});
  CHECK(!HasRamUserLayer(overlay.overlay(), constant_id, 9001, 0));
  CHECK(ReadAll(overlay.Load(layer)) == ReadAll(backing.Load(layer)));
  CHECK(ReadAll(overlay.Load(layer)) == ReadAll(backing.Load(layer)));

  WriteBytes(overlay, dyn_layer, {5, 6});
  CHECK(HasRamUserLayer(overlay.overlay(), dynamic_id, 9001, 0));
  CHECK(ReadAll(overlay.Load(dyn_layer)) ==
        (std::vector<std::uint8_t>{5, 6}));

  ae::RamDomainStorage::ObjClassData const backing_before = backing.state;
  overlay.Remove(constant_id);
  overlay.Remove(dynamic_id);
  CHECK(backing.state == backing_before);
  CHECK(!HasRamUserLayer(overlay.overlay(), dynamic_id, 9001, 0));
}

void TestDynamicLayerSkipAndRetainUserOverride() {
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay{backing};

  ae::ObjId const id{42};
  ae::DomainQuery const query{id, 9, 0};

  WriteBytes(backing, query, {10, 20});
  WriteBytes(overlay, query, {10, 20});
  CHECK(overlay.overlay().state.empty());

  WriteBytes(overlay, query, {17, 29});
  CHECK(HasRamUserLayer(overlay.overlay(), id, 9, 0));

  WriteBytes(overlay, query, {10, 20});
  CHECK(HasRamUserLayer(overlay.overlay(), id, 9, 0));
  CHECK(ReadAll(overlay.Load(query)) == (std::vector<std::uint8_t>{10, 20}));

  ae::DomainQuery const missing{id, 9, 1};
  CHECK(overlay.Load(missing).result == ae::DomainLoadResult::kEmpty);
  WriteBytes(overlay, missing, {});
  CHECK(HasRamUserLayer(overlay.overlay(), id, 9, 1));
  CHECK(ReadAll(overlay.Load(missing)).empty());

  WriteBytes(overlay, ae::DomainQuery{id, 9, 2}, {1});
  CHECK(ReadAll(overlay.Load(ae::DomainQuery{id, 9, 2})) ==
        (std::vector<std::uint8_t>{1}));
}

void TestRealObjectGraphUserBacking() {
  ae::RamDomainStorage backing;
  ae::ObjId const root_id{0x09B20001u};
  ae::ObjId const constant_id{0x09B20002u};
  ae::ObjId const dynamic_id{0x09B20003u};
  std::set<ae::ObjId> supply{constant_id};

  {
    ae::Domain domain{backing};
    UserBackingRoot::ptr root_ref = UserBackingRoot::ptr::Create(
        ae::CreateWith{domain}.with_id(root_id));
    ae::Ptr<UserBackingRoot> root = root_ref.Load();
    UserBackingPayload::ptr const_ref = UserBackingPayload::ptr::Create(
        ae::CreateWith{domain}
            .with_id(constant_id)
            .with_flags(ae::ObjFlags::kUnloadedByDefault));
    ae::Ptr<UserBackingPayload> const_row = const_ref.Load();
    const_row->value = 111;
    UserBackingPayload::ptr dyn_ref = UserBackingPayload::ptr::Create(
        ae::CreateWith{domain}.with_id(dynamic_id));
    ae::Ptr<UserBackingPayload> dyn_row = dyn_ref.Load();
    dyn_row->value = 222;
    root->constant_child = const_ref;
    root->dynamic_child = dyn_ref;
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(root, root_id).IsOk());
  }

  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  OverlayDomainStorage overlay{backing, supply};
  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<UserBackingRoot> root{loaded};
    CHECK(root->constant_child.id() == constant_id);
    CHECK(!root->constant_child.is_loaded());
    ae::Ptr<UserBackingPayload> dyn = root->dynamic_child.Load();
    CHECK(dyn->value == 222);
    dyn->value = 333;
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(root, root_id).IsOk());
    CHECK(!HasRamUserLayer(overlay.overlay(), constant_id,
                           UserBackingPayload::kClassId, 0));
    CHECK(HasRamUserLayer(overlay.overlay(), dynamic_id,
                          UserBackingPayload::kClassId, 0));
  }

  CHECK(backing.state == backing_snapshot);

  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<UserBackingRoot> root{loaded};
    CHECK(root->constant_child.id() == constant_id);
    ae::Ptr<UserBackingPayload> dyn = root->dynamic_child.Load();
    CHECK(dyn->value == 333);
    ae::Ptr<UserBackingPayload> constant = root->constant_child.Load();
    CHECK(constant->value == 111);
  }
}

void TestEmbeddedSupplyConstantSet() {
  static std::uint8_t const kConstBytes[] = {0xC0, 0x01};
  static std::uint8_t const kDynBytes[] = {0xD0, 0x02};
  ae::ObjId::Type const kConstId = 0x09B30001;
  ae::ObjId::Type const kDynId = 0x09B30002;
  std::uint32_t const kClass = 9002;

  EmbeddedDistilledLayerRecord const layers[] = {
      {kConstId, kClass, 0, kConstBytes, sizeof(kConstBytes)},
      {kDynId, kClass, 0, kDynBytes, sizeof(kDynBytes)},
  };
  ae::ObjId::Type const supply_ids[] = {kConstId};
  EmbeddedDistilledGraph const graph{
      .root_object_id = kDynId,
      .supply_constant_ids = {supply_ids, 1},
      .layers = layers,
      .layer_count = 2,
  };

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(backing, graph);
  std::set<ae::ObjId> const supply =
      EmbeddedSupplyConstantIdSet(graph);
  OverlayDomainStorage overlay{backing, supply};

  ae::DomainQuery const const_q{ae::ObjId{kConstId}, kClass, 0};
  ae::DomainQuery const dyn_q{ae::ObjId{kDynId}, kClass, 0};

  WriteBytes(overlay.overlay(), const_q, {9, 9});
  CHECK(ReadAll(overlay.Load(const_q)) ==
        (std::vector<std::uint8_t>{0xC0, 0x01}));
  CHECK(overlay.IsSupplyConstant(ae::ObjId{kConstId}));

  WriteBytes(overlay, dyn_q, {7, 8});
  CHECK(ReadAll(overlay.Load(dyn_q)) == (std::vector<std::uint8_t>{7, 8}));
  CHECK(HasRamUserLayer(overlay.overlay(), ae::ObjId{kDynId}, kClass, 0));
}

void TestExternalDirectoryUserPersistence() {
  std::filesystem::path const user_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_overlay_user_backing_dir_test";
  std::filesystem::remove_all(user_root);

  ae::RamDomainStorage backing;
  ae::ObjId const object_id{99001};
  ae::DomainQuery const v0{object_id, UserBackingPayload::kClassId, 0};

  {
    ae::Domain domain{backing};
    auto created = UserBackingPayload::ptr::Create(
        ae::CreateWith{domain}.with_id(object_id));
    ae::Ptr<UserBackingPayload> row = created.Load();
    row->value = 800;
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(row, object_id).IsOk());
  }

  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    ae::Ptr<UserBackingPayload> row{loaded};
    row->value = 900;
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveVersion(ae::Version<0>{}, *row, object_id).IsOk());
    CHECK(LayerFileExists(user_root, v0));
  }

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    ae::Ptr<UserBackingPayload> row{loaded};
    CHECK(row->value == 900);
  }

  CHECK(backing.state == backing_snapshot);
}

void TestLegacySingleBackingConstructor() {
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay{backing};
  ae::DomainQuery const query{ae::ObjId{1}, 2, 0};
  WriteBytes(backing, query, {1});
  WriteBytes(overlay, query, {2});
  CHECK(ReadAll(overlay.Load(query)) == (std::vector<std::uint8_t>{2}));
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestLegacySingleBackingConstructor();
  apptraverse::test::TestSourcePrecedenceAndCleanup();
  apptraverse::test::TestSupplyConstantVsDynamicSameClass();
  apptraverse::test::TestDynamicLayerSkipAndRetainUserOverride();
  apptraverse::test::TestRealObjectGraphUserBacking();
  apptraverse::test::TestEmbeddedSupplyConstantSet();
  apptraverse::test::TestExternalDirectoryUserPersistence();
  std::cout << "overlay_user_backing_test OK\n";
  return 0;
}
