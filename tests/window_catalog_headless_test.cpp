#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/obj/domain.h"

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/presenter.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

#include "window_catalog_graph_headless.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_ids.h"
#include "window_catalog_prepare.h"
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

ui::UiDesktopWindowPresenter& CatalogWindowPresenter(CatalogWindow& window) {
  return static_cast<ui::UiDesktopWindowPresenter&>(
      *CatalogWindowPresented(window).presenter);
}

std::filesystem::path TestDir(char const* name) {
  auto const dir =
      std::filesystem::temp_directory_path() / "apptraverse_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

struct PublicationWaiter {
  std::mutex mu;
  std::condition_variable cv;
  int pending{0};
  ApplicationPublicationKind kind{ApplicationPublicationKind::Initial};
};

ApplicationPublicationKind WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
  return waiter.kind;
}

ui::UiPushButtonPresenter& ChromeButtonPresenter(WindowCatalog& ui_catalog,
                                                ae::ObjPtr<Presenter> const& ref) {
  ref.Load();
  CHECK(ref);
  return static_cast<ui::UiPushButtonPresenter&>(*ref);
}

ui::UiListContainerPresenter& ListPresenter(WindowCatalog& ui_catalog) {
  ui_catalog.presenters.list_presenter.Load();
  CHECK(ui_catalog.presenters.list_presenter);
  return static_cast<ui::UiListContainerPresenter&>(
      *ui_catalog.presenters.list_presenter);
}

void HostUserCloseWindow(WindowCatalog& catalog, ae::ObjId window_obj_id,
                         ModelObjectProxy& proxy) {
  proxy.Invoke(catalog.obj_id, &WindowCatalog::UserCloseWindow, window_obj_id);
}

void TestHeadlessCatalogChromeE2e() {
  EnsureObjectRegistration();
  EnsureHeadlessWindowCatalogGraphRegistration();

  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  auto const profile_dir = TestDir("window_catalog_headless_e2e");

  {
    DirectoryDomainStorage bootstrap{profile_dir};
    ae::Domain bootstrap_domain{bootstrap};
    auto catalog = BuildHeadlessWindowCatalogGraph(bootstrap_domain);
    FinalizeDistilledGraph(*catalog);
    SaveDistilledRoot(*catalog);
  }

  DirectoryDomainStorage model_storage{profile_dir};
  ApplicationModelRuntime runtime;
  PublicationWaiter waiter;
  std::unique_ptr<ApplicationUiSession> ui_session;
  ae::ObjId saved_holder_id;
  ae::ObjId saved_window_id;
  std::uint32_t saved_next_number = 0;
  int session_result = 1;

  ModelObjectProxy model_proxy{[&runtime](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  }};

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.persist = [&](WindowCatalog& catalog) -> int {
    catalog.list.Load();
    CHECK(catalog.list->rows.size() == 1);
    WindowHolder* remaining = AsHolder(catalog.list->rows[0]);
    remaining->window.Load();
    if (remaining->window->origin_id() != kWindowPrefabId) {
      return 1;
    }
    CatalogWindowPresented(*remaining->window).presenter.Load();
    if (CatalogWindowPresenter(*remaining->window).origin_id() !=
        kWindowPrefabPresenterId) {
      return 1;
    }
    saved_next_number = catalog.next_window_number;
    model_storage.BeginSaveSession();
    SaveWindowCatalogPersistentSnapshot(catalog);
    if (model_storage.SaveSessionHadFailure()) {
      return 1;
    }
    saved_holder_id = remaining->obj_id;
    saved_window_id = remaining->window.id();
    return 0;
  };

  std::thread model_thread{[&] {
    session_result = RunApplicationModelSession(
        model_storage, catalog_id, runtime,
        [&](ApplicationPublicationKind kind) {
          {
            std::lock_guard<std::mutex> lock{waiter.mu};
            waiter.kind = kind;
            ++waiter.pending;
          }
          waiter.cv.notify_one();
        },
        hooks);
  }};

  CHECK(WaitPublication(waiter) == ApplicationPublicationKind::Initial);
  std::vector<std::uint8_t> initial_bytes = runtime.TakePublishedCopy();
  runtime.NotifyPublicationConsumed();
  ui_session = std::make_unique<ApplicationUiSession>(initial_bytes, nullptr,
                                                      model_proxy);
  WindowCatalog& ui_catalog =
      static_cast<WindowCatalog&>(ui_session->root());
  CHECK(!ui_catalog.window_prefab.is_loaded());

  auto apply_incremental = [&] {
    CHECK(WaitPublication(waiter) == ApplicationPublicationKind::Incremental);
    std::vector<std::uint8_t> bytes = runtime.TakePublishedCopy();
    runtime.NotifyPublicationConsumed();
    ui_session->ApplyStructural(bytes);
  };

  ChromeButtonPresenter(ui_catalog, ui_catalog.presenters.add_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.list.Load();
  CHECK(ui_catalog.list->rows.size() == 1);
  WindowHolder* holder0 = AsHolder(ui_catalog.list->rows[0]);
  holder0->window.Load();
  ae::ObjId const window0_id = holder0->window.id();
  ae::ObjId const presenter0_id =
      CatalogWindowPresenter(*holder0->window).obj_id;
  CHECK(window0_id != kWindowPrefabId);
  CHECK(presenter0_id != kWindowPrefabPresenterId);
  CHECK(CatalogWindowPresenter(*holder0->window).window.id() == window0_id);

  ChromeButtonPresenter(ui_catalog, ui_catalog.presenters.add_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.list.Load();
  CHECK(ui_catalog.list->rows.size() == 2);
  WindowHolder* holder1 = AsHolder(ui_catalog.list->rows[1]);
  holder1->window.Load();
  ae::ObjId const window1_id = holder1->window.id();
  CHECK(window1_id != window0_id);
  CHECK(window1_id != kWindowPrefabId);
  CHECK(CatalogWindowPresenter(*holder1->window).obj_id !=
        kWindowPrefabPresenterId);
  CHECK(CatalogWindowPresenter(*holder1->window).obj_id != presenter0_id);

  ChromeButtonPresenter(ui_catalog,
                        ui_catalog.presenters.language_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.locale.Load();
  CHECK(ui_catalog.locale->language_index() == 1);

  ListPresenter(ui_catalog).SubmitListRowSelectionChangedFromUi(0);
  apply_incremental();
  ui_catalog.list.Load();
  CHECK(ui_catalog.list->selection_index == 0);

  HostUserCloseWindow(ui_catalog, window0_id, model_proxy);
  apply_incremental();
  ui_catalog.list.Load();
  CHECK(ui_catalog.list->rows.size() == 2);
  holder0 = AsHolder(ui_catalog.list->rows[0]);
  holder1 = AsHolder(ui_catalog.list->rows[1]);
  holder1->window.Load();
  CHECK(holder1->window.id() == window1_id);

  ListPresenter(ui_catalog).SubmitListRowSelectionChangedFromUi(0);
  apply_incremental();
  ChromeButtonPresenter(ui_catalog, ui_catalog.presenters.open_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.list.Load();
  holder0 = AsHolder(ui_catalog.list->rows[0]);
  holder0->window.Load();
  CHECK(holder0->window.id() == window0_id);
  CHECK(CatalogWindowPresenter(*holder0->window).window.id() == window0_id);

  ListPresenter(ui_catalog).SubmitListRowSelectionChangedFromUi(0);
  apply_incremental();
  ChromeButtonPresenter(ui_catalog,
                        ui_catalog.presenters.delete_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.list.Load();
  CHECK(ui_catalog.list->rows.size() == 1);
  CHECK(AsHolder(ui_catalog.list->rows[0])->window.id() == window1_id);
  CHECK(!ui_catalog.window_prefab.is_loaded());

  runtime.RequestStop();
  model_thread.join();
  ui_session.reset();

  CHECK(session_result == 0);
  CHECK(saved_window_id == window1_id);
  CHECK(saved_next_number == 3);

  {
    ae::Domain cold_domain{model_storage};
    auto cold = LoadApplication<WindowCatalog>(cold_domain, catalog_id);
    PrepareWindowCatalogForRun(*cold);
    cold->list.Load();
    CHECK(cold->list->rows.size() == 1);
    CHECK(AsHolder(cold->list->rows[0])->obj_id == saved_holder_id);
    CHECK(AsHolder(cold->list->rows[0])->window.id() == saved_window_id);
    CHECK(cold->locale->language_index() == 1);
    CHECK(cold->next_window_number == saved_next_number);
    AsHolder(cold->list->rows[0])->window.Load();
    CHECK(AsHolder(cold->list->rows[0])->window->origin_id() == kWindowPrefabId);
    CatalogWindowPresented(*AsHolder(cold->list->rows[0])->window)
        .presenter.Load();
    CHECK(CatalogWindowPresenter(*AsHolder(cold->list->rows[0])->window)
              .origin_id() == kWindowPrefabPresenterId);
    CHECK(cold->window_prefab.is_valid());
    CHECK(!cold->window_prefab.is_loaded());
  }

  // Second UI session after cold load: Add continues numbering and proxy works.
  {
    DirectoryDomainStorage cold_storage{profile_dir};
    ApplicationModelRuntime cold_runtime;
    PublicationWaiter cold_waiter;
    std::unique_ptr<ApplicationUiSession> cold_ui;
    int cold_session_result = 1;
    ModelObjectProxy cold_proxy{[&cold_runtime](ModelObjectProxy::ModelWork work) {
      cold_runtime.Post(std::move(work));
    }};
    std::thread cold_model_thread{[&] {
      ae::Domain domain{cold_storage};
      auto catalog_obj = LoadApplication<WindowCatalog>(domain, catalog_id);
      PrepareWindowCatalogForRun(*catalog_obj);
      cold_session_result = 0;
      (void)cold_runtime.Run(
          domain, *catalog_obj,
          [&](ApplicationPublicationKind kind) {
            {
              std::lock_guard<std::mutex> lock{cold_waiter.mu};
              cold_waiter.kind = kind;
              ++cold_waiter.pending;
            }
            cold_waiter.cv.notify_one();
          },
          {});
    }};

    CHECK(WaitPublication(cold_waiter) == ApplicationPublicationKind::Initial);
    std::vector<std::uint8_t> cold_initial = cold_runtime.TakePublishedCopy();
    cold_runtime.NotifyPublicationConsumed();
    cold_ui = std::make_unique<ApplicationUiSession>(cold_initial, nullptr,
                                                       cold_proxy);
    WindowCatalog& cold_ui_catalog =
        static_cast<WindowCatalog&>(cold_ui->root());
    CHECK(cold_ui_catalog.next_window_number == saved_next_number);

    auto cold_apply = [&] {
      CHECK(WaitPublication(cold_waiter) ==
            ApplicationPublicationKind::Incremental);
      std::vector<std::uint8_t> bytes = cold_runtime.TakePublishedCopy();
      cold_runtime.NotifyPublicationConsumed();
      cold_ui->ApplyStructural(bytes);
    };

    ChromeButtonPresenter(cold_ui_catalog,
                          cold_ui_catalog.presenters.add_button_presenter)
        .NotifyClickedFromUi();
    cold_apply();
    cold_ui_catalog.list.Load();
    CHECK(cold_ui_catalog.list->rows.size() == 2);
    CHECK(cold_ui_catalog.next_window_number == saved_next_number + 1);

    cold_runtime.RequestStop();
    cold_model_thread.join();
    cold_ui.reset();
    CHECK(cold_session_result == 0);
  }
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestHeadlessCatalogChromeE2e();
  std::cout << "window_catalog_headless_test OK\n";
  return 0;
}
