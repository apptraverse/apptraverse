#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"

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

WindowHolder* AsHolder(ui::UiListRow::ptr const& row) {
  return static_cast<WindowHolder*>(row.operator->());
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

void TestColdLoadEmptyUserFromEmbeddedBacking() {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::window_catalog::kEmbeddedGraph);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  auto const user_dir = TestDir("window_catalog_embedded_cold_user");
  DirectoryDomainStorage user_storage{user_dir};
  OverlayDomainStorage overlay{
      user_storage, backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};
  ae::Domain domain{overlay};

  auto catalog =
      LoadApplication<WindowCatalog>(domain, catalog_id);
  CHECK(catalog->obj_id == catalog_id);
  CHECK(catalog->window_prefab.is_valid());
  CHECK(!catalog->window_prefab.is_loaded());
  catalog->list.Load();
  CHECK(catalog->list);
  CHECK(catalog->list->rows.empty());
  CHECK(backing.state == backing_snapshot);
  CHECK(std::filesystem::is_empty(user_dir));
}

void TestUserOverlayPersistsAddWindow() {
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::window_catalog::kEmbeddedGraph);

  auto const user_dir = TestDir("window_catalog_embedded_user_overlay");
  DirectoryDomainStorage user_storage{user_dir};
  OverlayDomainStorage overlay{
      user_storage, backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};

  ae::ObjId holder_id;
  {
    ae::Domain domain{overlay};
    auto catalog = LoadApplication<WindowCatalog>(domain, catalog_id);
    catalog->list.Load();
    catalog->AddWindow();
    CHECK(catalog->list->rows.size() == 1);
    holder_id = AsHolder(catalog->list->rows[0])->obj_id;
    ShutdownSaveCatalog(*catalog, user_storage);
    CHECK(!std::filesystem::is_empty(user_dir));
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
