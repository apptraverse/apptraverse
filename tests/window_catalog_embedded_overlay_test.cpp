#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_window_presenter.h"

#include "window_catalog_distilled.h"
#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

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

ae::ObjId const kWindowPrefabId{
    window_catalog_demo::ToObjId(window_catalog_demo::ObjId::WindowPrefab)};
ae::ObjId const kWindowPrefabPresenterId{window_catalog_demo::ToObjId(
    window_catalog_demo::ObjId::WindowPrefabPresenter)};

WindowHolder* AsHolder(ui::UiListRow::ptr const& row) {
  return static_cast<WindowHolder*>(row.operator->());
}

ui::UiPresentedWindow const& CatalogWindowPresented(
    CatalogWindow const& window) {
  return static_cast<ui::UiPresentedWindow const&>(window);
}

ui::UiDesktopWindowPresenter::ptr LoadCatalogWindowPresenter(
    CatalogWindow& window) {
  CHECK(CatalogWindowPresented(window).presenter.is_valid());
  ae::Ptr<ae::Obj> presenter_obj{
      CatalogWindowPresented(window).presenter.Load()};
  CHECK(presenter_obj);
  return ui::UiDesktopWindowPresenter::ptr::MakeFromThis(
      static_cast<ui::UiDesktopWindowPresenter*>(presenter_obj.get()));
}

std::filesystem::path TestDir(char const* name) {
  auto const dir =
      std::filesystem::temp_directory_path() / "apptraverse_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

void ShutdownSaveCatalog(WindowCatalog& catalog,
                         DirectoryDomainStorage& user_storage) {
  user_storage.BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  CHECK(!user_storage.SaveSessionHadFailure());
}

bool BackingHasStoredObject(ae::RamDomainStorage const& backing,
                            ae::ObjId object_id) {
  auto const obj_it = backing.state.find(object_id);
  return obj_it != backing.state.end() && obj_it->second.has_value() &&
         !obj_it->second->empty();
}

bool UserDirectoryHasObjectBody(std::filesystem::path const& user_root,
                                ae::ObjId object_id) {
  auto const obj_dir = user_root / std::to_string(object_id.id());
  if (!std::filesystem::exists(obj_dir)) {
    return false;
  }
  for (auto const& entry : std::filesystem::directory_iterator(obj_dir)) {
    if (entry.is_directory()) {
      return true;
    }
  }
  return false;
}

void ExpectSupplyConstantsForPrefab(OverlayDomainStorage const& overlay) {
  std::set<ae::ObjId> const supply_ids = EmbeddedSupplyConstantIdSet(
      embedded_distilled::window_catalog::kEmbeddedGraph);
  CHECK(supply_ids.count(kWindowPrefabId) == 1);
  CHECK(supply_ids.count(kWindowPrefabPresenterId) == 1);
  CHECK(overlay.IsSupplyConstant(kWindowPrefabId));
  CHECK(overlay.IsSupplyConstant(kWindowPrefabPresenterId));
}

void ExpectPrefabLayersInBacking(ae::RamDomainStorage const& backing) {
  CHECK(BackingHasStoredObject(backing, kWindowPrefabId));
  CHECK(BackingHasStoredObject(backing, kWindowPrefabPresenterId));
}

void ExpectNoPrefabBodiesInUser(std::filesystem::path const& user_root) {
  CHECK(!UserDirectoryHasObjectBody(user_root, kWindowPrefabId));
  CHECK(!UserDirectoryHasObjectBody(user_root, kWindowPrefabPresenterId));
}

void TestColdLoadEmptyUserFromEmbeddedBacking() {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::window_catalog::kEmbeddedGraph);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;
  ExpectPrefabLayersInBacking(backing);

  auto const user_dir = TestDir("window_catalog_embedded_cold_user");
  DirectoryDomainStorage user_storage{user_dir};
  OverlayDomainStorage overlay{
      user_storage, backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};
  ExpectSupplyConstantsForPrefab(overlay);

  ae::Domain domain{overlay};

  auto catalog = LoadApplication<WindowCatalog>(domain, catalog_id);
  CHECK(catalog->obj_id == catalog_id);
  CHECK(catalog->window_prefab.is_valid());
  CHECK(!catalog->window_prefab.is_loaded());
  catalog->list.Load();
  CHECK(catalog->list);
  CHECK(catalog->list->rows.empty());
  CHECK(backing.state == backing_snapshot);
  CHECK(std::filesystem::is_empty(user_dir));
  ExpectNoPrefabBodiesInUser(user_dir);
}

void TestUserOverlayPersistsAddWindow() {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::window_catalog::kEmbeddedGraph);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;
  ExpectPrefabLayersInBacking(backing);

  auto const user_dir = TestDir("window_catalog_embedded_user_overlay");
  DirectoryDomainStorage user_storage{user_dir};
  OverlayDomainStorage overlay{
      user_storage, backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};
  ExpectSupplyConstantsForPrefab(overlay);

  ae::ObjId holder_id;
  ae::ObjId first_window_id;
  ae::ObjId second_window_id;
  {
    ae::Domain domain{overlay};
    auto catalog = LoadApplication<WindowCatalog>(domain, catalog_id);
    catalog->list.Load();
    catalog->AddWindow();
    CHECK(catalog->list->rows.size() == 1);
    WindowHolder* holder = AsHolder(catalog->list->rows[0]);
    holder_id = holder->obj_id;
    first_window_id = holder->window.id();
    CHECK(first_window_id != kWindowPrefabId);
    CHECK(holder->window->origin_id() == kWindowPrefabId);
    auto presenter = LoadCatalogWindowPresenter(*holder->window);
    CHECK(presenter.id() != kWindowPrefabPresenterId);
    CHECK(presenter->origin_id() == kWindowPrefabPresenterId);
    CHECK(!catalog->window_prefab.is_loaded());
    ShutdownSaveCatalog(*catalog, user_storage);
    CHECK(!std::filesystem::is_empty(user_dir));
    CHECK(backing.state == backing_snapshot);
    ExpectNoPrefabBodiesInUser(user_dir);
  }

  ae::RamDomainStorage replay_backing;
  InstallEmbeddedDistilledBacking(
      replay_backing, embedded_distilled::window_catalog::kEmbeddedGraph);
  DirectoryDomainStorage replay_user{user_dir};
  OverlayDomainStorage replay_overlay{
      replay_user, replay_backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};
  ae::Domain replay_domain{replay_overlay};
  auto replay_catalog =
      LoadApplication<WindowCatalog>(replay_domain, catalog_id);
  replay_catalog->list.Load();
  CHECK(replay_catalog->list->rows.size() == 1);
  CHECK(AsHolder(replay_catalog->list->rows[0])->obj_id == holder_id);
  CHECK(AsHolder(replay_catalog->list->rows[0])->window.id() == first_window_id);
  CHECK(replay_catalog->window_prefab.is_valid());
  CHECK(!replay_catalog->window_prefab.is_loaded());

  replay_catalog->AddWindow();
  CHECK(replay_catalog->list->rows.size() == 2);
  WindowHolder* holder2 = AsHolder(replay_catalog->list->rows[1]);
  second_window_id = holder2->window.id();
  CHECK(second_window_id != first_window_id);
  CHECK(second_window_id != kWindowPrefabId);
  CHECK(holder2->window->origin_id() == kWindowPrefabId);
  auto presenter2 = LoadCatalogWindowPresenter(*holder2->window);
  CHECK(presenter2->origin_id() == kWindowPrefabPresenterId);
  CHECK(!replay_catalog->window_prefab.is_loaded());

  ShutdownSaveCatalog(*replay_catalog, replay_user);
  CHECK(replay_backing.state == backing_snapshot);
  ExpectNoPrefabBodiesInUser(user_dir);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::EnsureWindowCatalogModelRegistration();
  apptraverse::test::TestColdLoadEmptyUserFromEmbeddedBacking();
  apptraverse::test::TestUserOverlayPersistsAddWindow();
  std::cout << "window_catalog_embedded_overlay_test OK\n";
  return 0;
}
