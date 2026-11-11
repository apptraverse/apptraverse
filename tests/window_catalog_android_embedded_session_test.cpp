#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"

#include "embedded_distilled_window_catalog_android_supply.h"
#include "window_catalog_graph_android.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_row_text.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"
#include "window_catalog_supply_constant_ids.h"

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

ae::ObjId const kCatalogId{
    window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

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

void SaveCatalogAtBackgroundBoundary(WindowCatalog& catalog,
                                     ae::RamDomainStorage& user,
                                     std::filesystem::path const& user_dir) {
  SaveWindowCatalogPersistentSnapshot(catalog);
  CHECK(SaveUserStorageFile(user, user_dir));
  CHECK(std::filesystem::is_regular_file(user_dir / "user.bin"));
  std::size_t files = 0;
  for (auto const& entry : std::filesystem::recursive_directory_iterator(user_dir)) {
    CHECK(entry.is_regular_file());
    ++files;
  }
  CHECK(files == 1);
}

void ExpectNoLocalizationSupplyBodiesInUser(
    WindowCatalog const& catalog, OverlayDomainStorage const& overlay,
    ae::RamDomainStorage const& user) {
  for (ae::ObjId const& id : CollectWindowCatalogSupplyConstantIds(catalog)) {
    CHECK(overlay.IsSupplyConstant(id));
    CHECK(user.state.count(id) == 0);
  }
}

void MutateCatalogSession(WindowCatalog& catalog) {
  catalog.AddWindow();
  catalog.AddWindow();
  catalog.persisted.list.Load();
  CHECK(catalog.persisted.list->rows.size() == 2);
  WindowHolder* holder0 = AsHolder(catalog.persisted.list->rows[0]);
  WindowHolder* holder1 = AsHolder(catalog.persisted.list->rows[1]);
  catalog.OpenHolderWindow(*holder0);
  catalog.OpenHolderWindow(*holder1);
  catalog.CloseHolderWindow(*holder0);
  catalog.SelectHolder(WindowHolder::ptr::MakeFromThis(holder1));
  catalog.persisted.locale.Load();
  catalog.persisted.locale->SubmitLanguageIndexFromUi(1);
  catalog.MaterializeCatalogStrings(1);
  catalog.persisted.locale->SubmitLanguageIndexFromUi(0);
  catalog.MaterializeCatalogStrings(0);
  CHECK(catalog.persisted.locale->language_index() == 0);
}

void TestAndroidEmbeddedBackingUserColdLoadWithoutNormalize() {
  ae::RamDomainStorage backing;
  InstallEmbeddedDistilledBacking(
      backing, embedded_distilled::window_catalog::kEmbeddedGraph);
  ae::RamDomainStorage::ObjClassData const backing_snapshot = backing.state;

  auto const user_dir =
      TestDir("window_catalog_android_embedded_session");
  ae::RamDomainStorage user_storage;
  OverlayDomainStorage overlay{
      user_storage, backing,
      EmbeddedSupplyConstantIdSet(
          embedded_distilled::window_catalog::kEmbeddedGraph)};

  ae::ObjId holder0_id;
  ae::ObjId holder1_id;
  ae::ObjId open_window_id;
  ae::ObjId catalog_base_id;

  {
    ae::Domain domain{overlay};
    auto catalog = LoadApplication<WindowCatalog>(domain, kCatalogId);
    CHECK(std::filesystem::is_empty(user_dir));
    CHECK(!catalog->persisted.mobile_pager);
    CHECK(catalog->persisted.texts.label_close.is_valid());
    catalog_base_id = catalog->base.id();
    CHECK(catalog_base_id.is_valid());
    PrepareWindowCatalogForPagedRun(*catalog);
    CHECK(catalog->persisted.mobile_pager);
    MutateCatalogSession(*catalog);
    catalog->persisted.list.Load();
    holder0_id = AsHolder(catalog->persisted.list->rows[0])->obj_id;
    holder1_id = AsHolder(catalog->persisted.list->rows[1])->obj_id;
    open_window_id = AsHolder(catalog->persisted.list->rows[1])->window.id();
    CHECK(std::filesystem::is_empty(user_dir));
    SaveCatalogAtBackgroundBoundary(*catalog, user_storage, user_dir);
    ExpectNoLocalizationSupplyBodiesInUser(*catalog, overlay, user_storage);
    CHECK(backing.state == backing_snapshot);
    catalog.Reset();
  }

  {
    // The second model has no objects or in-memory USER bytes from the first.
    ae::RamDomainStorage cold_user;
    CHECK(LoadUserStorageFile(cold_user, user_dir));
    ae::RamDomainStorage cold_backing;
    InstallEmbeddedDistilledBacking(
        cold_backing, embedded_distilled::window_catalog::kEmbeddedGraph);
    OverlayDomainStorage cold_overlay{
        cold_user, cold_backing,
        EmbeddedSupplyConstantIdSet(
            embedded_distilled::window_catalog::kEmbeddedGraph)};
    ae::Domain cold_domain{cold_overlay};
    auto catalog = LoadApplication<WindowCatalog>(cold_domain, kCatalogId);
    CHECK(catalog->persisted.mobile_pager);
    catalog->persisted.list.Load();
    CHECK(catalog->persisted.list->rows.size() == 2);
    CHECK(AsHolder(catalog->persisted.list->rows[0])->obj_id == holder0_id);
    CHECK(AsHolder(catalog->persisted.list->rows[1])->obj_id == holder1_id);
    CHECK(AsHolder(catalog->persisted.list->rows[1])->window.id() ==
          open_window_id);
    CHECK(!AsHolder(catalog->persisted.list->rows[0])->window.is_loaded());
    CHECK(AsHolder(catalog->persisted.list->rows[1])->window.is_loaded());
    CHECK(catalog->persisted.list->selection_index == 1);
    CHECK(catalog->persisted.locale->language_index() == 0);
    CHECK(catalog->base.id() == catalog_base_id);
    CHECK(ResolveWindowCatalogHolderListRowText(
              *AsHolder(catalog->persisted.list->rows[0]), *catalog, 0) ==
          "Window 1 (Closed)");
    PrepareWindowCatalogForPagedRun(*catalog);
    CHECK(catalog->persisted.mobile_pager);
    catalog->AddWindow();
    catalog->persisted.list.Load();
    CHECK(catalog->persisted.list->rows.size() == 3);
    SaveCatalogAtBackgroundBoundary(*catalog, cold_user, user_dir);
    CHECK(backing.state == backing_snapshot);
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureWindowCatalogAndroidGraphRegistration();
  apptraverse::test::TestAndroidEmbeddedBackingUserColdLoadWithoutNormalize();
  std::cout << "window_catalog_android_embedded_session_test OK\n";
  return 0;
}
