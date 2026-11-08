#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_session_storage.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "embedded_distilled_window_catalog_supply.h"
#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(2);                                                          \
    }                                                                        \
  } while (false)

std::filesystem::path TestUserDir() {
  auto const dir = std::filesystem::temp_directory_path() /
                   "apptraverse_wc_user_persistence_boundary";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::size_t CountRegularFiles(std::filesystem::path const& root) {
  std::size_t count = 0;
  if (!std::filesystem::exists(root)) {
    return 0;
  }
  for (auto const& entry :
       std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      ++count;
    }
  }
  return count;
}

void FlushShutdownCatalog(WindowCatalog& catalog,
                          ApplicationModelSessionStorage& session) {
  session.disk_user().BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  session.StagePersistentGraphInSessionOverlay(catalog);
  session.FlushEntireSessionOverlayToDisk();
  CHECK(!session.disk_user().SaveSessionHadFailure());
}

WindowHolder* AsHolder(ui::UiListRow::ptr const& row) {
  return static_cast<WindowHolder*>(row.operator->());
}

ae::ObjId CatalogId() {
  return window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog);
}

void RunBoundaryScenario() {
  std::filesystem::path const user_dir = TestUserDir();
  DirectoryDomainStorage disk{user_dir};
  ApplicationModelSessionStorage session{
      disk, embedded_distilled::window_catalog::kEmbeddedGraph};
  ApplicationModelSessionStorage::ActivateScope const active{session};

  std::size_t files_after_boot = 0;
  std::size_t files_after_close = 0;
  ae::ObjId closed_window_id{};

  {
    ae::Domain domain{session.model_overlay()};
    auto catalog = LoadApplication<WindowCatalog>(domain, CatalogId());
    PrepareWindowCatalogForRun(*catalog);
    files_after_boot = CountRegularFiles(user_dir);
    CHECK(files_after_boot == 0);

    catalog->AddWindow();
    catalog->AddWindow();
    catalog->persisted.list.Load();
    CHECK(catalog->persisted.list->rows.size() >= 2);

    WindowHolder* holder0 = AsHolder(catalog->persisted.list->rows[0]);
    holder0->window.Load();
    CatalogWindow& child = *holder0->window;
    child.SubmitFrameFromNative(200, 140, 501, 401, true);
    catalog->persisted.locale.Load();
    catalog->MaterializeCatalogStrings(1);
    catalog->persisted.locale->SubmitLanguageIndexFromUi(1);

    CHECK(CountRegularFiles(user_dir) == files_after_boot);

    closed_window_id = holder0->window.id();
    catalog->CloseHolderWindow(*holder0);
    files_after_close = CountRegularFiles(user_dir);
    CHECK(files_after_close > files_after_boot);
    CHECK(!holder0->window.is_loaded());
    CHECK(holder0->window.is_valid());

    catalog->OpenHolderWindow(*holder0);
    holder0->window.Load();
    CatalogWindow& child_reopen = *holder0->window;
    child_reopen.SubmitFrameFromNative(210, 150, 502, 402, true);
    CHECK(CountRegularFiles(user_dir) == files_after_close);

    FlushShutdownCatalog(*catalog, session);
  }

  std::size_t const files_after_shutdown = CountRegularFiles(user_dir);
  CHECK(files_after_shutdown >= files_after_close);

  {
    DirectoryDomainStorage cold_disk{user_dir};
    ApplicationModelSessionStorage cold_session{
        cold_disk, embedded_distilled::window_catalog::kEmbeddedGraph};
    ae::Domain cold_domain{cold_session.model_overlay()};
    auto catalog = LoadApplication<WindowCatalog>(cold_domain, CatalogId());
    PrepareWindowCatalogForRun(*catalog);
    catalog->persisted.list.Load();
    WindowHolder* holder0 = AsHolder(catalog->persisted.list->rows[0]);
    CHECK(holder0->window.id() == closed_window_id);
    catalog->persisted.locale.Load();
    CHECK(catalog->persisted.locale->language_index() == 1);
  }

  std::cout << "WINDOW_CATALOG_USER_PERSISTENCE_BOUNDARY ok boot_files="
            << files_after_boot << " after_close=" << files_after_close
            << " after_shutdown=" << files_after_shutdown << '\n';
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::test::RunBoundaryScenario();
  return 0;
}
