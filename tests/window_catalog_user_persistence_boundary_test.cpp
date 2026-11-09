#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/application_model_session_storage.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

#include "embedded_distilled_window_catalog_supply.h"
#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse::test {
namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                          \
      std::exit(2);                                                          \
    }                                                                        \
  } while (false)

EmbeddedDistilledGraph const& CatalogGraph() {
  return embedded_distilled::window_catalog::kEmbeddedGraph;
}

std::filesystem::path TestUserDir(char const* tag) {
  auto const dir = std::filesystem::temp_directory_path() /
                   "apptraverse_wc_user_persistence_boundary" / tag;
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

std::size_t StoreDelta(InstrumentedDomainStorageStats const& after,
                       InstrumentedDomainStorageStats const& before) {
  return after.store_calls - before.store_calls;
}

ui::UiPresentedWindow const& CatalogWindowPresented(CatalogWindow const& window) {
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

void VerifyColdClosedChildOnDiskOnly(
    std::filesystem::path const& user_dir, ae::ObjId const& holder_id,
    ae::ObjId const& window_id, ae::ObjId const& expected_presenter_id,
    int expected_x, int expected_y, int expected_w, int expected_h) {
  auto const holder_dir = user_dir / std::to_string(holder_id.id());
  auto const window_dir = user_dir / std::to_string(window_id.id());
  CHECK(std::filesystem::is_directory(holder_dir));
  CHECK(std::filesystem::is_directory(window_dir));

  DirectoryDomainStorage cold_disk{user_dir};
  ApplicationModelSessionStorage cold_session{cold_disk, CatalogGraph()};
  ae::Domain cold_domain{cold_session.model_overlay()};

  ae::DomainGraph graph{&cold_domain};
  ae::Ptr<ae::Obj> holder_obj;
  CHECK(graph.LoadRoot(holder_id, holder_obj).IsOk());
  CHECK(holder_obj);
  auto holder = WindowHolder::ptr::MakeFromThis(
      static_cast<WindowHolder*>(holder_obj.get()));
  holder.Load();
  CHECK(holder->window.is_valid());
  CHECK(holder->window.id() == window_id);
  CHECK(!holder->window.is_loaded());

  holder->window.Load();
  CatalogWindow& child = *holder->window;
  CHECK(child.x() == expected_x);
  CHECK(child.y() == expected_y);
  CHECK(child.restore_client_width() == expected_w);
  CHECK(child.restore_client_height() == expected_h);
  CHECK(CatalogWindowPresented(child).presenter.is_valid());
  CHECK(!CatalogWindowPresented(child).presenter.is_loaded());
  auto presenter = LoadCatalogWindowPresenter(child);
  CHECK(presenter.is_valid());
  CHECK(presenter.id() == expected_presenter_id);
}

void RunBoundaryScenario() {
  std::filesystem::path const user_dir = TestUserDir("primary");
  DirectoryDomainStorage disk{user_dir};
  InstrumentedDomainStorage instrumented{disk, user_dir, &disk};
  ApplicationModelSessionStorage session{disk, CatalogGraph(), &instrumented};
  ApplicationModelRuntime runtime;
  ApplicationModelSessionStorage::ActivateScope const active{session, runtime};

  std::size_t files_after_boot = 0;
  std::size_t files_after_close = 0;
  ae::ObjId closed_holder_id{};
  ae::ObjId closed_window_id{};

  InstrumentedDomainStorageStats const stats_boot_before =
      instrumented.SnapshotStats();

  {
    ae::Domain domain{session.model_overlay()};
    auto catalog = LoadApplication<WindowCatalog>(domain, CatalogId());
    catalog->BindEmbeddedUserSession(&session);
    PrepareWindowCatalogForRun(*catalog);
    files_after_boot = CountRegularFiles(user_dir);
    CHECK(files_after_boot == 0);
    CHECK(StoreDelta(instrumented.SnapshotStats(), stats_boot_before) == 0);

    InstrumentedDomainStorageStats const stats_work_before =
        instrumented.SnapshotStats();

    catalog->AddWindow();
    catalog->AddWindow();
    catalog->persisted.list.Load();
    CHECK(catalog->persisted.list->rows.size() >= 2);

    WindowHolder* holder0 = AsHolder(catalog->persisted.list->rows[0]);
    holder0->window.Load();
    CatalogWindow& child = *holder0->window;
    child.SubmitFrameFromNative(200, 140, 501, 401, true);

    CHECK(CountRegularFiles(user_dir) == files_after_boot);
    CHECK(StoreDelta(instrumented.SnapshotStats(), stats_work_before) == 0);

    closed_holder_id = holder0->obj_id;
    closed_window_id = holder0->window.id();
    ae::ObjId const closed_presenter_id{
        CatalogWindowPresented(child).presenter.id()};
    InstrumentedDomainStorageStats const stats_close_before =
        instrumented.SnapshotStats();
    catalog->CloseHolderWindow(*holder0);
    files_after_close = CountRegularFiles(user_dir);
    CHECK(files_after_close > files_after_boot);
    CHECK(StoreDelta(instrumented.SnapshotStats(), stats_close_before) > 0);
    CHECK(!holder0->window.is_loaded());
    CHECK(holder0->window.is_valid());

    VerifyColdClosedChildOnDiskOnly(user_dir, closed_holder_id, closed_window_id,
                                    closed_presenter_id, 200, 140, 501, 401);

    catalog->persisted.locale.Load();
    catalog->MaterializeCatalogStrings(1);
    catalog->persisted.locale->SubmitLanguageIndexFromUi(1);

    InstrumentedDomainStorageStats const stats_reopen_work_before =
        instrumented.SnapshotStats();
    catalog->OpenHolderWindow(*holder0);
    holder0->window.Load();
    CatalogWindow& child_reopen = *holder0->window;
    child_reopen.SubmitFrameFromNative(210, 150, 502, 402, true);
    CHECK(CountRegularFiles(user_dir) == files_after_close);
    CHECK(StoreDelta(instrumented.SnapshotStats(), stats_reopen_work_before) ==
          0);

    FlushShutdownCatalog(*catalog, session);
  }

  std::size_t const files_after_shutdown = CountRegularFiles(user_dir);
  CHECK(files_after_shutdown >= files_after_close);

  {
    DirectoryDomainStorage cold_disk{user_dir};
    ApplicationModelSessionStorage cold_session{cold_disk, CatalogGraph()};
    ae::Domain cold_domain{cold_session.model_overlay()};
    auto catalog = LoadApplication<WindowCatalog>(cold_domain, CatalogId());
    PrepareWindowCatalogForRun(*catalog);
    catalog->persisted.list.Load();
    WindowHolder* holder0 = AsHolder(catalog->persisted.list->rows[0]);
    CHECK(holder0->obj_id == closed_holder_id);
    CHECK(holder0->window.id() == closed_window_id);
    catalog->persisted.locale.Load();
    CHECK(catalog->persisted.locale->language_index() == 1);
  }

  std::cout << "WINDOW_CATALOG_USER_PERSISTENCE_BOUNDARY ok boot_files="
            << files_after_boot << " after_close=" << files_after_close
            << " after_shutdown=" << files_after_shutdown << '\n';
}

void RunExistingUserNoWriteUntilBoundary() {
  std::filesystem::path const seed_dir = TestUserDir("seed");
  std::filesystem::path const user_dir = TestUserDir("reload");
  {
    DirectoryDomainStorage disk{seed_dir};
    ApplicationModelSessionStorage session{disk, CatalogGraph()};
    ApplicationModelRuntime runtime;
    ApplicationModelSessionStorage::ActivateScope const active{session, runtime};
    ae::Domain domain{session.model_overlay()};
    auto catalog = LoadApplication<WindowCatalog>(domain, CatalogId());
    catalog->BindEmbeddedUserSession(&session);
    PrepareWindowCatalogForRun(*catalog);
    catalog->AddWindow();
    FlushShutdownCatalog(*catalog, session);
  }
  std::error_code ec;
  std::filesystem::copy(seed_dir, user_dir,
                        std::filesystem::copy_options::recursive, ec);
  CHECK(!ec);
  std::size_t const seeded_files = CountRegularFiles(user_dir);

  DirectoryDomainStorage disk{user_dir};
  InstrumentedDomainStorage instrumented{disk, user_dir, &disk};
  ApplicationModelSessionStorage session{disk, CatalogGraph(), &instrumented};
  ApplicationModelRuntime runtime;
  ApplicationModelSessionStorage::ActivateScope const active{session, runtime};
  InstrumentedDomainStorageStats const stats0 = instrumented.SnapshotStats();

  ae::Domain domain{session.model_overlay()};
  auto catalog = LoadApplication<WindowCatalog>(domain, CatalogId());
  catalog->BindEmbeddedUserSession(&session);
  PrepareWindowCatalogForRun(*catalog);
  catalog->AddWindow();
  catalog->persisted.list.Load();
  WindowHolder* holder = AsHolder(catalog->persisted.list->rows.back());
  holder->window.Load();
  holder->window->SubmitFrameFromNative(44, 55, 320, 240, true);
  ae::ObjId const window_id = holder->window.id();
  ae::ObjId const holder_id = holder->obj_id;
  ae::ObjId const presenter_id{
      CatalogWindowPresented(*holder->window).presenter.id()};
  CHECK(CountRegularFiles(user_dir) == seeded_files);
  CHECK(StoreDelta(instrumented.SnapshotStats(), stats0) == 0);

  catalog->CloseHolderWindow(*holder);
  CHECK(CountRegularFiles(user_dir) > seeded_files);
  CHECK(StoreDelta(instrumented.SnapshotStats(), stats0) > 0);
  VerifyColdClosedChildOnDiskOnly(user_dir, holder_id, window_id, presenter_id,
                                  44, 55, 320, 240);
}

void RunSessionIsolation() {
  std::filesystem::path const dir_a = TestUserDir("iso_a");
  std::filesystem::path const dir_b = TestUserDir("iso_b");

  DirectoryDomainStorage disk_a{dir_a};
  ApplicationModelSessionStorage session_a{disk_a, CatalogGraph()};
  ApplicationModelRuntime runtime_a;
  ApplicationModelSessionStorage::ActivateScope const scope_a{session_a,
                                                            runtime_a};
  DirectoryDomainStorage disk_b{dir_b};
  ApplicationModelSessionStorage session_b{disk_b, CatalogGraph()};
  ApplicationModelRuntime runtime_b;
  ApplicationModelSessionStorage::ActivateScope const scope_b{session_b,
                                                            runtime_b};

  ae::Domain domain_a{session_a.model_overlay()};
  ae::Domain domain_b{session_b.model_overlay()};
  auto catalog_a = LoadApplication<WindowCatalog>(domain_a, CatalogId());
  auto catalog_b = LoadApplication<WindowCatalog>(domain_b, CatalogId());
  catalog_a->BindEmbeddedUserSession(&session_a);
  catalog_b->BindEmbeddedUserSession(&session_b);
  PrepareWindowCatalogForRun(*catalog_a);
  PrepareWindowCatalogForRun(*catalog_b);

  catalog_a->AddWindow();
  catalog_b->AddWindow();
  CHECK(CountRegularFiles(dir_a) == 0);
  CHECK(CountRegularFiles(dir_b) == 0);

  catalog_a->persisted.list.Load();
  catalog_b->persisted.list.Load();
  WindowHolder* holder_a = AsHolder(catalog_a->persisted.list->rows[0]);
  WindowHolder* holder_b = AsHolder(catalog_b->persisted.list->rows[0]);
  holder_a->window.Load();
  holder_a->window->SubmitFrameFromNative(10, 11, 120, 100, true);
  holder_b->window.Load();
  holder_b->window->SubmitFrameFromNative(1, 2, 100, 80, true);

  catalog_b->CloseHolderWindow(*holder_b);
  CHECK(CountRegularFiles(dir_b) > 0);
  CHECK(CountRegularFiles(dir_a) == 0);

  holder_a->window->SubmitFrameFromNative(20, 21, 130, 110, true);
  CHECK(CountRegularFiles(dir_a) == 0);
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureWindowCatalogGraphRegistration();
  apptraverse::test::RunBoundaryScenario();
  apptraverse::test::RunExistingUserNoWriteUntilBoundary();
  apptraverse::test::RunSessionIsolation();
  std::cout << "ALL_USER_PERSISTENCE_SCENARIOS_OK\n";
  return 0;
}
