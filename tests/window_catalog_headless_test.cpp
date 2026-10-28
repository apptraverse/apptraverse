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
#include "window_catalog_lifecycle.h"
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
  std::vector<std::uint8_t> bytes;
};

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

void WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
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
  int session_result = 1;

  ModelObjectProxy model_proxy{[&runtime](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  }};

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.persist = [&](WindowCatalog& catalog) -> int {
    catalog.list.Load();
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
          std::vector<std::uint8_t> bytes = runtime.TakePublishedCopy();
          runtime.NotifyPublicationConsumed();
          {
            std::lock_guard<std::mutex> lock{waiter.mu};
            waiter.kind = kind;
            waiter.bytes = std::move(bytes);
            ++waiter.pending;
          }
          waiter.cv.notify_one();
        },
        hooks);
  }};

  WaitPublication(waiter);
  CHECK(waiter.kind == ApplicationPublicationKind::Initial);
  ui_session = std::make_unique<ApplicationUiSession>(waiter.bytes, nullptr,
                                                      model_proxy);
  WindowCatalog& ui_catalog =
      static_cast<WindowCatalog&>(ui_session->root());
  CHECK(!ui_catalog.window_prefab.is_loaded());

  auto apply_incremental = [&] {
    WaitPublication(waiter);
    CHECK(waiter.kind == ApplicationPublicationKind::Incremental);
    ui_session->ApplyStructural(waiter.bytes);
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
  CHECK(CatalogWindowPresenter(*holder1->window).window.id() ==
        holder1->window.id());

  ChromeButtonPresenter(ui_catalog,
                        ui_catalog.presenters.language_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.locale.Load();
  CHECK(ui_catalog.locale->language_index() == 1);
  ui_catalog.texts.label_add.Load();
  ui_catalog.texts.label_add->strings[0].Load();
  ui_catalog.texts.label_add->strings[1].Load();
  CHECK(!ui_catalog.texts.label_add->strings[0]->utf8.empty());
  CHECK(!ui_catalog.texts.label_add->strings[1]->utf8.empty());

  ListPresenter(ui_catalog).SubmitListRowSelectionChangedFromUi(0);
  apply_incremental();
  ui_catalog.list.Load();
  CHECK(ui_catalog.list->selection_index == 0);

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

  ae::Domain cold_domain{model_storage};
  auto cold = LoadApplication<WindowCatalog>(cold_domain, catalog_id);
  PrepareWindowCatalogForRun(*cold);
  cold->list.Load();
  CHECK(cold->list->rows.size() == 1);
  CHECK(AsHolder(cold->list->rows[0])->obj_id == saved_holder_id);
  CHECK(AsHolder(cold->list->rows[0])->window.id() == saved_window_id);
  CHECK(cold->locale->language_index() == 1);
  AsHolder(cold->list->rows[0])->window.Load();
  CHECK(AsHolder(cold->list->rows[0])->window->origin_id() == kWindowPrefabId);
  CatalogWindowPresented(*AsHolder(cold->list->rows[0])->window)
      .presenter.Load();
  CHECK(CatalogWindowPresenter(*AsHolder(cold->list->rows[0])->window)
            .origin_id() == kWindowPrefabPresenterId);
  CHECK(cold->window_prefab.is_valid());
  CHECK(!cold->window_prefab.is_loaded());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestHeadlessCatalogChromeE2e();
  std::cout << "window_catalog_headless_test OK\n";
  return 0;
}
