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

void TestQueuedCatalogActionsKeepRowIdentity() {
  EnsureObjectRegistration();
  EnsureHeadlessWindowCatalogGraphRegistration();
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
  EnsureObjectRegistration();
  EnsureHeadlessWindowCatalogGraphRegistration();

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
  EnsureObjectRegistration();
  EnsureHeadlessWindowCatalogGraphRegistration();

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
  EnsureObjectRegistration();
  EnsureHeadlessWindowCatalogGraphRegistration();

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

void TestMobileCloseButtonAndAdaptiveLayout() {
  EnsureObjectRegistration();
  EnsureHeadlessWindowCatalogGraphRegistration();

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

  // Close through the button path, then reopen: Close graph is rewired.
  ae::ObjId const window_id = window.obj_id;
  window.close_button->HandlePushButtonClicked();
  CHECK(!holder->window.is_loaded());
  CHECK(!catalog->IsHolderWindowOpen(*holder));
  catalog->OpenHolderWindow(*holder);
  CHECK(holder->window.is_loaded());
  CHECK(holder->window.id() == window_id);
  CHECK(holder->window->close_button.is_loaded());
  CHECK(holder->window->close_button->window.id() == window_id);
  CHECK(holder->window->close_button->catalog.id() == catalog->obj_id);

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

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::test::TestQueuedCatalogActionsKeepRowIdentity();
  apptraverse::test::TestHeadlessDeleteClearsSelectionIndexRam();
  apptraverse::test::TestHeadlessDeleteSelectionAbPersistAndColdLoad();
  apptraverse::test::TestHeadlessCatalogChromeE2e();
  apptraverse::test::TestMobileCloseButtonAndAdaptiveLayout();
  std::cout << "window_catalog_headless_test OK\n";
  return 0;
}
