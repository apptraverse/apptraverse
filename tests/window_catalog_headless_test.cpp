#include <condition_variable>
#include <deque>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
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
#include "apptraverse/object_clone.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

#include "window_catalog_graph_headless.h"
#include "window_catalog_chrome_layout.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_row_text.h"
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

void TestIndependentSuppliesHaveStableIdentitiesAndBytes() {
  ae::RamDomainStorage first;
  ae::RamDomainStorage second;
  {
    ae::Domain domain{first};
    auto catalog = BuildHeadlessWindowCatalogGraph(domain);
    FinalizeDistilledGraph(*catalog);
    SaveDistilledRoot(*catalog);
    CHECK(domain.GenerateObjectId().id() >= 10000);
  }
  for (int i = 0; i < 37; ++i) {
    (void)ae::ObjId::GenerateUnique();
  }
  {
    ae::Domain domain{second};
    auto catalog = BuildHeadlessWindowCatalogGraph(domain);
    FinalizeDistilledGraph(*catalog);
    SaveDistilledRoot(*catalog);
  }
  CHECK(!first.state.empty());
  CHECK(first.state == second.state);
  CHECK(first.object_scopes == second.object_scopes);
  for (auto const& [id, layers] : first.state) {
    CHECK(id.id() < 10000);
    CHECK(layers.has_value());
  }
}

void ShutdownSaveCatalog(WindowCatalog& catalog,
                         DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  CHECK(!storage.SaveSessionHadFailure());
}

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
  ui_catalog.persisted.presenters.list_presenter.Load();
  CHECK(ui_catalog.persisted.presenters.list_presenter);
  return static_cast<ui::UiListContainerPresenter&>(
      *ui_catalog.persisted.presenters.list_presenter);
}

void WaitPublication(PublicationWaiter& waiter) {
  std::unique_lock<std::mutex> lock{waiter.mu};
  waiter.cv.wait(lock, [&] { return waiter.pending > 0; });
  --waiter.pending;
}

void ExpectChromeOpenDeleteDisabled(WindowCatalog& catalog) {
  catalog.persisted.buttons.open_button.Load();
  catalog.persisted.buttons.delete_button.Load();
  CHECK(!catalog.persisted.buttons.open_button->enabled());
  CHECK(!catalog.persisted.buttons.delete_button->enabled());
}

void ExpectChromeOpenDeleteEnabled(WindowCatalog& catalog) {
  catalog.persisted.buttons.open_button.Load();
  catalog.persisted.buttons.delete_button.Load();
  CHECK(catalog.persisted.buttons.open_button->enabled());
  CHECK(catalog.persisted.buttons.delete_button->enabled());
}

void ExpectNoSelectionOneRow(WindowCatalog& catalog,
                             ae::ObjId const& expected_holder_id,
                             ae::ObjId const& expected_window_id) {
  catalog.persisted.list.Load();
  CHECK(catalog.persisted.list->rows.size() == 1);
  CHECK(catalog.persisted.list->selection_index == -1);
  CHECK(!catalog.persisted.selected.is_valid());
  WindowHolder* only = AsHolder(catalog.persisted.list->rows[0]);
  CHECK(only->obj_id == expected_holder_id);
  CHECK(only->window.id() == expected_window_id);
  CHECK(!only->window.is_loaded());
  ExpectChromeOpenDeleteDisabled(catalog);
}

int CountLoadedCatalogChildWindows(WindowCatalog& catalog) {
  catalog.persisted.list.Load();
  int n = 0;
  for (auto const& row : catalog.persisted.list->rows) {
    auto* h = AsHolder(row);
    if (h->window.is_loaded()) {
      ++n;
    }
  }
  return n;
}

void RunDeleteMatrixScenario(ae::Domain& domain, WindowCatalog& catalog,
                             int window_count, int close_index, int select_index) {
  (void)domain;
  while (static_cast<int>(catalog.persisted.list->rows.size()) < window_count) {
    catalog.AddWindow();
  }
  catalog.persisted.list.Load();
  CHECK(catalog.persisted.list->rows.size() ==
        static_cast<std::size_t>(window_count));
  for (int i = 0; i < window_count; ++i) {
    if (i == close_index) {
      WindowHolder* h = AsHolder(catalog.persisted.list->rows[static_cast<std::size_t>(i)]);
      h->window.Load();
      catalog.UserCloseWindow(h->window.id());
      CHECK(!h->window.is_loaded());
    }
  }
  catalog.SubmitSelectionFromUi(static_cast<std::size_t>(select_index));
  catalog.persisted.list.Load();
  CHECK(catalog.persisted.list->selection_index ==
        static_cast<std::int32_t>(select_index));
  catalog.DeleteSelected();
  catalog.persisted.list.Load();
  CHECK(catalog.persisted.list->rows.size() ==
        static_cast<std::size_t>(window_count - 1));
  CHECK(catalog.persisted.list->selection_index == -1);
  CHECK(!catalog.persisted.selected.is_valid());
  ExpectChromeOpenDeleteDisabled(catalog);
  // Open without a new selection must not load any child window.
  int loaded_before = 0;
  for (auto const& row : catalog.persisted.list->rows) {
    auto* h = AsHolder(row);
    if (h->window.is_loaded()) {
      ++loaded_before;
    }
  }
  catalog.OpenSelected();
  int loaded_after = 0;
  for (auto const& row : catalog.persisted.list->rows) {
    auto* h = AsHolder(row);
    if (h->window.is_loaded()) {
      ++loaded_after;
    }
  }
  CHECK(loaded_after == loaded_before);
}

void TestAddedHolderReplayPreservesInitialLinks() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildHeadlessWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  PrepareWindowCatalogForRun(*catalog);
  catalog->AddWindow();

  auto row = catalog->persisted.list->rows.back();
  WindowHolder& holder = *AsHolder(row);
  ae::ObjId const title_id = holder.primary_text.id();
  ae::ObjId const window_id = holder.window.id();
  ae::ObjId const presenter_id = CatalogWindowPresented(*holder.window).presenter.id();
  auto const window_number = holder.window_number;
  std::string const primary_text = ui::ResolvePresentedListRowPrimaryText(row, 0);
  std::string const row_text = ResolveWindowCatalogHolderListRowText(holder, *catalog, 0);
  CHECK(title_id == catalog->persisted.texts.list_row_title_format.id());
  CHECK(primary_text == "Window {0}{1}");
  CHECK(row_text == "Window 1");
  CHECK(holder.journal.empty());

  // A freshly created row must already have a complete initial history base.
  bool const replayed = holder.TryReplayFromBase();
  CHECK(replayed);
  CHECK(holder.primary_text.is_valid());
  CHECK(holder.primary_text.id() == title_id);
  CHECK(holder.window.id() == window_id);
  CHECK(holder.window.is_loaded());
  CHECK(CatalogWindowPresented(*holder.window).presenter.id() == presenter_id);
  CHECK(holder.window_number == window_number);
  CHECK(holder.journal.empty());
  CHECK(ui::ResolvePresentedListRowPrimaryText(row, 0) == primary_text);
  CHECK(ResolveWindowCatalogHolderListRowText(holder, *catalog, 0) == row_text);
}

void TestMobileRingLoadsLazyListAndSkipsClosedWindows() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  WindowCatalogGraphOptions options;
  options.mobile_pager = true;
  auto catalog = BuildHeadlessWindowCatalogGraph(domain, options);
  FinalizeDistilledGraph(*catalog);
  PrepareWindowCatalogForRun(*catalog);
  catalog->AddWindow();
  catalog->AddWindow();
  catalog->AddWindow();
  auto a = WindowHolder::ptr::MakeFromThis(AsHolder(catalog->persisted.list->rows[0]));
  auto b = WindowHolder::ptr::MakeFromThis(AsHolder(catalog->persisted.list->rows[1]));
  auto c = WindowHolder::ptr::MakeFromThis(AsHolder(catalog->persisted.list->rows[2]));
  ae::ObjId const main_id = catalog->persisted.main_window.id();
  ae::ObjId const a_id = a->window.id();
  ae::ObjId const b_id = b->window.id();
  ae::ObjId const c_id = c->window.id();
  catalog->CloseHolderWindow(*b);
  CHECK(!b->window.is_loaded());

  auto ring_ids = [&] {
    std::vector<ae::ObjId> ids;
    for (auto const& window : catalog->BuildMobileRing()) {
      ids.push_back(window.id());
    }
    return ids;
  };
  std::vector<ae::ObjId> const expected{main_id, a_id, c_id};
  CHECK(ring_ids() == expected);
  catalog->persisted.list.Save();
  catalog->persisted.list.Reset();
  CHECK(catalog->persisted.list.is_valid());
  CHECK(!catalog->persisted.list.is_loaded());
  CHECK(ring_ids() == expected);
  CHECK(catalog->persisted.list.is_loaded());
  CHECK(!b->window.is_loaded());

  CHECK(catalog->MobileRingPredecessor(*a->window).id() == main_id);
  CHECK(catalog->MobileRingPredecessor(*c->window).id() == a_id);
  CHECK(catalog->MobileRingPredecessor(*catalog->persisted.main_window).id() == c_id);
  catalog->SetMobileActiveWindow(catalog->persisted.main_window);
  catalog->SubmitMobileSwipeFromUi(1);
  CHECK(catalog->ResolveMobileActiveWindow().id() == a_id);
  catalog->SubmitMobileSwipeFromUi(1);
  CHECK(catalog->ResolveMobileActiveWindow().id() == c_id);
  catalog->SubmitMobileSwipeFromUi(1);
  CHECK(catalog->ResolveMobileActiveWindow().id() == main_id);
  catalog->SubmitMobileActiveFromUi(b_id);
  CHECK(catalog->ResolveMobileActiveWindow().id() == main_id);

  // Native ordering can still contain a closed window or duplicate/stale IDs.
  catalog->SetDesktopZOrderFromObjIds(
      {c_id.id(), b_id.id(), 0, a_id.id(), c_id.id(), main_id.id()});
  auto const& order = catalog->persisted.desktop_z_order;
  CHECK(order.size() == 3);
  CHECK(order[0].id() == c_id);
  CHECK(order[1].id() == a_id);
  CHECK(order[2].id() == main_id);
  CHECK(!b->window.is_loaded());
}

void TestQueuedCatalogActionsKeepRowIdentity() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildHeadlessWindowCatalogGraph(domain);
  PrepareWindowCatalogForRun(*catalog);
  catalog->AddWindow();
  catalog->AddWindow();
  catalog->AddWindow();
  auto a = WindowHolder::ptr::MakeFromThis(AsHolder(catalog->persisted.list->rows[0]));
  auto b = WindowHolder::ptr::MakeFromThis(AsHolder(catalog->persisted.list->rows[1]));
  auto c = WindowHolder::ptr::MakeFromThis(AsHolder(catalog->persisted.list->rows[2]));
  catalog->UserCloseWindow(a->window.id());
  catalog->UserCloseWindow(b->window.id());
  catalog->UserCloseWindow(c->window.id());
  std::deque<ModelObjectProxy::ModelWork> pending;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) {
    pending.push_back(std::move(work));
  }};
  ByteSink initial;
  SerializeInitialPublication(*catalog, initial);
  ApplicationUiSession session{initial.bytes, nullptr, proxy};
  auto& mirror = static_cast<WindowCatalog&>(session.root());
  auto& presenter = ListPresenter(mirror);
  auto drain = [&] {
    while (!pending.empty()) {
      auto work = std::move(pending.front());
      pending.pop_front();
      work(domain);
    }
  };

  presenter.SubmitListRowSelectionChangedFromUi(1);
  presenter.SubmitListRowActivatedFromUi(1);
  CHECK(pending.size() == 2);
  catalog->SelectHolder(a);
  catalog->DeleteSelected();  // B moves from index 1 to 0; mirror still shows A,B,C.
  drain();
  CHECK(catalog->persisted.selected.id() == b.id());
  CHECK(catalog->persisted.list->selection_index == 0);
  CHECK(catalog->IsHolderWindowOpen(*b));
  CHECK(!catalog->IsHolderWindowOpen(*c));
  CHECK(mirror.persisted.list->rows.size() == 3);

  presenter.SubmitListRowSelectionChangedFromUi(2);
  presenter.SubmitListRowActivatedFromUi(2);
  catalog->SelectHolder(c);
  catalog->DeleteSelected();
  catalog->SelectHolder(b);
  auto const generation = catalog->Generation();
  drain();  // C is still retained by this test/history, but no longer a list member.
  CHECK(catalog->Generation() == generation);
  CHECK(catalog->persisted.selected.id() == b.id());
  CHECK(catalog->persisted.list->rows.size() == 1);
  CHECK(!catalog->IsHolderWindowOpen(*c));

  presenter.SubmitListRowSelectionChangedFromUi(99);
  presenter.SubmitListRowActivatedFromUi(99);
  CHECK(pending.empty());
}

void TestHeadlessDeleteClearsSelectionIndexRam() {

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildHeadlessWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);

  // A, B; close B; select A; delete A -> only B, no selection.
  RunDeleteMatrixScenario(domain, *catalog, 2, 1, 0);

  // Three rows: close middle, delete first -> two remain, no selection.
  catalog->AddWindow();
  catalog->AddWindow();
  catalog->persisted.list.Load();
  CHECK(catalog->persisted.list->rows.size() == 3);
  WindowHolder* r0 = AsHolder(catalog->persisted.list->rows[0]);
  WindowHolder* r1 = AsHolder(catalog->persisted.list->rows[1]);
  WindowHolder* r2 = AsHolder(catalog->persisted.list->rows[2]);
  r0->window.Load();
  r1->window.Load();
  r2->window.Load();
  catalog->UserCloseWindow(r1->window.id());
  CHECK(!r1->window.is_loaded());
  catalog->SubmitSelectionFromUi(0);
  catalog->DeleteSelected();
  catalog->persisted.list.Load();
  CHECK(catalog->persisted.list->rows.size() == 2);
  CHECK(catalog->persisted.list->selection_index == -1);
  CHECK(!catalog->persisted.selected.is_valid());
  ExpectChromeOpenDeleteDisabled(*catalog);

  // Two rows: select last, delete -> one row, no selection.
  catalog->SubmitSelectionFromUi(1);
  catalog->DeleteSelected();
  catalog->persisted.list.Load();
  CHECK(catalog->persisted.list->rows.size() == 1);
  CHECK(catalog->persisted.list->selection_index == -1);
  CHECK(!catalog->persisted.selected.is_valid());
  ExpectChromeOpenDeleteDisabled(*catalog);

  // One row left: select and delete -> empty list.
  catalog->SubmitSelectionFromUi(0);
  catalog->DeleteSelected();
  catalog->persisted.list.Load();
  CHECK(catalog->persisted.list->rows.empty());
  CHECK(catalog->persisted.list->selection_index == -1);
  CHECK(!catalog->persisted.selected.is_valid());
  ExpectChromeOpenDeleteDisabled(*catalog);
}

void TestHeadlessDeleteSelectionAbPersistAndColdLoad() {

  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  auto const profile_dir = TestDir("window_catalog_headless_delete_ab");

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
  ae::ObjId holder_b_id{};
  ae::ObjId window_b_id{};
  int session_result = 1;

  ModelObjectProxy model_proxy{[&runtime](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  }};

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.persist = [&](WindowCatalog& catalog) -> int {
    catalog.persisted.list.Load();
    CHECK(catalog.persisted.list->rows.size() == 1);
    CHECK(catalog.persisted.list->selection_index == -1);
    CHECK(!catalog.persisted.selected.is_valid());
    WindowHolder* b = AsHolder(catalog.persisted.list->rows[0]);
    CHECK(b->obj_id == holder_b_id);
    CHECK(b->window.id() == window_b_id);
    CHECK(!b->window.is_loaded());
    ShutdownSaveCatalog(catalog, model_storage);
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

  auto apply_incremental = [&] {
    WaitPublication(waiter);
    CHECK(waiter.kind == ApplicationPublicationKind::Incremental);
    ui_session->ApplyStructural(waiter.bytes);
  };

  ChromeButtonPresenter(ui_catalog, ui_catalog.persisted.presenters.add_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ChromeButtonPresenter(ui_catalog, ui_catalog.persisted.presenters.add_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.persisted.list.Load();
  CHECK(ui_catalog.persisted.list->rows.size() == 2);
  WindowHolder* holder_a = AsHolder(ui_catalog.persisted.list->rows[0]);
  WindowHolder* holder_b = AsHolder(ui_catalog.persisted.list->rows[1]);
  holder_b->window.Load();
  ae::ObjId const window_b = holder_b->window.id();
  model_proxy.Invoke(ui_catalog.obj_id, &WindowCatalog::UserCloseWindow, window_b);
  apply_incremental();
  CHECK(!holder_b->window.is_loaded());
  CHECK(holder_b->window.is_valid());

  ListPresenter(ui_catalog).SubmitListRowSelectionChangedFromUi(0);
  apply_incremental();
  ExpectChromeOpenDeleteEnabled(ui_catalog);

  ChromeButtonPresenter(ui_catalog, ui_catalog.persisted.presenters.delete_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();

  holder_b_id = holder_b->obj_id;
  window_b_id = holder_b->window.id();
  ExpectNoSelectionOneRow(ui_catalog, holder_b_id, window_b_id);
  int const loaded_after_delete = CountLoadedCatalogChildWindows(ui_catalog);
  // Disabled Open: presenter returns without posting model work (no publication).
  ChromeButtonPresenter(ui_catalog,
                        ui_catalog.persisted.presenters.open_button_presenter)
      .NotifyClickedFromUi();
  CHECK(CountLoadedCatalogChildWindows(ui_catalog) == loaded_after_delete);
  WindowHolder* after_noop_open = AsHolder(ui_catalog.persisted.list->rows[0]);
  CHECK(!after_noop_open->window.is_loaded());

  runtime.RequestStop();
  model_thread.join();
  ui_session.reset();
  CHECK(session_result == 0);

  ApplicationModelRuntime runtime_cold;
  PublicationWaiter waiter_cold;
  std::unique_ptr<ApplicationUiSession> ui_session_cold;
  int cold_session_result = 1;
  ModelObjectProxy model_proxy_cold{[&runtime_cold](ModelObjectProxy::ModelWork work) {
    runtime_cold.Post(std::move(work));
  }};
  ApplicationModelSessionHooks<WindowCatalog> hooks_cold;
  hooks_cold.prepare = PrepareWindowCatalogForRun;
  hooks_cold.persist = [](WindowCatalog&) -> int { return 0; };

  std::thread cold_thread{[&] {
    cold_session_result = RunApplicationModelSession(
        model_storage, catalog_id, runtime_cold,
        [&](ApplicationPublicationKind kind) {
          std::vector<std::uint8_t> bytes = runtime_cold.TakePublishedCopy();
          runtime_cold.NotifyPublicationConsumed();
          {
            std::lock_guard<std::mutex> lock{waiter_cold.mu};
            waiter_cold.kind = kind;
            waiter_cold.bytes = std::move(bytes);
            ++waiter_cold.pending;
          }
          waiter_cold.cv.notify_one();
        },
        hooks_cold);
  }};

  WaitPublication(waiter_cold);
  CHECK(waiter_cold.kind == ApplicationPublicationKind::Initial);
  ui_session_cold = std::make_unique<ApplicationUiSession>(waiter_cold.bytes,
                                                           nullptr,
                                                           model_proxy_cold);
  WindowCatalog& cold_ui =
      static_cast<WindowCatalog&>(ui_session_cold->root());

  auto apply_cold_incremental = [&] {
    WaitPublication(waiter_cold);
    CHECK(waiter_cold.kind == ApplicationPublicationKind::Incremental);
    ui_session_cold->ApplyStructural(waiter_cold.bytes);
  };

  cold_ui.persisted.list.Load();
  CHECK(cold_ui.persisted.list->rows.size() == 1);
  ExpectNoSelectionOneRow(cold_ui, holder_b_id, window_b_id);
  int const cold_loaded_before_open =
      CountLoadedCatalogChildWindows(cold_ui);
  ChromeButtonPresenter(cold_ui,
                        cold_ui.persisted.presenters.open_button_presenter)
      .NotifyClickedFromUi();
  CHECK(CountLoadedCatalogChildWindows(cold_ui) == cold_loaded_before_open);

  ListPresenter(cold_ui).SubmitListRowSelectionChangedFromUi(0);
  apply_cold_incremental();
  ExpectChromeOpenDeleteEnabled(cold_ui);

  ChromeButtonPresenter(cold_ui,
                        cold_ui.persisted.presenters.open_button_presenter)
      .NotifyClickedFromUi();
  apply_cold_incremental();
  cold_ui.persisted.list.Load();
  WindowHolder* opened_b = AsHolder(cold_ui.persisted.list->rows[0]);
  CHECK(opened_b->window.is_loaded());
  CHECK(opened_b->obj_id == holder_b_id);
  CHECK(opened_b->window.id() == window_b_id);

  runtime_cold.RequestStop();
  cold_thread.join();
  ui_session_cold.reset();
  CHECK(cold_session_result == 0);
}

ui::UiPresentedWindow& MainWindowPresented(WindowCatalog& catalog) {
  catalog.persisted.main_window.Load();
  return static_cast<ui::UiPresentedWindow&>(*catalog.persisted.main_window);
}

struct LocalizationSlotIds {
  ae::ObjId en;
  ae::ObjId ru;
};

LocalizationSlotIds CaptureLocalizationSlotIds(
    ui::UiLocalizationString const& localized) {
  LocalizationSlotIds ids{};
  CHECK(localized.strings.size() >= 2);
  ids.en = localized.strings[0].id();
  ids.ru = localized.strings[1].id();
  return ids;
}

void ExpectActiveSlotOnly(ui::UiLocalizationString const& localized,
                          int active_index, LocalizationSlotIds const& ids) {
  int const inactive_index = active_index == 0 ? 1 : 0;
  CHECK(localized.strings[static_cast<std::size_t>(active_index)].is_loaded());
  CHECK(localized.strings[static_cast<std::size_t>(inactive_index)].is_valid());
  CHECK(!localized.strings[static_cast<std::size_t>(inactive_index)]
             .is_loaded());
  CHECK(localized.strings[0].id() == ids.en);
  CHECK(localized.strings[1].id() == ids.ru);
}

struct CatalogLocalizationExpectation {
  int language_index;
  char const* label_add;
  char const* label_open;
  char const* label_delete;
  char const* label_language;
  char const* main_title;
  char const* row1;
  char const* row2;
  char const* open_child_title;
};

CatalogLocalizationExpectation const kCatalogEn{
    0, "Add", "Open", "Delete", "RU", "Window Catalog", "Window 1",
    "Window 2 (Closed)", "Window 1"};
CatalogLocalizationExpectation const kCatalogRu{
    1,     "\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD0\xB8\xD1\x82\xD1\x8C",
    "\xD0\x9E\xD1\x82\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD1\x8C",
    "\xD0\xA3\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xB8\xD1\x82\xD1\x8C", "EN",
    "\xD0\x9A\xD0\xB0\xD1\x82\xD0\xB0\xD0\xBB\xD0\xBE\xD0\xB3 \xD0\xBE\xD0\xBA\xD0\xBE\xD0\xBD",
    "\xD0\x9E\xD0\xBA\xD0\xBD\xD0\xBE 1",
    "\xD0\x9E\xD0\xBA\xD0\xBD\xD0\xBE 2 (\xD0\xB7\xD0\xB0\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82\xD0\xBE)",
    "\xD0\x9E\xD0\xBA\xD0\xBD\xD0\xBE 1"};

void ExpectModelLocalization(WindowCatalog& catalog,
                             CatalogLocalizationExpectation const& expect,
                             LocalizationSlotIds const& label_add_ids,
                             LocalizationSlotIds const& label_open_ids,
                             LocalizationSlotIds const& label_delete_ids,
                             LocalizationSlotIds const& label_language_ids,
                             LocalizationSlotIds const& main_title_ids,
                             LocalizationSlotIds const& open_row_ids,
                             LocalizationSlotIds const& closed_row_ids) {
  catalog.persisted.locale.Load();
  CHECK(catalog.persisted.locale->language_index() == expect.language_index);
  catalog.persisted.texts.label_add.Load();
  catalog.persisted.texts.label_open.Load();
  catalog.persisted.texts.label_delete.Load();
  catalog.persisted.texts.label_language.Load();
  CHECK(catalog.persisted.texts.label_add->Resolve(expect.language_index) ==
        expect.label_add);
  CHECK(catalog.persisted.texts.label_open->Resolve(expect.language_index) ==
        expect.label_open);
  CHECK(catalog.persisted.texts.label_delete->Resolve(expect.language_index) ==
        expect.label_delete);
  CHECK(catalog.persisted.texts.label_language->Resolve(expect.language_index) ==
        expect.label_language);
  ExpectActiveSlotOnly(*catalog.persisted.texts.label_add, expect.language_index,
                       label_add_ids);
  ExpectActiveSlotOnly(*catalog.persisted.texts.label_open, expect.language_index,
                       label_open_ids);
  ExpectActiveSlotOnly(*catalog.persisted.texts.label_delete, expect.language_index,
                       label_delete_ids);
  ExpectActiveSlotOnly(*catalog.persisted.texts.label_language, expect.language_index,
                       label_language_ids);

  MainWindowPresented(catalog).presenter.Load();
  MainWindowPresented(catalog).presenter->title.Load();
  CHECK(MainWindowPresented(catalog).presenter->title->Resolve(
            expect.language_index) == expect.main_title);
  ExpectActiveSlotOnly(*MainWindowPresented(catalog).presenter->title,
                       expect.language_index, main_title_ids);

  catalog.persisted.list.Load();
  CHECK(catalog.persisted.list->rows.size() >= 2);
  WindowHolder* row0 = AsHolder(catalog.persisted.list->rows[0]);
  WindowHolder* row1 = AsHolder(catalog.persisted.list->rows[1]);
  CHECK(row0->primary_text.id() ==
        catalog.persisted.texts.list_row_title_format.id());
  CHECK(row1->primary_text.id() ==
        catalog.persisted.texts.list_row_title_format.id());
  CHECK(ResolveWindowCatalogHolderListRowText(*row0, catalog, expect.language_index) ==
        expect.row1);
  CHECK(ResolveWindowCatalogHolderListRowText(*row1, catalog, expect.language_index) ==
        expect.row2);

  row0->window.Load();
  CHECK(ResolveWindowCatalogChildWindowTitle(catalog, row0->window.id(),
                                             expect.language_index) ==
        expect.open_child_title);

  CHECK(row1->window.is_valid());
  CHECK(!row1->window.is_loaded());
  CHECK(!catalog.persisted.window_prefab.is_loaded());
}

void ExpectUiMirrorLocalization(WindowCatalog& ui_catalog,
                                CatalogLocalizationExpectation const& expect) {
  ui_catalog.persisted.locale.Load();
  CHECK(ui_catalog.persisted.locale->language_index() == expect.language_index);
  ui_catalog.persisted.texts.label_add.Load();
  ui_catalog.persisted.texts.label_open.Load();
  ui_catalog.persisted.texts.label_delete.Load();
  ui_catalog.persisted.texts.label_language.Load();
  CHECK(ui_catalog.persisted.texts.label_add->Resolve(expect.language_index) ==
        expect.label_add);
  CHECK(ui_catalog.persisted.texts.label_open->Resolve(expect.language_index) ==
        expect.label_open);
  CHECK(ui_catalog.persisted.texts.label_delete->Resolve(expect.language_index) ==
        expect.label_delete);
  CHECK(ui_catalog.persisted.texts.label_language->Resolve(expect.language_index) ==
        expect.label_language);

  MainWindowPresented(ui_catalog).presenter.Load();
  MainWindowPresented(ui_catalog).presenter->title.Load();
  CHECK(MainWindowPresented(ui_catalog).presenter->title->Resolve(
            expect.language_index) == expect.main_title);

  ui_catalog.persisted.list.Load();
  WindowHolder* row0 = AsHolder(ui_catalog.persisted.list->rows[0]);
  WindowHolder* row1 = AsHolder(ui_catalog.persisted.list->rows[1]);
  CHECK(ResolveWindowCatalogHolderListRowText(*row0, ui_catalog, expect.language_index) ==
        expect.row1);
  CHECK(ResolveWindowCatalogHolderListRowText(*row1, ui_catalog, expect.language_index) ==
        expect.row2);

  row0->window.Load();
  CHECK(ResolveWindowCatalogChildWindowTitle(ui_catalog, row0->window.id(),
                                             expect.language_index) ==
        expect.open_child_title);

  CHECK(row1->window.is_valid());
  CHECK(!row1->window.is_loaded());
  CHECK(!ui_catalog.persisted.window_prefab.is_loaded());
}

void TestHeadlessCatalogChromeE2e() {

  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  auto const profile_dir = TestDir("window_catalog_headless_e2e");

  LocalizationSlotIds label_add_ids;
  LocalizationSlotIds label_open_ids;
  LocalizationSlotIds label_delete_ids;
  LocalizationSlotIds label_language_ids;
  LocalizationSlotIds main_title_ids;
  LocalizationSlotIds open_row_ids;
  LocalizationSlotIds closed_row_ids;

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
  ae::ObjId saved_selected_holder_id;
  int session_result = 1;
  CatalogLocalizationExpectation const* expect_model_localization = nullptr;

  ModelObjectProxy model_proxy{[&runtime](ModelObjectProxy::ModelWork work) {
    runtime.Post(std::move(work));
  }};

  ApplicationModelSessionHooks<WindowCatalog> hooks;
  hooks.prepare = PrepareWindowCatalogForRun;
  hooks.before_incremental = [&](WindowCatalog& catalog) {
    if (expect_model_localization == nullptr) {
      return;
    }
    ExpectModelLocalization(catalog, *expect_model_localization, label_add_ids,
                            label_open_ids, label_delete_ids,
                            label_language_ids, main_title_ids, open_row_ids,
                            closed_row_ids);
    expect_model_localization = nullptr;
  };
  hooks.persist = [&](WindowCatalog& catalog) -> int {
    catalog.persisted.list.Load();
    CHECK(catalog.persisted.list->selection_index == 1);
    CHECK(catalog.persisted.selected.is_valid());
    CHECK(catalog.persisted.selected.id() == catalog.persisted.list->rows[1].id());
    WindowHolder* row0 = AsHolder(catalog.persisted.list->rows[0]);
    WindowHolder* row1 = AsHolder(catalog.persisted.list->rows[1]);
    row0->window.Load();
    if (row0->window->origin_id() != kWindowPrefabId) {
      return 1;
    }
    CHECK(!row1->window.is_loaded());
    CHECK(row1->window.is_valid());
    catalog.persisted.locale.Load();
    CHECK(catalog.persisted.locale->language_index() == 0);
    model_storage.BeginSaveSession();
    SaveWindowCatalogPersistentSnapshot(catalog);
    if (model_storage.SaveSessionHadFailure()) {
      return 1;
    }
    CHECK(row0->primary_text.id() ==
          catalog.persisted.texts.list_row_title_format.id());
    CHECK(row1->primary_text.id() ==
          catalog.persisted.texts.list_row_title_format.id());
    CHECK(!row1->window.is_loaded());
    saved_holder_id = row1->obj_id;
    saved_window_id = row1->window.id();
    saved_selected_holder_id = catalog.persisted.selected.id();
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
  CHECK(!ui_catalog.persisted.window_prefab.is_loaded());
  ui_catalog.persisted.list.Load();
  CHECK(ui_catalog.persisted.list->selection_index == -1);
  ui_catalog.persisted.buttons.open_button.Load();
  ui_catalog.persisted.buttons.delete_button.Load();
  CHECK(!ui_catalog.persisted.buttons.open_button->enabled());
  CHECK(!ui_catalog.persisted.buttons.delete_button->enabled());
  ui_catalog.persisted.texts.label_add.Load();
  ui_catalog.persisted.texts.label_open.Load();
  ui_catalog.persisted.texts.label_delete.Load();
  ui_catalog.persisted.texts.label_language.Load();
  ui_catalog.persisted.texts.list_row_title_format.Load();
  label_add_ids = CaptureLocalizationSlotIds(*ui_catalog.persisted.texts.label_add);
  label_open_ids = CaptureLocalizationSlotIds(*ui_catalog.persisted.texts.label_open);
  label_delete_ids =
      CaptureLocalizationSlotIds(*ui_catalog.persisted.texts.label_delete);
  label_language_ids =
      CaptureLocalizationSlotIds(*ui_catalog.persisted.texts.label_language);
  MainWindowPresented(ui_catalog).presenter->title.Load();
  main_title_ids = CaptureLocalizationSlotIds(
      *MainWindowPresented(ui_catalog).presenter->title);
  auto apply_incremental = [&] {
    WaitPublication(waiter);
    CHECK(waiter.kind == ApplicationPublicationKind::Incremental);
    ui_session->ApplyStructural(waiter.bytes);
  };

  ChromeButtonPresenter(ui_catalog, ui_catalog.persisted.presenters.add_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.persisted.list.Load();
  CHECK(ui_catalog.persisted.list->rows.size() == 1);
  WindowHolder* holder0 = AsHolder(ui_catalog.persisted.list->rows[0]);
  holder0->window.Load();
  ae::ObjId const window0_id = holder0->window.id();
  ae::ObjId const presenter0_id =
      CatalogWindowPresenter(*holder0->window).obj_id;
  CHECK(window0_id != kWindowPrefabId);
  CHECK(presenter0_id != kWindowPrefabPresenterId);
  CHECK(CatalogWindowPresenter(*holder0->window).window.id() == window0_id);

  ChromeButtonPresenter(ui_catalog, ui_catalog.persisted.presenters.add_button_presenter)
      .NotifyClickedFromUi();
  apply_incremental();
  ui_catalog.persisted.list.Load();
  CHECK(ui_catalog.persisted.list->rows.size() == 2);
  WindowHolder* holder1 = AsHolder(ui_catalog.persisted.list->rows[1]);
  holder1->window.Load();
  ae::ObjId const window1_id = holder1->window.id();
  CHECK(window1_id != window0_id);
  CHECK(window1_id != kWindowPrefabId);
  CHECK(CatalogWindowPresenter(*holder1->window).obj_id !=
        kWindowPrefabPresenterId);
  CHECK(CatalogWindowPresenter(*holder1->window).obj_id != presenter0_id);
  CHECK(CatalogWindowPresenter(*holder1->window).window.id() ==
        holder1->window.id());
  ui_catalog.persisted.texts.list_row_title_format.Load();
  open_row_ids =
      CaptureLocalizationSlotIds(*ui_catalog.persisted.texts.list_row_title_format);
  closed_row_ids = open_row_ids;

  model_proxy.Invoke(ui_catalog.obj_id, &WindowCatalog::UserCloseWindow,
                     window1_id);
  apply_incremental();
  CHECK(holder0->window.is_loaded());
  CHECK(CatalogWindowPresenter(*holder0->window).presentation_loaded);
  CHECK(holder1->window.is_valid());
  CHECK(!holder1->window.is_loaded());

  ExpectUiMirrorLocalization(ui_catalog, kCatalogEn);
  ExpectActiveSlotOnly(*ui_catalog.persisted.texts.label_add, 0, label_add_ids);

  auto toggle_language = [&](CatalogLocalizationExpectation const& next,
                             CatalogLocalizationExpectation const& stale) {
    ExpectUiMirrorLocalization(ui_catalog, stale);
    expect_model_localization = &next;
    ChromeButtonPresenter(ui_catalog,
                          ui_catalog.persisted.presenters.language_button_presenter)
        .NotifyClickedFromUi();
    WaitPublication(waiter);
    CHECK(waiter.kind == ApplicationPublicationKind::Incremental);
    ExpectUiMirrorLocalization(ui_catalog, stale);
    ui_session->ApplyStructural(waiter.bytes);
    ExpectUiMirrorLocalization(ui_catalog, next);
    ui_catalog.persisted.texts.label_add.Load();
    ui_catalog.persisted.texts.label_open.Load();
    ui_catalog.persisted.texts.label_language.Load();
    ExpectActiveSlotOnly(*ui_catalog.persisted.texts.label_add, next.language_index,
                         label_add_ids);
    ExpectActiveSlotOnly(*ui_catalog.persisted.texts.label_open, next.language_index,
                         label_open_ids);
    ExpectActiveSlotOnly(*ui_catalog.persisted.texts.label_language, next.language_index,
                         label_language_ids);
    CHECK(holder1->window.is_valid());
    CHECK(!holder1->window.is_loaded());
    CHECK(!ui_catalog.persisted.window_prefab.is_loaded());
  };

  toggle_language(kCatalogRu, kCatalogEn);
  toggle_language(kCatalogEn, kCatalogRu);

  ListPresenter(ui_catalog).SubmitListRowSelectionChangedFromUi(1);
  apply_incremental();
  ui_catalog.persisted.list.Load();
  CHECK(ui_catalog.persisted.list->selection_index == 1);
  CHECK(ui_catalog.persisted.selected.is_valid());
  CHECK(ui_catalog.persisted.selected.id() == ui_catalog.persisted.list->rows[1].id());
  ui_catalog.persisted.buttons.open_button.Load();
  ui_catalog.persisted.buttons.delete_button.Load();
  CHECK(ui_catalog.persisted.buttons.open_button->enabled());
  CHECK(ui_catalog.persisted.buttons.delete_button->enabled());

  runtime.RequestStop();
  model_thread.join();
  ui_session.reset();

  CHECK(session_result == 0);
  CHECK(saved_window_id == window1_id);
  CHECK(saved_selected_holder_id == saved_holder_id);

  {
    ae::DomainQuery const schema0{catalog_id, WindowCatalog::kClassId, 0};
    ae::DomainQuery const schema1{catalog_id, WindowCatalog::kClassId, 1};
    ae::DomainQuery const schema2{catalog_id, WindowCatalog::kClassId, 2};
    CHECK(model_storage.Load(schema0).result == ae::DomainLoadResult::kLoaded);
    CHECK(model_storage.Load(schema1).result == ae::DomainLoadResult::kEmpty);
    CHECK(model_storage.Load(schema2).result == ae::DomainLoadResult::kEmpty);
  }

  ae::Domain cold_domain{model_storage};
  auto cold = LoadApplication<WindowCatalog>(cold_domain, catalog_id);
  for (auto const& row : cold->persisted.list->rows) {
    CHECK(AsHolder(row)->primary_text.is_valid());
    CHECK(AsHolder(row)->primary_text.id() ==
          cold->persisted.texts.list_row_title_format.id());
  }
  CHECK(cold->persisted.selected.is_valid());
  CHECK(cold->persisted.selected.id() == saved_selected_holder_id);
  bool const cold_selected_unloaded_at_root_load =
      !cold->persisted.selected.is_loaded();
  cold->RefreshChromeButtonEnabledState();
  cold->persisted.buttons.open_button.Load();
  cold->persisted.buttons.delete_button.Load();
  CHECK(cold->persisted.buttons.open_button->enabled());
  CHECK(cold->persisted.buttons.delete_button->enabled());
  cold->persisted.list.Load();
  CHECK(cold->persisted.list->rows.size() == 2);
  CHECK(cold->persisted.list->selection_index == 1);
  bool const cold_selected_unloaded_before_prepare =
      !cold->persisted.selected.is_loaded();
  CHECK(cold_selected_unloaded_at_root_load == cold_selected_unloaded_before_prepare);
  if (cold_selected_unloaded_before_prepare) {
    cold->OpenSelected();
    WindowHolder* sel = AsHolder(cold->persisted.list->rows[1]);
    CHECK(sel->window.is_loaded());
  }
  PrepareWindowCatalogForRun(*cold);
  CHECK(ResolveWindowCatalogHolderListRowText(
            *AsHolder(cold->persisted.list->rows[0]), *cold, 0) == "Window 1");
  CHECK(cold->persisted.list->selection_index == 1);
  CHECK(cold->persisted.selected.is_valid());
  CHECK(cold->persisted.selected.id() == saved_selected_holder_id);
  cold->persisted.buttons.open_button.Load();
  cold->persisted.buttons.delete_button.Load();
  CHECK(cold->persisted.buttons.open_button->enabled());
  CHECK(cold->persisted.buttons.delete_button->enabled());
  cold->SelectHolder({});
  CHECK(cold->persisted.list->selection_index == -1);
  CHECK(!cold->persisted.selected.is_valid());
  cold->RefreshChromeButtonEnabledState();
  CHECK(!cold->persisted.buttons.open_button->enabled());
  CHECK(!cold->persisted.buttons.delete_button->enabled());
  cold->persisted.list.Load();
  CHECK(cold->persisted.list->rows.size() == 2);
  CHECK(AsHolder(cold->persisted.list->rows[1])->obj_id == saved_holder_id);
  CHECK(AsHolder(cold->persisted.list->rows[1])->window.id() == saved_window_id);
  CHECK(cold->persisted.locale->language_index() == 0);
  AsHolder(cold->persisted.list->rows[0])->window.Load();
  CHECK(AsHolder(cold->persisted.list->rows[0])->window->origin_id() ==
        kWindowPrefabId);
  CatalogWindowPresented(*AsHolder(cold->persisted.list->rows[0])->window)
      .presenter.Load();
  CHECK(CatalogWindowPresenter(*AsHolder(cold->persisted.list->rows[0])->window)
            .origin_id() == kWindowPrefabPresenterId);
  CHECK(cold->persisted.window_prefab.is_valid());
  CHECK(!cold->persisted.window_prefab.is_loaded());
}

void TestCompleteClosePrefabDirectClone() {
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  WindowCatalogGraphOptions options;
  options.mobile_close_in_prefab = true;
  auto catalog = BuildHeadlessWindowCatalogGraph(domain, options);
  FinalizeDistilledGraph(*catalog);

  CatalogWindow::ptr copy;
  ae::ObjId const close_button_id{ToObjId(ObjId::WindowPrefabCloseButton)};
  ae::ObjId const close_presenter_id{
      ToObjId(ObjId::WindowPrefabCloseButtonPresenter)};
  {
    // Use the same cold prefab child-domain boundary as AddWindow, without
    // calling AddWindow or any application-side link repair.
    ae::Domain prefab_domain{storage};
    prefab_domain.set_parent_domain(&domain);
    ae::DomainGraph loader{&prefab_domain};
    ae::Ptr<ae::Obj> source_root;
    auto const loaded = loader.LoadRoot(kWindowPrefabId, source_root);
    CHECK(loaded.IsOk());
    CHECK(source_root);
    auto& prefab = static_cast<CatalogWindow&>(*source_root);
    prefab.close_button.Load();
    prefab.close_button_presenter.Load();
    CHECK(prefab.close_button->catalog.is_valid());
    // Source loading may materialize stored ancestor layers locally. Clone must
    // preserve their external identity and bind them to the target instance.
    CHECK(prefab.close_button->catalog.id() == catalog.id());
    CHECK(prefab.close_button->window.Load().get() == &prefab);
    CHECK(prefab.close_button_presenter->button.Load().get() ==
          prefab.close_button.Load().get());
    CHECK(prefab.close_button_presenter->window_presenter.Load().get() ==
          CatalogWindowPresented(prefab).presenter.Load().get());

    CloneOptions clone_options;
    clone_options.stable_origin = kWindowPrefabId;
    clone_options.stable_prefab_ids = {
        kWindowPrefabId, kWindowPrefabPresenterId, close_button_id,
        close_presenter_id};
    auto cloned = CloneCanonicalRoot(prefab_domain, prefab, domain, clone_options);
    CHECK(cloned);
    auto* copied = static_cast<CatalogWindow*>(cloned.root.Load().get());
    copy = CatalogWindow::ptr::MakeFromThis(copied);
    CHECK(copy.id() != kWindowPrefabId);
    CHECK(copy->close_button.id() != close_button_id);
    CHECK(copy->close_button_presenter.id() != close_presenter_id);
    CHECK(CatalogWindowPresented(*copy).presenter.id() !=
          kWindowPrefabPresenterId);
  }

  // All internal edges and ownership survive destruction of the source domain.
  // The external catalog remains the exact same instance, not another copy.
  auto check_links = [](CatalogWindow& window, WindowCatalog& expected_catalog) {
    window.close_button.Load();
    window.close_button_presenter.Load();
    auto const button = window.close_button.Load();
    auto const presenter = window.close_button_presenter.Load();
    CHECK(window.domain == expected_catalog.domain);
    CHECK(button->domain == expected_catalog.domain);
    CHECK(presenter->domain == expected_catalog.domain);
    CHECK(button->window.Load().get() == &window);
    CHECK(button->catalog.Load().get() == &expected_catalog);
    CHECK(presenter->button.Load().get() == button.get());
    CHECK(presenter->window_presenter.Load().get() ==
          CatalogWindowPresented(window).presenter.Load().get());
    CHECK(button->label.Load().get() ==
          expected_catalog.persisted.texts.label_close.Load().get());
  };
  check_links(*copy, *catalog);
  CHECK(catalog->persisted.list->rows.empty());

  auto const saved = ae::DomainGraph{&domain}.SaveRoot(copy.Load(), copy.id());
  CHECK(saved.IsOk());
  ae::Domain cold_domain{storage};
  auto cold_catalog = LoadApplication<WindowCatalog>(cold_domain, catalog.id());
  auto cold_copy = CatalogWindow::ptr::Declare(
      ae::CreateWith{cold_domain}.with_id(copy.id()));
  CHECK(cold_copy.Load());
  check_links(*cold_copy, *cold_catalog);
  CHECK(cold_catalog->persisted.list->rows.empty());
}

void TestMobileCloseButtonAndAdaptiveLayout() {

  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  WindowCatalogGraphOptions options;
  options.mobile_pager = true;
  options.mobile_close_in_prefab = true;
  auto catalog = BuildHeadlessWindowCatalogGraph(domain, options);
  FinalizeDistilledGraph(*catalog);

  CHECK(catalog->persisted.texts.label_close.is_valid());
  catalog->AddWindow();
  CHECK(catalog->persisted.list->rows.size() == 1);
  WindowHolder* holder = AsHolder(catalog->persisted.list->rows[0]);
  CHECK(catalog->IsHolderWindowOpen(*holder));
  CHECK(holder->window.is_loaded());
  CatalogWindow& window = *holder->window;
  CHECK(window.close_button.is_valid());
  CHECK(window.close_button.is_loaded());
  CHECK(window.close_button_presenter.is_valid());
  CHECK(window.close_button_presenter.is_loaded());
  // Clone: new objects, not the prefab ones.
  CHECK(window.close_button.id() != ae::ObjId{ToObjId(ObjId::WindowPrefabCloseButton)});
  CHECK(window.close_button_presenter.id() !=
        ae::ObjId{ToObjId(ObjId::WindowPrefabCloseButtonPresenter)});
  CHECK(window.close_button->window.id() == window.obj_id);
  CHECK(window.close_button->catalog.id() == catalog->obj_id);
  CHECK(window.close_button_presenter->button.id() == window.close_button.id());
  CHECK(window.close_button_presenter->window_presenter.id() ==
        CatalogWindowPresented(window).presenter.id());
  CHECK(window.close_button->label.id() ==
        catalog->persisted.texts.label_close.id());

  // Native viewport reports must change model-space Close layout, not rely on
  // a platform presenter scaling the original 480px prefab rectangle.
  auto const original_x = window.x();
  auto const original_y = window.y();
  auto const original_restore_w = window.restore_client_width();
  auto const original_restore_h = window.restore_client_height();
  catalog->SubmitHolderChildWindowClientSizeFromUi(window.obj_id.id(), 360, 800);
  CHECK(window.close_button_presenter->layout_x == 12);
  CHECK(window.close_button_presenter->layout_y == 740);
  CHECK(window.close_button_presenter->layout_width == 336);
  CHECK(window.close_button_presenter->layout_height == 48);
  auto check_child_layout = [&](std::int32_t width, std::int32_t height,
                                std::int32_t dpi) {
    CHECK(window.client_width() == width);
    CHECK(window.client_height() == height);
    auto const rect = ComputeWindowCatalogChildCloseLayout(width, height, dpi);
    CHECK(window.close_button_presenter->layout_x == rect.x);
    CHECK(window.close_button_presenter->layout_y == rect.y);
    CHECK(window.close_button_presenter->layout_width == rect.width);
    CHECK(window.close_button_presenter->layout_height == rect.height);
    CHECK(window.x() == original_x);
    CHECK(window.y() == original_y);
    CHECK(window.restore_client_width() == original_restore_w);
    CHECK(window.restore_client_height() == original_restore_h);
    CHECK(!window.TakeFrameRequestPendingPublication());
  };
  check_child_layout(360, 800, 160);
  catalog->SubmitHolderChildWindowClientSizeFromUi(window.obj_id.id(), 1080, 2400, 480);
  check_child_layout(1080, 2400, 480);
  CHECK(window.close_button_presenter->layout_width == 1008);
  CHECK(window.close_button_presenter->layout_height == 144);
  CHECK(window.close_button_presenter->layout_y == 2220);
  catalog->SubmitHolderChildWindowClientSizeFromUi(window.obj_id.id(), 2400, 1080, 480);
  check_child_layout(2400, 1080, 480);
  CHECK(window.close_button_presenter->layout_y == 900);
  // Density changes matter even when viewport dimensions do not change.
  catalog->SubmitHolderChildWindowClientSizeFromUi(window.obj_id.id(), 2400, 1080, 160);
  check_child_layout(2400, 1080, 160);
  CHECK(window.close_button_presenter->layout_height == 48);
  auto const history_size = window.journal.size();
  catalog->SubmitHolderChildWindowClientSizeFromUi(window.obj_id.id(), 2400, 1080, 160);
  CHECK(window.journal.size() == history_size);

  // iOS reports points at dpi160 through the same member-pointer proxy route.
  // Keep this call explicit: C++ default arguments do not cross Invoke.
  ModelObjectProxy ios_proxy{[&](ModelObjectProxy::ModelWork work) {
    work(*catalog->domain);
  }};
  for (auto const& size : {std::pair{393, 759}, std::pair{759, 393}}) {
    ios_proxy.Invoke(catalog.id(),
        &WindowCatalog::SubmitHolderChildWindowClientSizeFromUi,
        window.obj_id.id(), std::int32_t{size.first},
        std::int32_t{size.second}, std::int32_t{160});
    check_child_layout(size.first, size.second, 160);
    CHECK(window.close_button_presenter->layout_height == 48);
  }
  catalog->SubmitHolderChildWindowClientSizeFromUi(window.obj_id.id(), 40, 60, 480);
  check_child_layout(40, 60, 480);
  auto const& tiny_close = *window.close_button_presenter;
  CHECK(tiny_close.layout_width > 0 && tiny_close.layout_height > 0);
  CHECK(tiny_close.layout_x >= 0 && tiny_close.layout_y >= 0);
  CHECK(tiny_close.layout_x + tiny_close.layout_width <= 40);
  CHECK(tiny_close.layout_y + tiny_close.layout_height <= 60);

  catalog->SubmitHolderChildWindowClientSizeFromUi(window.obj_id.id(), 1080, 2400, 480);
  CHECK(window.TryReplayFromBase());
  check_child_layout(1080, 2400, 480);

  // Close and reopen using the saved Close graph, without repairing its links.
  ae::ObjId const window_id = window.obj_id;
  window.close_button->HandlePushButtonClicked();
  CHECK(!holder->window.is_loaded());
  CHECK(!catalog->IsHolderWindowOpen(*holder));
  catalog->SubmitHolderChildWindowClientSizeFromUi(window_id.id(), 800, 600, 160);
  CHECK(!holder->window.is_loaded());
  catalog->OpenHolderWindow(*holder);
  CHECK(holder->window.is_loaded());
  CHECK(holder->window.id() == window_id);
  CHECK(holder->window->close_button.is_loaded());
  CHECK(holder->window->close_button->window.id() == window_id);
  CHECK(holder->window->close_button->catalog.id() == catalog->obj_id);
  CHECK(holder->window->client_width() == 1080);
  CHECK(holder->window->close_button_presenter->layout_height == 144);

  SaveWindowCatalogPersistentSnapshot(*catalog);
  ae::Domain cold_domain{storage};
  auto cold = LoadApplication<WindowCatalog>(cold_domain, catalog.id());
  PrepareWindowCatalogForRun(*cold);
  auto* cold_holder = AsHolder(cold->persisted.list->rows[0]);
  auto& cold_window = *cold_holder->window;
  CHECK(cold_window.close_button->window.id() == window_id);
  CHECK(cold_window.close_button->catalog.id() == cold.id());
  CHECK(cold_window.close_button->catalog.domain() == &cold_domain);
  CHECK(cold_window.close_button_presenter->button.id() == cold_window.close_button.id());
  CHECK(cold_window.close_button_presenter->window_presenter.id() ==
        CatalogWindowPresented(cold_window).presenter.id());
  CHECK(cold_window.client_width() == 1080);
  CHECK(cold_window.client_height() == 2400);
  CHECK(cold_window.close_button_presenter->layout_x == 36);
  CHECK(cold_window.close_button_presenter->layout_y == 2220);
  CHECK(cold_window.close_button_presenter->layout_width == 1008);
  CHECK(cold_window.close_button_presenter->layout_height == 144);
  cold_window.close_button->HandlePushButtonClicked();
  CHECK(!cold_holder->window.is_loaded());
  CHECK(catalog->IsHolderWindowOpen(*holder));

  // Adaptive main chrome: portrait, then landscape; unchanged size is a no-op.
  catalog->SubmitMainClientViewportFromUi(360, 800);
  CHECK(catalog->persisted.main_client_viewport.width == 360);
  CHECK(catalog->persisted.main_client_viewport.height == 800);
  auto const portrait = ComputeWindowCatalogMainChromeLayout(360, 800);
  CHECK(portrait.list.y < portrait.add.y);
  CHECK(portrait.add.width == 360 - 2 * 12);
  CHECK(portrait.add.height >= 48);
  auto& add_presenter = static_cast<ui::UiPushButtonPresenter&>(
      *catalog->persisted.presenters.add_button_presenter);
  CHECK(add_presenter.layout_x == portrait.add.x);
  CHECK(add_presenter.layout_y == portrait.add.y);
  CHECK(add_presenter.layout_width == portrait.add.width);
  CHECK(add_presenter.layout_height == portrait.add.height);
  auto& list_presenter = static_cast<ui::UiListContainerPresenter&>(
      *catalog->persisted.presenters.list_presenter);
  CHECK(list_presenter.layout_width == portrait.list.width);
  CHECK(list_presenter.layout_height == portrait.list.height);

  auto const landscape = ComputeWindowCatalogMainChromeLayout(800, 360, 160);
  CHECK(landscape.list.x < landscape.add.x);
  catalog->SubmitMainClientViewportFromUi(800, 360);
  CHECK(add_presenter.layout_x == landscape.add.x);
  CHECK(list_presenter.layout_width == landscape.list.width);
  CHECK(list_presenter.layout_height == landscape.list.height);

  auto const square = ComputeWindowCatalogMainChromeLayout(600, 600, 160);
  CHECK(square.list.x < square.add.x);
  catalog->SubmitMainClientViewportFromUi(600, 600);
  CHECK(list_presenter.layout_x == square.list.x);
  CHECK(add_presenter.layout_x == square.add.x);
}
}  // namespace
}  // namespace apptraverse::test

namespace apptraverse::test {
void TestQueuedCloseClicksRespectWindowLifetime() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildHeadlessWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  PrepareWindowCatalogForDesktopRun(*catalog);
  catalog->AddWindow();
  auto* holder = AsHolder(catalog->persisted.list->rows[0]);
  auto const id = holder->window.id();
  auto const button_id = holder->window->close_button.id();
  std::deque<ModelObjectProxy::ModelWork> work;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork item) {
    work.push_back(std::move(item));
  }};
  auto run_one = [&] {
    CHECK(!work.empty());
    auto item = std::move(work.front());
    work.pop_front();
    item(domain);
  };
  auto queue_click = [&] {
    auto& presenter = *holder->window->close_button_presenter;
    presenter.model_proxy = &proxy;
    presenter.NotifyClickedFromUi();
  };
  queue_click();
  queue_click();
  CHECK(work.size() == 2);
  run_one();
  CHECK(!catalog->IsHolderWindowOpen(*holder));
  CHECK(!domain.Find(id));
  CHECK(!domain.Find(button_id));
  auto const closed_generation = catalog->Generation();
  run_one();
  CHECK(catalog->Generation() == closed_generation);
  CHECK(!domain.Find(id));

  catalog->OpenHolderWindow(*holder);
  holder->window->close_button->MaterializeEnabled(false);
  queue_click();
  CHECK(work.empty());
  holder->window->close_button->MaterializeEnabled(true);
  queue_click();
  holder->window->close_button->MaterializeEnabled(false);
  run_one();  // The model also checks the current enabled state.
  CHECK(catalog->IsHolderWindowOpen(*holder));
  holder->window->close_button->MaterializeEnabled(true);

  ae::Ptr<CatalogWindow> retained = holder->window.Load();
  queue_click();
  queue_click();
  run_one();
  CHECK(domain.Find(id));
  CHECK(!catalog->IsHolderWindowOpen(*holder));
  auto const retained_generation = catalog->Generation();
  run_one();
  CHECK(catalog->Generation() == retained_generation);
  CHECK(!holder->window.is_loaded());
  retained = {};
  CHECK(!domain.Find(id));

  catalog->OpenHolderWindow(*holder);
  queue_click();
  catalog->SelectHolder(WindowHolder::ptr::MakeFromThis(holder));
  catalog->DeleteSelected();
  run_one();
  CHECK(catalog->persisted.list->rows.empty());
  CHECK(!domain.Find(id));
  CHECK(work.empty());
}

void TestQueuedNativeReportsRespectWindowMembership() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildHeadlessWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  PrepareWindowCatalogForDesktopRun(*catalog);
  catalog->AddWindow();
  auto* holder = AsHolder(catalog->persisted.list->rows[0]);
  auto const id = holder->window.id();
  std::deque<ModelObjectProxy::ModelWork> work;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork item) {
    work.push_back(std::move(item));
  }};
  auto drain = [&] {
    while (!work.empty()) {
      auto item = std::move(work.front());
      work.pop_front();
      item(domain);
    }
  };
  auto report = [&](std::int32_t x) {
    proxy.Invoke(catalog.id(), &WindowCatalog::SubmitHolderChildWindowFrameFromUi,
        id.id(), x, std::int32_t{-110}, std::int32_t{660}, std::int32_t{420}, true);
    proxy.Invoke(catalog.id(), &WindowCatalog::SubmitWindowScreenFromUi,
        id.id(), std::int32_t{-1920}, std::int32_t{-200},
        std::int32_t{1920}, std::int32_t{1080});
  };
  report(-800);
  drain();
  CHECK(holder->window->x() == -800);
  CHECK(holder->window->y() == -110);
  CHECK(holder->window->restore_client_width() == 660);
  CHECK(holder->window->close_button_presenter->layout_y ==
        ComputeWindowCatalogChildCloseLayout(660, 420, 160).y);

  ae::Ptr<CatalogWindow> retained = holder->window.Load();
  report(999);
  catalog->UserCloseWindow(id);
  CHECK(!holder->window.is_loaded());
  CHECK(domain.Find(id));  // Mere object existence is not a live-window check.
  drain();
  CHECK(retained->x() == -800);
  CHECK(retained->y() == -110);
  CHECK(!holder->window.is_loaded());
  retained = {};
  CHECK(!domain.Find(id));
  report(1000);
  drain();
  CHECK(!domain.Find(id));
  CHECK(!holder->window.is_loaded());

  // Reading a closed window's saved state must not make stale UI events live.
  holder->window.Load();
  CHECK(holder->window.is_loaded());
  CHECK(!catalog->IsHolderWindowOpen(*holder));
  report(1002);
  proxy.Invoke(catalog.id(), &WindowCatalog::SubmitHolderChildWindowClientSizeFromUi,
      id.id(), std::int32_t{900}, std::int32_t{700}, std::int32_t{160});
  drain();
  CHECK(holder->window->x() == -800);
  CHECK(holder->window->y() == -110);
  CHECK(holder->window->client_width() == 660);
  CHECK(holder->window->client_height() == 420);
  CHECK(!catalog->IsHolderWindowOpen(*holder));
  holder->window.Reset();

  catalog->OpenHolderWindow(*holder);
  report(1001);
  catalog->SelectHolder(WindowHolder::ptr::MakeFromThis(holder));
  catalog->DeleteSelected();
  drain();
  CHECK(catalog->persisted.list->rows.empty());
  CHECK(!domain.Find(id));

  // The main window uses the same stable screen route but is not a list row.
  auto const main_id = catalog->persisted.main_window.id();
  proxy.Invoke(catalog.id(), &WindowCatalog::SubmitWindowScreenFromUi,
      main_id.id(), std::int32_t{-1920}, std::int32_t{0},
      std::int32_t{3840}, std::int32_t{1080});
  drain();
  CHECK(catalog->persisted.main_window.is_loaded());
}

void TestPresentationEnvironmentPreservesPortableState() {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  WindowCatalogGraphOptions options;
  options.mobile_close_in_prefab = true;
  auto catalog = BuildHeadlessWindowCatalogGraph(domain, options);
  FinalizeDistilledGraph(*catalog);
  PrepareWindowCatalogForDesktopRun(*catalog);
  catalog->AddWindow();
  auto* first = AsHolder(catalog->persisted.list->rows[0]);
  first->window->RequestFrame(-310, -90, 720, 460);
  auto const first_id = first->window.id();
  auto& close_presenter = *first->window->close_button_presenter;
  close_presenter.window_presenter->presentation_loaded = true;
  CHECK(!close_presenter.EnabledForPresentation());
  catalog->AddWindow();
  auto* second = AsHolder(catalog->persisted.list->rows[1]);
  catalog->UserCloseWindow(second->window.id());
  CHECK(!second->window.is_loaded());

  PrepareWindowCatalogForPagedRun(*catalog);
  CHECK(close_presenter.EnabledForPresentation());
  catalog->SetMobileActiveWindow(first->window);
  catalog->SubmitHolderChildWindowClientSizeFromUi(first_id.id(), 1080, 2400, 480);
  CHECK(first->window->client_width() == 1080);
  CHECK(first->window->restore_client_width() == 720);
  CHECK(first->window->x() == -310);
  SaveWindowCatalogPersistentSnapshot(*catalog);

  ae::Domain cold_domain{storage};
  auto cold = LoadApplication<WindowCatalog>(cold_domain, catalog.id());
  PrepareWindowCatalogForDesktopRun(*cold);
  CHECK(!cold->persisted.mobile_pager);
  CHECK(cold->persisted.mobile_active_window.id() == first_id);
  auto* cold_first = AsHolder(cold->persisted.list->rows[0]);
  auto* cold_second = AsHolder(cold->persisted.list->rows[1]);
  auto& cold_close = *cold_first->window->close_button_presenter;
  cold_close.window_presenter->presentation_loaded = true;
  CHECK(!cold_close.EnabledForPresentation());
  CHECK(cold_first->window.id() == first_id);
  CHECK(cold_first->window->x() == -310);
  CHECK(cold_first->window->y() == -90);
  CHECK(cold_first->window->restore_client_width() == 720);
  CHECK(cold_first->window->restore_client_height() == 460);
  CHECK(!cold_second->window.is_loaded());
  // Native desktop client measurement reflows the shared Close graph without
  // destroying the saved restore geometry or activating a closed window.
  cold->SubmitHolderChildWindowClientSizeFromUi(first_id.id(), 720, 460, 160);
  CHECK(cold_first->window->client_width() == 720);
  auto const layout = ComputeWindowCatalogChildCloseLayout(720, 460, 160);
  CHECK(cold_first->window->close_button_presenter->layout_y == layout.y);
  CHECK(!cold_second->window.is_loaded());
  PrepareWindowCatalogForPagedRun(*cold);
  CHECK(cold_close.EnabledForPresentation());
  CHECK(cold->ResolveMobileActiveWindow().id() == first_id);
  cold->OpenHolderWindow(*cold_second);
  CHECK(cold_second->window->close_button->window.id() == cold_second->window.id());
  cold->SetMobileActiveWindow(cold_first->window);
  PrepareWindowCatalogForDesktopRun(*cold);
  cold->SetDesktopZOrderFromObjIds({first_id.id(), cold->persisted.main_window.id().id()});
  cold->UserCloseWindow(first_id);
  CHECK(!cold_first->window.is_loaded());
  CHECK(!cold_domain.Find(first_id));
  CHECK(cold->persisted.mobile_active_window.id() == cold->persisted.main_window.id());
  CHECK(cold->persisted.desktop_z_order.size() == 1);
  SaveWindowCatalogPersistentSnapshot(*cold);
  ae::Domain returned_domain{storage};
  auto returned = LoadApplication<WindowCatalog>(returned_domain, cold.id());
  PrepareWindowCatalogForPagedRun(*returned);
  CHECK(!AsHolder(returned->persisted.list->rows[0])->window.is_loaded());
  CHECK(returned->ResolveMobileActiveWindow().id() == returned->persisted.main_window.id());
  auto* returned_second = AsHolder(returned->persisted.list->rows[1]);
  auto const second_id = returned_second->window.id();
  returned->SetMobileActiveWindow(returned_second->window);
  PrepareWindowCatalogForDesktopRun(*returned);
  returned->SelectHolder(WindowHolder::ptr::MakeFromThis(returned_second));
  returned->DeleteSelected();
  CHECK(returned->persisted.list->rows.size() == 1);
  CHECK(!returned_domain.Find(second_id));
  CHECK(returned->persisted.mobile_active_window.id() == returned->persisted.main_window.id());
}
}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureHeadlessWindowCatalogGraphRegistration();
  apptraverse::test::TestQueuedCloseClicksRespectWindowLifetime();
  apptraverse::test::TestQueuedNativeReportsRespectWindowMembership();
  apptraverse::test::TestPresentationEnvironmentPreservesPortableState();
  apptraverse::test::TestIndependentSuppliesHaveStableIdentitiesAndBytes();
  apptraverse::test::TestAddedHolderReplayPreservesInitialLinks();
  apptraverse::test::TestMobileRingLoadsLazyListAndSkipsClosedWindows();
  apptraverse::test::TestQueuedCatalogActionsKeepRowIdentity();
  apptraverse::test::TestHeadlessDeleteClearsSelectionIndexRam();
  apptraverse::test::TestHeadlessDeleteSelectionAbPersistAndColdLoad();
  apptraverse::test::TestHeadlessCatalogChromeE2e();
  apptraverse::test::TestCompleteClosePrefabDirectClone();
  apptraverse::test::TestMobileCloseButtonAndAdaptiveLayout();
  std::cout << "window_catalog_headless_test OK\n";
  return 0;
}
