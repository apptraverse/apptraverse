#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
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

void ShutdownSaveCatalog(WindowCatalog& catalog,
                         DirectoryDomainStorage& user_storage) {
  user_storage.BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  CHECK(!user_storage.SaveSessionHadFailure());
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

void ExpectNoLocalizationSupplyBodiesInUser(
    WindowCatalog const& catalog, OverlayDomainStorage const& overlay,
    std::filesystem::path const& user_root) {
  for (ae::ObjId const& id : CollectWindowCatalogSupplyConstantIds(catalog)) {
    CHECK(overlay.IsSupplyConstant(id));
    CHECK(!UserDirectoryHasObjectBody(user_root, id));
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
  DirectoryDomainStorage user_storage{user_dir};
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
    CHECK(catalog->persisted.mobile_pager);
    CHECK(catalog->persisted.texts.label_close.is_valid());
    catalog_base_id = catalog->base.id();
    CHECK(catalog_base_id.is_valid());
    PrepareWindowCatalogForRun(*catalog);
    MutateCatalogSession(*catalog);
    catalog->persisted.list.Load();
    holder0_id = AsHolder(catalog->persisted.list->rows[0])->obj_id;
    holder1_id = AsHolder(catalog->persisted.list->rows[1])->obj_id;
    open_window_id = AsHolder(catalog->persisted.list->rows[1])->window.id();
    ShutdownSaveCatalog(*catalog, user_storage);
    ExpectNoLocalizationSupplyBodiesInUser(*catalog, overlay, user_dir);
    CHECK(backing.state == backing_snapshot);
    catalog.Reset();
  }

  {
    ae::Domain cold_domain{overlay};
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
    PrepareWindowCatalogForRun(*catalog);
    catalog->AddWindow();
    catalog->persisted.list.Load();
    CHECK(catalog->persisted.list->rows.size() == 3);
    ShutdownSaveCatalog(*catalog, user_storage);
    CHECK(backing.state == backing_snapshot);
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWindowCatalogModelRegistration();
  apptraverse::EnsureWindowCatalogAndroidGraphRegistration();
  apptraverse::test::TestAndroidEmbeddedBackingUserColdLoadWithoutNormalize();
  std::cout << "window_catalog_android_embedded_session_test OK\n";
  return 0;
}
