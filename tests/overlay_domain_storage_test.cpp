#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/overlay_domain_storage.h"

namespace apptraverse::test {

class OverlayVersionedWindow : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OverlayVersionedWindow",
                           OverlayVersionedWindow, ae::Obj, 1)

 protected:
  OverlayVersionedWindow() = default;

 public:
  explicit OverlayVersionedWindow(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(posx))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, width);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, width);
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    dnv(base_, posx);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    dnv(base_, posx);
  }

  std::uint32_t width;
  std::int32_t posx;
};

class OverlayLazyRoot : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::test::OverlayLazyRoot", OverlayLazyRoot,
                           ae::Obj, 0)

 protected:
  OverlayLazyRoot() = default;

 public:
  explicit OverlayLazyRoot(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(first), AE_MMBR(second))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, first, second);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, first, second);
  }

  OverlayVersionedWindow::ptr first;
  OverlayVersionedWindow::ptr second;
};

APPTRAVERSE_REGISTER(OverlayVersionedWindow);
APPTRAVERSE_REGISTER(OverlayLazyRoot);

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

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

void TestOverlayPrefersOverlayAndFallsBackToBacking() {
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay{backing};

  ae::ObjId const id{42};
  ae::DomainQuery const backing_only{id, 7, 0};
  ae::DomainQuery const both{id, 9, 0};

  WriteBytes(backing, backing_only, {1, 2, 3});
  WriteBytes(backing, both, {10, 11});
  WriteBytes(overlay, both, {20, 21, 22});

  auto load_backing_only = overlay.Load(backing_only);
  CHECK(ReadAll(std::move(load_backing_only)) ==
        (std::vector<std::uint8_t>{1, 2, 3}));

  auto load_overlay = overlay.Load(both);
  CHECK(ReadAll(std::move(load_overlay)) ==
        (std::vector<std::uint8_t>{20, 21, 22}));

  auto backing_both = backing.Load(both);
  CHECK(ReadAll(std::move(backing_both)) ==
        (std::vector<std::uint8_t>{10, 11}));

  auto classes = overlay.Enumerate(id);
  std::set<std::uint32_t> unique{classes.begin(), classes.end()};
  CHECK(unique.count(7) == 1);
  CHECK(unique.count(9) == 1);
  CHECK(unique.size() == 2);

  overlay.CleanUp();
  auto after_cleanup = overlay.Load(both);
  CHECK(ReadAll(std::move(after_cleanup)) ==
        (std::vector<std::uint8_t>{10, 11}));
}

void TestSameClassVersionPrecedence() {
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay{backing};

  ae::ObjId const id{42};
  ae::DomainQuery const v0{id, 9, 0};
  ae::DomainQuery const v1{id, 9, 1};
  ae::DomainQuery const v2{id, 9, 2};

  WriteBytes(backing, v0, {10, 20});
  WriteBytes(backing, v1, {64, 48});
  CHECK(overlay.overlay().state.empty());
  CHECK(ReadAll(overlay.Load(v0)) == (std::vector<std::uint8_t>{10, 20}));
  CHECK(ReadAll(overlay.Load(v1)) == (std::vector<std::uint8_t>{64, 48}));
  CHECK(overlay.overlay().state.empty());

  WriteBytes(overlay, v0, {17, 29});
  CHECK(ReadAll(overlay.Load(v0)) == (std::vector<std::uint8_t>{17, 29}));
  CHECK(ReadAll(overlay.Load(v1)) == (std::vector<std::uint8_t>{64, 48}));
  CHECK(ReadAll(backing.Load(v0)) == (std::vector<std::uint8_t>{10, 20}));

  WriteBytes(overlay, v1, {74, 53});
  WriteBytes(backing, v0, {100, 200});
  WriteBytes(backing, v1, {80, 60});
  WriteBytes(backing, v2, {4});
  CHECK(ReadAll(overlay.Load(v0)) == (std::vector<std::uint8_t>{17, 29}));
  CHECK(ReadAll(overlay.Load(v1)) == (std::vector<std::uint8_t>{74, 53}));
  CHECK(ReadAll(overlay.Load(v2)) == (std::vector<std::uint8_t>{4}));
  {
    auto const obj_it = overlay.overlay().state.find(id);
    if (obj_it != overlay.overlay().state.end() && obj_it->second.has_value()) {
      auto const class_it = obj_it->second->find(9);
      if (class_it != obj_it->second->end()) {
        CHECK(class_it->second.find(2) == class_it->second.end());
      }
    }
  }

  ae::RamDomainStorage::ObjClassData const backing_before = backing.state;
  WriteBytes(overlay, v0, {30, 46});
  CHECK(backing.state == backing_before);
  CHECK(ReadAll(overlay.Load(v0)) == (std::vector<std::uint8_t>{30, 46}));
  CHECK(ReadAll(overlay.Load(v1)) == (std::vector<std::uint8_t>{74, 53}));
  CHECK(ReadAll(overlay.Load(v2)) == (std::vector<std::uint8_t>{4}));
}

void TestNativeVersionedObjectUsesMixedSources() {
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay{backing};
  ae::ObjId const object_id{42};

  {
    ae::Domain domain{backing};
    auto created = OverlayVersionedWindow::ptr::Create(
        ae::CreateWith{domain}.with_id(object_id));
    ae::Ptr<OverlayVersionedWindow> window = created.Load();
    window->width = 800;
    window->posx = 100;
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(window, object_id).IsOk());
  }

  ae::RamDomainStorage::ObjClassData const backing_after_bundle = backing.state;

  {
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(loaded);
    CHECK(loaded->GetClassId() == OverlayVersionedWindow::kClassId);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    CHECK(window->obj_id == object_id);
    CHECK(window->width == 800);
    CHECK(window->posx == 100);
    CHECK(overlay.overlay().state.empty());

    window->width = 900;
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveVersion(ae::Version<0>{}, *window, object_id).IsOk());
  }

  CHECK(backing.state == backing_after_bundle);
  {
    ae::RamDomainStorage::ObjClassData const& overlay_ram = overlay.overlay().state;
    CHECK(overlay_ram.count(object_id) == 1);
    CHECK(overlay_ram.at(object_id).has_value());
    ae::RamDomainStorage::VersionData const& versions =
        overlay_ram.at(object_id)->at(OverlayVersionedWindow::kClassId);
    CHECK(versions.count(0) == 1);
    CHECK(versions.count(1) == 0);
  }

  {
    ae::Domain domain{backing};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(loaded);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    window->width = 700;
    window->posx = 200;
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(window, object_id).IsOk());
  }

  ae::RamDomainStorage::ObjClassData const backing_before_user_save = backing.state;
  ae::RamDomainStorage::ObjClassData const overlay_before_mixed_load =
      overlay.overlay().state;

  {
    ae::Domain domain{overlay};
    CHECK(overlay.overlay().state == overlay_before_mixed_load);
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(overlay.overlay().state == overlay_before_mixed_load);
    CHECK(loaded);
    CHECK(loaded->GetClassId() == OverlayVersionedWindow::kClassId);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    CHECK(window->width == 900);
    CHECK(window->posx == 200);

    window->posx = 300;
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(window, object_id).IsOk());
  }

  CHECK(backing.state == backing_before_user_save);
  {
    ae::RamDomainStorage::ObjClassData const& overlay_ram = overlay.overlay().state;
    CHECK(overlay_ram.count(object_id) == 1);
    CHECK(overlay_ram.at(object_id).has_value());
    ae::RamDomainStorage::VersionData const& versions =
        overlay_ram.at(object_id)->at(OverlayVersionedWindow::kClassId);
    CHECK(versions.count(0) == 1);
    CHECK(versions.count(1) == 1);
  }

  ae::RamDomainStorage::ObjClassData const overlay_after_user_save =
      overlay.overlay().state;

  {
    ae::Domain domain{overlay};
    CHECK(overlay.overlay().state == overlay_after_user_save);
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(overlay.overlay().state == overlay_after_user_save);
    CHECK(loaded);
    CHECK(loaded->GetClassId() == OverlayVersionedWindow::kClassId);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    CHECK(window->obj_id == object_id);
    CHECK(window->width == 900);
    CHECK(window->posx == 300);
  }
}

void TestUnchangedBackingLayerIsNotMaterialized() {
  ae::RamDomainStorage backing;
  OverlayDomainStorage overlay{backing};

  ae::ObjId const id{42};
  ae::DomainQuery const query{id, 9, 0};

  WriteBytes(backing, query, {10, 20});
  WriteBytes(overlay, query, {10, 20});
  CHECK(overlay.overlay().state.empty());
  CHECK(ReadAll(overlay.Load(query)) == (std::vector<std::uint8_t>{10, 20}));

  WriteBytes(overlay, query, {17, 29});
  {
    ae::RamDomainStorage::ObjClassData const& overlay_ram = overlay.overlay().state;
    CHECK(overlay_ram.count(id) == 1);
    CHECK(overlay_ram.at(id).has_value());
    CHECK(overlay_ram.at(id)->at(9).count(0) == 1);
  }
  CHECK(ReadAll(overlay.Load(query)) == (std::vector<std::uint8_t>{17, 29}));

  WriteBytes(overlay, query, {10, 20});
  {
    ae::RamDomainStorage::ObjClassData const& overlay_ram = overlay.overlay().state;
    CHECK(overlay_ram.count(id) == 1);
    CHECK(overlay_ram.at(id).has_value());
    CHECK(overlay_ram.at(id)->at(9).count(0) == 1);
  }
  CHECK(ReadAll(overlay.Load(query)) == (std::vector<std::uint8_t>{10, 20}));

  ae::RamDomainStorage::ObjClassData const backing_before = backing.state;
  WriteBytes(backing, query, {30, 40});
  CHECK(backing.state != backing_before);
  CHECK(ReadAll(overlay.Load(query)) == (std::vector<std::uint8_t>{10, 20}));
  CHECK(ReadAll(backing.Load(query)) == (std::vector<std::uint8_t>{30, 40}));
}

void TestDurableVersionedOverlay() {
  std::filesystem::path const user_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_durable_overlay_user_state";
  std::filesystem::remove_all(user_root);

  ae::RamDomainStorage backing;
  ae::ObjId const object_id{42};
  ae::DomainQuery const v0_query{object_id, OverlayVersionedWindow::kClassId, 0};
  ae::DomainQuery const v1_query{object_id, OverlayVersionedWindow::kClassId, 1};

  {
    ae::Domain domain{backing};
    auto created = OverlayVersionedWindow::ptr::Create(
        ae::CreateWith{domain}.with_id(object_id));
    ae::Ptr<OverlayVersionedWindow> window = created.Load();
    window->width = 800;
    window->posx = 100;
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(window, object_id).IsOk());
  }

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(loaded);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    CHECK(window->width == 800);
    CHECK(window->posx == 100);

    window->width = 900;
    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveVersion(ae::Version<0>{}, *window, object_id).IsOk());
    CHECK(!user_storage.SaveSessionHadFailure());
  }

  {
    ae::Domain domain{backing};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(loaded);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    window->width = 700;
    window->posx = 200;
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(window, object_id).IsOk());
  }

  ae::RamDomainStorage::ObjClassData const backing_after_rebundle = backing.state;

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(loaded);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    CHECK(window->width == 900);
    CHECK(window->posx == 200);

    window->posx = 300;
    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(window, object_id).IsOk());
    CHECK(!user_storage.SaveSessionHadFailure());
  }

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(object_id, loaded).IsOk());
    CHECK(loaded);
    CHECK(loaded->GetClassId() == OverlayVersionedWindow::kClassId);
    ae::Ptr<OverlayVersionedWindow> window{loaded};
    CHECK(window->obj_id == object_id);
    CHECK(window->width == 900);
    CHECK(window->posx == 300);
  }

  CHECK(backing.state == backing_after_rebundle);
  CHECK(DirectoryDomainStorageLayerFileExists(user_root, v0_query));
  CHECK(DirectoryDomainStorageLayerFileExists(user_root, v1_query));
}

void TestLazyGraphPreservesUnloadedChildren() {
  std::filesystem::path const user_root =
      std::filesystem::temp_directory_path() /
      "apptraverse_lazy_graph_overlay_user_state";
  std::filesystem::remove_all(user_root);

  ae::RamDomainStorage backing;
  ae::ObjId const root_id{42000};
  ae::ObjId const child1_id{42001};
  ae::ObjId const child2_id{42002};
  ae::DomainQuery const child1_v0{child1_id, OverlayVersionedWindow::kClassId,
                                  0};
  ae::DomainQuery const child1_v1{child1_id, OverlayVersionedWindow::kClassId,
                                  1};
  ae::DomainQuery const child2_v0{child2_id, OverlayVersionedWindow::kClassId,
                                  0};
  ae::DomainQuery const child2_v1{child2_id, OverlayVersionedWindow::kClassId,
                                  1};

  {
    ae::Domain domain{backing};
    ae::Ptr<OverlayLazyRoot> root =
        OverlayLazyRoot::ptr::Create(ae::CreateWith{domain}.with_id(root_id))
            .Load();
    OverlayVersionedWindow::ptr child1_ref =
        OverlayVersionedWindow::ptr::Create(
            ae::CreateWith{domain}
                .with_id(child1_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault));
    ae::Ptr<OverlayVersionedWindow> child1 = child1_ref.Load();
    child1->width = 800;
    child1->posx = 100;
    OverlayVersionedWindow::ptr child2_ref =
        OverlayVersionedWindow::ptr::Create(
            ae::CreateWith{domain}
                .with_id(child2_id)
                .with_flags(ae::ObjFlags::kUnloadedByDefault));
    ae::Ptr<OverlayVersionedWindow> child2 = child2_ref.Load();
    child2->width = 600;
    child2->posx = 200;
    root->first = child1_ref;
    root->second = child2_ref;
    ae::DomainGraph graph{&domain};
    CHECK(graph.SaveRoot(root, root_id).IsOk());
    CHECK(backing.state.count(child1_id) == 1);
    CHECK(backing.state.count(child2_id) == 1);
    CHECK(backing.state.at(child1_id).has_value());
    CHECK(backing.state.at(child2_id).has_value());
    CHECK(backing.state.at(child1_id)
              ->at(OverlayVersionedWindow::kClassId)
              .count(0) == 1);
    CHECK(backing.state.at(child1_id)
              ->at(OverlayVersionedWindow::kClassId)
              .count(1) == 1);
    CHECK(backing.state.at(child2_id)
              ->at(OverlayVersionedWindow::kClassId)
              .count(0) == 1);
    CHECK(backing.state.at(child2_id)
              ->at(OverlayVersionedWindow::kClassId)
              .count(1) == 1);
  }

  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<OverlayLazyRoot> root{loaded};
    CHECK(root->first.id() == child1_id);
    CHECK(root->second.id() == child2_id);
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());
    ae::Ptr<OverlayVersionedWindow> child1 = root->first.Load();
    CHECK(child1->width == 800);
    CHECK(child1->posx == 100);
    child1->posx = 111;
    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(root, root_id).IsOk());
    CHECK(!user_storage.SaveSessionHadFailure());
    CHECK(!root->second.is_loaded());
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, child1_v1));
    CHECK(!DirectoryDomainStorageLayerFileExists(user_root, child1_v0));
    CHECK(!DirectoryDomainStorageLayerFileExists(user_root, child2_v0));
    CHECK(!DirectoryDomainStorageLayerFileExists(user_root, child2_v1));
    CHECK(backing.state == backing_snapshot);
  }

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<OverlayLazyRoot> root{loaded};
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());
    ae::Ptr<OverlayVersionedWindow> child2 = root->second.Load();
    CHECK(child2->width == 600);
    CHECK(child2->posx == 200);
    child2->width = 700;
    user_storage.BeginSaveSession();
    ae::DomainGraph save_graph{&domain};
    CHECK(save_graph.SaveRoot(root, root_id).IsOk());
    CHECK(!user_storage.SaveSessionHadFailure());
    CHECK(!root->first.is_loaded());
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, child1_v1));
    CHECK(!DirectoryDomainStorageLayerFileExists(user_root, child1_v0));
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, child2_v0));
    CHECK(!DirectoryDomainStorageLayerFileExists(user_root, child2_v1));
    CHECK(backing.state == backing_snapshot);
  }

  {
    DirectoryDomainStorage user_storage{user_root};
    OverlayDomainStorage overlay{user_storage, backing};
    ae::Domain domain{overlay};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> loaded;
    CHECK(load_graph.LoadRoot(root_id, loaded).IsOk());
    ae::Ptr<OverlayLazyRoot> root{loaded};
    CHECK(root->obj_id == root_id);
    CHECK(root->first.id() == child1_id);
    CHECK(root->second.id() == child2_id);
    CHECK(!root->first.is_loaded());
    CHECK(!root->second.is_loaded());
    ae::Ptr<OverlayVersionedWindow> child1 = root->first.Load();
    ae::Ptr<OverlayVersionedWindow> child2 = root->second.Load();
    CHECK(child1->width == 800);
    CHECK(child1->posx == 111);
    CHECK(child2->width == 700);
    CHECK(child2->posx == 200);
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, child1_v1));
    CHECK(!DirectoryDomainStorageLayerFileExists(user_root, child1_v0));
    CHECK(DirectoryDomainStorageLayerFileExists(user_root, child2_v0));
    CHECK(!DirectoryDomainStorageLayerFileExists(user_root, child2_v1));
    CHECK(backing.state == backing_snapshot);
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestOverlayPrefersOverlayAndFallsBackToBacking();
  apptraverse::test::TestSameClassVersionPrecedence();
  apptraverse::test::TestNativeVersionedObjectUsesMixedSources();
  apptraverse::test::TestUnchangedBackingLayerIsNotMaterialized();
  apptraverse::test::TestDurableVersionedOverlay();
  apptraverse::test::TestLazyGraphPreservesUnloadedChildren();
  std::cout << "overlay_domain_storage_test OK\n";
  return 0;
}
