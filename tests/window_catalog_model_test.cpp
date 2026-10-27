#include <fstream>
#include <filesystem>
#include <iostream>
#include <unordered_set>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/graph_walk.h"
#include "apptraverse/runtime_node.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_string.h"

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

std::filesystem::path TestDir(char const* name) {
  auto const dir =
      std::filesystem::temp_directory_path() / "apptraverse_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

WindowHolder* AsHolder(ui::UiListRow::ptr const& row) {
  return static_cast<WindowHolder*>(row.operator->());
}

ui::UiPresentedWindow& CatalogWindowPresented(CatalogWindow& window) {
  return static_cast<ui::UiPresentedWindow&>(window);
}

ui::UiPresentedWindow const& CatalogWindowPresented(CatalogWindow const& window) {
  return static_cast<ui::UiPresentedWindow const&>(window);
}

ui::UiPresentedWindow& MainWindowPresented(WindowCatalog& catalog) {
  return static_cast<ui::UiPresentedWindow&>(*catalog.main_window);
}

void ShutdownSaveCatalog(WindowCatalog& catalog,
                         DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  CHECK(!storage.SaveSessionHadFailure());
  CHECK(!std::filesystem::exists(
      storage.root() / "persistent_model_checkpoint.bin"));
}

void CopyStorageProfile(std::filesystem::path const& from,
                        std::filesystem::path const& to) {
  std::error_code ec;
  std::filesystem::remove_all(to, ec);
  std::filesystem::create_directories(to, ec);
  std::filesystem::copy(
      from, to,
      std::filesystem::copy_options::recursive |
          std::filesystem::copy_options::overwrite_existing,
      ec);
  CHECK(!ec);
}

WindowCatalog::ptr LoadPersistedWindowCatalog(ae::Domain& domain,
                                              ae::ObjId const& catalog_id) {
  return LoadApplication<WindowCatalog>(domain, catalog_id);
}

void ExpectReflectedGraphEdge(
    std::unordered_set<ae::ObjId::Type> const& reflected_ids,
    ae::ObjectPtrBase const& ref) {
  CHECK(ref.is_valid());
  CHECK(reflected_ids.count(ref.id().id()) == 1);
}

void TestCatalogReflectionTopologyAndInitialPublication() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto catalog = BuildWindowCatalogGraph(model_domain);
  FinalizeDistilledGraph(*catalog);

  std::unordered_set<ae::ObjId::Type> reflected_ids;
  ForEachGraphEdgeObjPtrOn(*catalog, [&](auto& edge) {
    if (!edge.is_valid()) {
      return;
    }
    reflected_ids.insert(edge.id().id());
  });

  ExpectReflectedGraphEdge(reflected_ids, catalog->main_window);
  CHECK(MainWindowPresented(*catalog).presenter.is_valid());
  CHECK(MainWindowPresented(*catalog).presenter.id() ==
        window_catalog_demo::ToObjId(
            window_catalog_demo::ObjId::MainWindowPresenter));
  ExpectReflectedGraphEdge(reflected_ids, catalog->list);
  ExpectReflectedGraphEdge(reflected_ids, catalog->locale);
  ExpectReflectedGraphEdge(reflected_ids, catalog->texts.label_add);
  ExpectReflectedGraphEdge(reflected_ids, catalog->texts.label_open);
  ExpectReflectedGraphEdge(reflected_ids, catalog->texts.label_delete);
  ExpectReflectedGraphEdge(reflected_ids, catalog->texts.label_language);
  ExpectReflectedGraphEdge(reflected_ids, catalog->texts.window_title_prefix);
  ExpectReflectedGraphEdge(reflected_ids, catalog->buttons.add_button);
  ExpectReflectedGraphEdge(reflected_ids, catalog->buttons.open_button);
  ExpectReflectedGraphEdge(reflected_ids, catalog->buttons.delete_button);
  ExpectReflectedGraphEdge(reflected_ids, catalog->buttons.language_button);
  ExpectReflectedGraphEdge(reflected_ids, catalog->presenters.list_presenter);
  ExpectReflectedGraphEdge(reflected_ids,
                           catalog->presenters.add_button_presenter);
  ExpectReflectedGraphEdge(reflected_ids,
                           catalog->presenters.open_button_presenter);
  ExpectReflectedGraphEdge(reflected_ids,
                           catalog->presenters.delete_button_presenter);
  ExpectReflectedGraphEdge(reflected_ids,
                           catalog->presenters.language_button_presenter);
  if (catalog->selected.is_valid()) {
    ExpectReflectedGraphEdge(reflected_ids, catalog->selected);
  }

  catalog->AddWindow();
  catalog->AddWindow();
  WindowHolder* closed_holder = AsHolder(catalog->list->rows[0]);
  WindowHolder* open_holder = AsHolder(catalog->list->rows[1]);
  ae::ObjId const closed_window_id = closed_holder->window.id();
  ae::ObjId const open_window_id = open_holder->window.id();
  catalog->SelectHolder(catalog->list->rows[0]);
  catalog->CloseHolderWindow(*closed_holder);
  CHECK(!closed_holder->window.is_loaded());
  CHECK(closed_holder->window.is_valid());
  CHECK(!model_domain.Find(closed_window_id));
  CHECK(open_holder->window.is_loaded());
  catalog->MaterializeCatalogStrings(0);

  ByteSink sink;
  SerializeInitialPublication(*catalog, sink);
  CHECK(!model_domain.Find(closed_window_id));

  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource in;
  in.data = sink.bytes.data();
  in.size = sink.bytes.size();
  auto ui_root = LoadInitialPublication(in, ui_domain, ui_storage);
  auto ui_catalog = WindowCatalog::ptr::MakeFromThis(
      static_cast<WindowCatalog*>(ui_root.get()));

  CHECK(MainWindowPresented(*ui_catalog).presenter.is_loaded());
  CHECK(ui_domain.Find(MainWindowPresented(*ui_catalog).presenter.id()));
  CHECK(ui_catalog->presenters.list_presenter.is_loaded());
  CHECK(ui_domain.Find(ui_catalog->presenters.list_presenter.id()));
  CHECK(ui_catalog->presenters.add_button_presenter.is_loaded());
  CHECK(ui_domain.Find(ui_catalog->presenters.add_button_presenter.id()));
  CHECK(ui_catalog->presenters.open_button_presenter.is_loaded());
  CHECK(ui_domain.Find(ui_catalog->presenters.open_button_presenter.id()));
  CHECK(ui_catalog->presenters.delete_button_presenter.is_loaded());
  CHECK(ui_domain.Find(ui_catalog->presenters.delete_button_presenter.id()));
  CHECK(ui_catalog->presenters.language_button_presenter.is_loaded());
  CHECK(ui_domain.Find(ui_catalog->presenters.language_button_presenter.id()));
  CHECK(ui_catalog->presenters.add_button_presenter.id() ==
        catalog->presenters.add_button_presenter.id());

  CHECK(!ui_domain.Find(closed_window_id));
  CHECK(!model_domain.Find(closed_window_id));
  CHECK(open_holder->window.is_loaded());
  CHECK(model_domain.Find(open_window_id));
}

void TestCatalogHolderOpenCloseAndMonotonicNumbers() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage ram_storage;
  ae::Domain domain{ram_storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);

  catalog->AddWindow();
  catalog->AddWindow();
  catalog->AddWindow();
  CHECK(catalog->list->rows.size() == 3);
  CHECK(catalog->next_window_number == 4);
  WindowHolder* row0 = AsHolder(catalog->list->rows[0]);
  WindowHolder* row1 = AsHolder(catalog->list->rows[1]);
  WindowHolder* row2 = AsHolder(catalog->list->rows[2]);
  CHECK(row0->window_number == 1);
  CHECK(row2->window_number == 3);
  CHECK(row0->window.is_loaded());
  CHECK(row2->window.is_loaded());

  ae::ObjId const middle_window_id = row1->window.id();
  catalog->CloseHolderWindow(*row1);
  CHECK(!row1->window.is_loaded());
  CHECK(row1->window.is_valid());
  CHECK(!domain.Find(middle_window_id));
  CHECK(row0->window.is_loaded());
  CHECK(row2->window.is_loaded());

  catalog->OpenHolderWindow(*row1);
  CHECK(row1->window.is_loaded());
  CHECK(row1->window.id() == middle_window_id);

  catalog->SelectHolder(catalog->list->rows[0]);
  catalog->DeleteSelected();
  catalog->SelectHolder(catalog->list->rows[0]);
  catalog->DeleteSelected();
  CHECK(catalog->list->rows.size() == 1);
  WindowHolder* holder3 = AsHolder(catalog->list->rows[0]);
  CHECK(holder3->window_number == 3);
  catalog->AddWindow();
  WindowHolder* holder4 = AsHolder(catalog->list->rows[1]);
  CHECK(holder4->window_number == 4);
  CHECK(holder4->window.is_loaded());
  CHECK(catalog->next_window_number == 5);

  auto const dir = TestDir("apptraverse_window_catalog_model_roundtrip");
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  ae::ObjId saved_window3_id{};
  ae::ObjId saved_window4_id{};

  {
    DirectoryDomainStorage persist_storage{dir};
    ae::Domain persist_domain{persist_storage};
    auto persist_catalog = BuildWindowCatalogGraph(persist_domain);
    FinalizeDistilledGraph(*persist_catalog);

    persist_catalog->AddWindow();
    persist_catalog->AddWindow();
    persist_catalog->AddWindow();
    WindowHolder* p2 = AsHolder(persist_catalog->list->rows[1]);
    ae::ObjId const p2_window_id = p2->window.id();
    persist_catalog->CloseHolderWindow(*p2);
    persist_catalog->OpenHolderWindow(*p2);
    CHECK(p2->window.id() == p2_window_id);
    persist_catalog->SelectHolder(persist_catalog->list->rows[0]);
    persist_catalog->DeleteSelected();
    persist_catalog->SelectHolder(persist_catalog->list->rows[0]);
    persist_catalog->DeleteSelected();
    WindowHolder* p_holder3 = AsHolder(persist_catalog->list->rows[0]);
    CHECK(p_holder3->window_number == 3);
    persist_catalog->AddWindow();
    saved_window3_id = p_holder3->window.id();
    persist_catalog->CloseHolderWindow(*p_holder3);
    CHECK(!persist_domain.Find(saved_window3_id));
    CHECK(persist_catalog->list->rows.size() == 2);
    WindowHolder* p_holder4 = AsHolder(persist_catalog->list->rows[1]);
    CHECK(p_holder4->window_number == 4);
    CHECK(p_holder4->window.is_loaded());
    saved_window4_id = p_holder4->window.id();
    CHECK(persist_catalog->next_window_number == 5);

    ShutdownSaveCatalog(*persist_catalog, persist_storage);
    persist_catalog.Reset();
  }

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};
  auto reloaded =
      LoadPersistedWindowCatalog(load_domain, catalog_id);
  CHECK(reloaded->list->rows.size() == 2);
  CHECK(reloaded->next_window_number == 5);

  WindowHolder* r3 = AsHolder(reloaded->list->rows[0]);
  WindowHolder* r4 = AsHolder(reloaded->list->rows[1]);
  CHECK(r3->window_number == 3);
  CHECK(r4->window_number == 4);
  CHECK(r3->primary_text.is_valid());
  CHECK(r4->primary_text.is_valid());
  CHECK(!r3->window.is_loaded());
  CHECK(r3->window.is_valid());
  CHECK(r3->window.id() == saved_window3_id);
  CHECK(!load_domain.Find(saved_window3_id));
  CHECK(r4->window.is_loaded());
  CHECK(r4->window.id() == saved_window4_id);
  CHECK(CatalogWindowPresented(*r4->window).presenter.is_loaded());
  CHECK(CatalogWindowPresented(*r4->window).presenter->title.is_valid());
  CHECK(CatalogWindowPresented(*r4->window).presenter->title.id() ==
        r4->primary_text.id());

  reloaded->OpenHolderWindow(*r3);
  CHECK(r3->window.is_loaded());
  CHECK(r3->window.id() == saved_window3_id);
  CHECK(CatalogWindowPresented(*r3->window).presenter.is_loaded());
  CHECK(CatalogWindowPresented(*r3->window).presenter->window.id() ==
        r3->window.id());
}

std::string ReadInactiveUtf8(ui::UiLocalizationString& localized,
                            std::size_t slot) {
  int const restore = 0;
  localized.MaterializeLanguage(static_cast<int>(slot));
  std::string const text = localized.Resolve(static_cast<int>(slot));
  localized.MaterializeLanguage(restore);
  return text;
}

void TestCatalogLocalizationUtf8Persisted() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  catalog->AddWindow();
  catalog->AddWindow();
  catalog->SelectHolder(catalog->list->rows[0]);

  catalog->MaterializeCatalogStrings(1);
  catalog->locale->SubmitLanguageIndexFromUi(1);
  CHECK(catalog->texts.label_add->Resolve(1) == "Добавить");
  CHECK(catalog->texts.label_open->Resolve(1) == "Открыть");
  CHECK(catalog->texts.label_delete->Resolve(1) == "Удалить");
  CHECK(catalog->texts.label_language->Resolve(1) == "EN");
  WindowHolder* h0 = AsHolder(catalog->list->rows[0]);
  CHECK(h0->primary_text->Resolve(1) == "Окно 1");

  catalog->MaterializeCatalogStrings(0);
  catalog->locale->SubmitLanguageIndexFromUi(0);
  CHECK(catalog->texts.label_add->Resolve(0) == "Add");
  CHECK(h0->primary_text->Resolve(0) == "Window 1");
  CHECK(ReadInactiveUtf8(*catalog->texts.label_add, 1) == "Добавить");
  CHECK(ReadInactiveUtf8(*h0->primary_text, 1) == "Окно 1");

  auto const dir = TestDir("apptraverse_window_catalog_localization_save");
  {
    DirectoryDomainStorage persist{dir};
    ae::Domain persist_domain{persist};
    auto persist_catalog = BuildWindowCatalogGraph(persist_domain);
    FinalizeDistilledGraph(*persist_catalog);
    persist_catalog->AddWindow();
    persist_catalog->AddWindow();
    persist_catalog->MaterializeCatalogStrings(1);
    persist_catalog->locale->SubmitLanguageIndexFromUi(1);
    ShutdownSaveCatalog(*persist_catalog, persist);
    persist_catalog.Reset();
  }
  DirectoryDomainStorage reload_storage{dir};
  ae::Domain reload_domain{reload_storage};
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  auto reloaded = LoadPersistedWindowCatalog(reload_domain, catalog_id);
  reloaded->MaterializeCatalogStrings(0);
  CHECK(reloaded->texts.label_add->Resolve(0) == "Add");
  CHECK(ReadInactiveUtf8(*reloaded->texts.label_add, 1) == "Добавить");
  WindowHolder* r0 = AsHolder(reloaded->list->rows[0]);
  CHECK(r0->primary_text->Resolve(0) == "Window 1");
  CHECK(ReadInactiveUtf8(*r0->primary_text, 1) == "Окно 1");
  CHECK(MainWindowPresented(*reloaded).presenter->title->Resolve(0) ==
        "Window Catalog");
  CHECK(ReadInactiveUtf8(*MainWindowPresented(*reloaded).presenter->title, 1) ==
        "Каталог окон");
}

void TestCatalogRuntimeSavePreservesOpenWindowLoadedState() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  auto const dir = TestDir("apptraverse_window_catalog_runtime_save");
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

  ae::ObjId window1_id{};
  ae::ObjId window2_id{};
  ae::ObjId window3_id{};
  ae::ObjId holder1_id{};
  ae::ObjId holder2_id{};
  ae::ObjId holder3_id{};

  {
    DirectoryDomainStorage storage_a{dir};
    ae::Domain domain_a{storage_a};
    auto bootstrap = BuildWindowCatalogGraph(domain_a);
    FinalizeDistilledGraph(*bootstrap);
    SaveDistilledRoot(*bootstrap);
    bootstrap.Reset();
  }

  {
    DirectoryDomainStorage storage_b{dir};
    ae::Domain domain_b{storage_b};
    auto catalog = LoadApplication<WindowCatalog>(domain_b, ae::ObjId{catalog_id});
    catalog->AddWindow();
    catalog->AddWindow();
    catalog->AddWindow();
    CHECK(catalog->list->rows.size() == 3);
    CHECK(catalog->next_window_number == 4);

    WindowHolder* h0 = AsHolder(catalog->list->rows[0]);
    WindowHolder* h1 = AsHolder(catalog->list->rows[1]);
    WindowHolder* h2 = AsHolder(catalog->list->rows[2]);
    holder1_id = h0->obj_id;
    holder2_id = h1->obj_id;
    holder3_id = h2->obj_id;
    window1_id = h0->window.id();
    window2_id = h1->window.id();
    window3_id = h2->window.id();
    CHECK(h0->window_number == 1);
    CHECK(h1->window_number == 2);
    CHECK(h2->window_number == 3);

    catalog->CloseHolderWindow(*h1);
    CHECK(h0->window.is_loaded());
    CHECK(!h1->window.is_loaded());
    CHECK(h2->window.is_loaded());
    CHECK(h1->window.is_valid());
    CHECK(!domain_b.Find(window2_id));

    std::size_t const saved_journal_size = catalog->journal.size();
    ShutdownSaveCatalog(*catalog, storage_b);
    catalog.Reset();

    DirectoryDomainStorage storage_c{dir};
    ae::Domain domain_c{storage_c};
    auto reloaded = LoadPersistedWindowCatalog(domain_c, catalog_id);

    CHECK(reloaded->base.is_valid());
    CHECK(reloaded->base.is_loaded());
    CHECK(reloaded->base.id() != reloaded->obj_id);
    CHECK(reloaded->journal.size() == saved_journal_size);
    CHECK(reloaded->list->rows.size() == 3);
    CHECK(reloaded->next_window_number == 4);
    WindowHolder* c0 = AsHolder(reloaded->list->rows[0]);
    WindowHolder* c1 = AsHolder(reloaded->list->rows[1]);
    WindowHolder* c2 = AsHolder(reloaded->list->rows[2]);
    CHECK(c0->obj_id == holder1_id);
    CHECK(c1->obj_id == holder2_id);
    CHECK(c2->obj_id == holder3_id);
    CHECK(!c1->window.is_loaded());
    CHECK(!domain_c.Find(window2_id));
    reloaded->OpenHolderWindow(*c1);
    CHECK(c1->window.is_loaded());
    CHECK(c1->window.id() == window2_id);
    if (!reloaded->buttons.add_button.is_loaded()) {
      reloaded->buttons.add_button.Load();
    }
    reloaded->buttons.add_button->NotifyClicked();
    CHECK(reloaded->list->rows.size() == 4);
    CHECK(reloaded->next_window_number == 5);
    WindowHolder* c3 = AsHolder(reloaded->list->rows[3]);
    CHECK(c3->obj_id != holder1_id);
    CHECK(c3->obj_id != holder2_id);
    CHECK(c3->obj_id != holder3_id);
    CHECK(c3->window_number == 4);
    reloaded.Reset();
  }

  DirectoryDomainStorage storage_c{dir};
  ae::Domain domain_c{storage_c};
  auto catalog = LoadPersistedWindowCatalog(domain_c, catalog_id);
  CHECK(catalog->list.is_valid());
  CHECK(catalog->list->rows.size() == 3);
  CHECK(catalog->next_window_number == 4);

  WindowHolder* r0 = AsHolder(catalog->list->rows[0]);
  WindowHolder* r1 = AsHolder(catalog->list->rows[1]);
  WindowHolder* r2 = AsHolder(catalog->list->rows[2]);
  CHECK(r0->obj_id == holder1_id);
  CHECK(r1->obj_id == holder2_id);
  CHECK(r2->obj_id == holder3_id);
  CHECK(r0->window_number == 1);
  CHECK(r1->window_number == 2);
  CHECK(r2->window_number == 3);
  CHECK(r0->window.id() == window1_id);
  CHECK(r1->window.id() == window2_id);
  CHECK(r2->window.id() == window3_id);
  CHECK(r0->window.is_loaded());
  CHECK(!r1->window.is_loaded());
  CHECK(r2->window.is_loaded());
  CHECK(r1->window.is_valid());
  CHECK(!domain_c.Find(window2_id));
  int const language = catalog->locale->language_index();
  catalog->MaterializeCatalogStrings(language);
  CHECK(r0->primary_text->Resolve(0) == "Window 1");
  CHECK(r1->primary_text->Resolve(0) == "Window 2");
  CHECK(r2->primary_text->Resolve(0) == "Window 3");
  catalog->RefreshChromeButtonEnabledState();
  RestoreLoadedCatalogWindows(*catalog);
  CHECK(catalog->list->rows.size() == 3);
  ByteSink pub;
  SerializeInitialPublication(*catalog, pub);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource pub_in;
  pub_in.data = pub.bytes.data();
  pub_in.size = pub.bytes.size();
  auto ui_root = LoadInitialPublication(pub_in, ui_domain, ui_storage);
  auto ui_catalog = WindowCatalog::ptr::MakeFromThis(
      static_cast<WindowCatalog*>(ui_root.get()));
  CHECK(ui_catalog->list.is_valid());
  CHECK(ui_catalog->list->rows.size() == 3);
  int const ui_language =
      ui_catalog->locale ? ui_catalog->locale->language_index() : 0;
  for (std::size_t i = 0; i < ui_catalog->list->rows.size(); ++i) {
    auto* ui_holder =
        AsHolder(ui_catalog->list->rows[i]);
    CHECK(ui_holder->primary_text.is_valid());
    if (!ui_holder->primary_text.is_loaded()) {
      ui_holder->primary_text.Load();
    }
    std::string const title = ui_holder->primary_text->Resolve(ui_language);
    CHECK(title == ("Window " + std::to_string(ui_holder->window_number)));
  }
}

void TestCatalogSaveAfterLanguageToggleAndChildClose() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  auto const dir = TestDir("apptraverse_window_catalog_save_lang_close");
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

  {
    DirectoryDomainStorage storage{dir};
    ae::Domain domain{storage};
    auto catalog = BuildWindowCatalogGraph(domain);
    FinalizeDistilledGraph(*catalog);
    catalog->AddWindow();
    catalog->AddWindow();
    catalog->AddWindow();
    WindowHolder* row1 = AsHolder(catalog->list->rows[1]);
    catalog->CloseHolderWindow(*row1);
    catalog->OpenHolderWindow(*row1);
    catalog->MaterializeCatalogStrings(1);
    catalog->MaterializeCatalogStrings(0);
    catalog->CloseHolderWindow(*AsHolder(catalog->list->rows[2]));
    ShutdownSaveCatalog(*catalog, storage);
    catalog.Reset();
  }

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};
  auto catalog =
      LoadPersistedWindowCatalog(load_domain, catalog_id);
  CHECK(catalog->list->rows.size() == 3);
  CHECK(AsHolder(catalog->list->rows[0])->window.is_loaded());
  CHECK(AsHolder(catalog->list->rows[1])->window.is_loaded());
  CHECK(!AsHolder(catalog->list->rows[2])->window.is_loaded());
}

void TestOpenLoadedWindowSetsPendingFocusRequest() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage model_storage;
  ae::Domain model_domain{model_storage};
  auto catalog = BuildWindowCatalogGraph(model_domain);
  FinalizeDistilledGraph(*catalog);
  catalog->AddWindow();
  WindowHolder* holder = AsHolder(catalog->list->rows[0]);
  ae::ObjId const window_id = holder->window.id();
  catalog->pending_focus_window = {};
  catalog->OpenHolderWindow(*holder);
  CHECK(catalog->pending_focus_window.id() == window_id);
}

void TestCatalogWindowFrameSurvivesCloseReload() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  catalog->AddWindow();
  WindowHolder* holder = AsHolder(catalog->list->rows[0]);
  ae::ObjId const window_id = holder->window.id();
  holder->window->SubmitFrameFromNative(280, 180, 700, 430);
  catalog->CloseHolderWindow(*holder);
  CHECK(!holder->window.is_loaded());
  CHECK(holder->window.is_valid());

  catalog->OpenHolderWindow(*holder);
  CHECK(holder->window.is_loaded());
  CHECK(holder->window.id() == window_id);
  CHECK(holder->window->x() == 280);
  CHECK(holder->window->y() == 180);
  CHECK(holder->window->client_width() == 700);
  CHECK(holder->window->client_height() == 430);

  auto const dir = TestDir("apptraverse_window_catalog_frame_roundtrip");
  {
    DirectoryDomainStorage persist_storage{dir};
    ae::Domain persist_domain{persist_storage};
    auto persist_catalog = BuildWindowCatalogGraph(persist_domain);
    FinalizeDistilledGraph(*persist_catalog);
    persist_catalog->AddWindow();
    WindowHolder* p_holder = AsHolder(persist_catalog->list->rows[0]);
    p_holder->window->SubmitFrameFromNative(280, 180, 700, 430);
    persist_catalog->CloseHolderWindow(*p_holder);
    ShutdownSaveCatalog(*persist_catalog, persist_storage);
    persist_catalog.Reset();
  }

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
  auto reloaded =
      LoadPersistedWindowCatalog(load_domain, catalog_id);
  WindowHolder* r0 = AsHolder(reloaded->list->rows[0]);
  reloaded->OpenHolderWindow(*r0);
  CHECK(r0->window->x() == 280);
  CHECK(r0->window->y() == 180);
  CHECK(r0->window->client_width() == 700);
  CHECK(r0->window->client_height() == 430);
}

std::size_t CountSetWindowCatalogOpenEvents(WindowCatalog const& catalog,
                                            bool open_value) {
  std::size_t count = 0;
  for (auto const& record : catalog.journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    if (record.event->GetClassId() != SetWindowCatalogOpenEvent::kClassId) {
      continue;
    }
    auto const& open_event =
        static_cast<SetWindowCatalogOpenEvent const&>(*record.event);
    if (open_event.open == open_value) {
      ++count;
    }
  }
  return count;
}

bool HasUnloadedByDefault(ae::ObjFlags flags) {
  return (static_cast<std::uint32_t>(flags) &
          static_cast<std::uint32_t>(ae::ObjFlags::kUnloadedByDefault)) != 0;
}

void CheckDistilledCatalogBaseLinks(WindowCatalog const& catalog) {
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  CHECK(catalog.base.is_valid());
  CHECK(catalog.base.is_loaded());
  auto const& catalog_base = static_cast<WindowCatalog const&>(*catalog.base);
  CHECK(catalog_base.list.is_valid());
  CHECK(catalog_base.list.id() == ToObjId(ObjId::CatalogList));
  CHECK(catalog_base.main_window.is_valid());
  CHECK(catalog_base.main_window.id() == ToObjId(ObjId::MainWindow));
  auto const& main_presented =
      static_cast<ui::UiPresentedWindow const&>(*catalog_base.main_window);
  CHECK(main_presented.presenter.is_valid());
  CHECK(main_presented.presenter.id() == ToObjId(ObjId::MainWindowPresenter));
  CHECK(catalog_base.locale.is_valid());
  CHECK(catalog_base.texts.label_add.is_valid());
  CHECK(catalog_base.texts.label_open.is_valid());
  CHECK(catalog_base.texts.label_delete.is_valid());
  CHECK(catalog_base.texts.label_language.is_valid());
  CHECK(catalog_base.texts.window_title_prefix.is_valid());
  CHECK(catalog_base.buttons.add_button.is_valid());
  CHECK(catalog_base.buttons.add_button.id() == ToObjId(ObjId::AddButton));
  CHECK(catalog_base.buttons.open_button.is_valid());
  CHECK(catalog_base.buttons.delete_button.is_valid());
  CHECK(catalog_base.buttons.language_button.is_valid());
  CHECK(catalog_base.presenters.list_presenter.is_valid());
  CHECK(catalog_base.presenters.list_presenter.id() ==
        ToObjId(ObjId::CatalogListPresenter));
  CHECK(catalog_base.presenters.add_button_presenter.is_valid());
  CHECK(catalog_base.presenters.open_button_presenter.is_valid());
  CHECK(catalog_base.presenters.delete_button_presenter.is_valid());
  CHECK(catalog_base.presenters.language_button_presenter.is_valid());

  CHECK(catalog.list.is_valid());
  CHECK(catalog.list.is_loaded());
  CHECK(catalog.list->catalog.is_valid());
  CHECK(catalog.list->catalog.id() == ToObjId(ObjId::Catalog));
  CHECK(catalog.list->base.is_valid());
  CHECK(catalog.list->base.is_loaded());
}

void CheckLiveCatalogChromeLinks(WindowCatalog const& catalog,
                                 ae::ObjId const& catalog_id) {
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  CHECK(catalog.obj_id == catalog_id);
  CHECK(catalog.list.is_valid());
  CHECK(catalog.list.is_loaded());
  CHECK(catalog.list.id() ==
        ToObjId(window_catalog_demo::ObjId::CatalogList));
  CHECK(catalog.list->catalog.is_valid());
  CHECK(catalog.list->catalog.is_loaded());
  CHECK(catalog.list->catalog.id() == catalog_id);
  CHECK(catalog.list->catalog.operator->() == &catalog);
  CHECK(catalog.main_window.is_valid());
  CHECK(catalog.main_window.id() == ToObjId(ObjId::MainWindow));
  CHECK(MainWindowPresented(const_cast<WindowCatalog&>(catalog))
            .presenter.is_valid());
  CHECK(catalog.locale.is_valid());
  CHECK(catalog.texts.label_add.is_valid());
  CHECK(catalog.texts.label_open.is_valid());
  CHECK(catalog.texts.label_delete.is_valid());
  CHECK(catalog.texts.label_language.is_valid());
  CHECK(catalog.texts.window_title_prefix.is_valid());
  CHECK(catalog.buttons.add_button.is_valid());
  CHECK(catalog.buttons.open_button.is_valid());
  CHECK(catalog.buttons.delete_button.is_valid());
  CHECK(catalog.buttons.language_button.is_valid());
  CHECK(catalog.presenters.list_presenter.is_valid());
  CHECK(catalog.presenters.add_button_presenter.is_valid());
  CHECK(catalog.presenters.open_button_presenter.is_valid());
  CHECK(catalog.presenters.delete_button_presenter.is_valid());
  CHECK(catalog.presenters.language_button_presenter.is_valid());
}

void TestWindowCatalogOpenCloseEventJournalPersistence() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage ram_storage;
  ae::Domain ram_domain{ram_storage};
  auto catalog = BuildWindowCatalogGraph(ram_domain);
  FinalizeDistilledGraph(*catalog);
  CheckDistilledCatalogBaseLinks(*catalog);

  catalog->AddWindow();
  catalog->AddWindow();
  CHECK(catalog->list->rows.size() == 2);
  WindowHolder* holder1 = AsHolder(catalog->list->rows[0]);
  WindowHolder* holder2 = AsHolder(catalog->list->rows[1]);
  ae::ObjId const holder1_id = holder1->obj_id;
  ae::ObjId const holder2_id = holder2->obj_id;
  ae::ObjId const window1_id = holder1->window.id();
  ae::ObjId const window2_id = holder2->window.id();
  holder1->window->SubmitFrameFromNative(111, 222, 333, 444);

  catalog->CloseHolderWindow(*holder1);
  CHECK(holder1->window.is_valid());
  CHECK(!holder1->window.is_loaded());
  CHECK(HasUnloadedByDefault(holder1->window.flags()));
  CHECK(!ram_domain.Find(window1_id));

  catalog->OpenHolderWindow(*holder1);
  CHECK(holder1->window.is_loaded());
  CHECK(holder1->window.id() == window1_id);
  CHECK(holder1->window->x() == 111);
  CHECK(holder1->window->y() == 222);
  CHECK(holder1->window->client_width() == 333);
  CHECK(holder1->window->client_height() == 444);

  std::size_t const open_events_before_repeat =
      CountSetWindowCatalogOpenEvents(*catalog, true);
  catalog->OpenHolderWindow(*holder1);
  CHECK(CountSetWindowCatalogOpenEvents(*catalog, true) ==
        open_events_before_repeat);

  catalog->CloseHolderWindow(*holder2);
  CHECK(holder2->window.is_valid());
  CHECK(!holder2->window.is_loaded());
  CHECK(!ram_domain.Find(window2_id));

  CHECK(CountSetWindowCatalogOpenEvents(*catalog, false) == 2);
  CHECK(CountSetWindowCatalogOpenEvents(*catalog, true) == 1);
  CHECK(holder1->window.is_loaded());
  CHECK(!holder2->window.is_loaded());

  std::vector<ae::ObjId> open_close_event_ids;
  for (auto const& record : catalog->journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    if (record.event->GetClassId() != SetWindowCatalogOpenEvent::kClassId) {
      continue;
    }
    open_close_event_ids.push_back(record.event->obj_id);
  }
  CHECK(open_close_event_ids.size() == 3);

  auto const dir = TestDir("apptraverse_window_catalog_open_close_journal");
  ae::ObjId const catalog_id{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};

  ae::ObjId persisted_holder1_id{};
  ae::ObjId persisted_holder2_id{};
  ae::ObjId persisted_window1_id{};
  ae::ObjId persisted_window2_id{};
  ae::ObjId persisted_base_id{};
  std::size_t saved_journal_size{};
  std::vector<ae::ObjId> persisted_open_close_event_ids;

  {
    DirectoryDomainStorage persist_storage{dir};
    ae::Domain persist_domain{persist_storage};
    auto persist = BuildWindowCatalogGraph(persist_domain);
    FinalizeDistilledGraph(*persist);
    CheckDistilledCatalogBaseLinks(*persist);
    SaveDistilledRoot(*persist);
    persist.Reset();
  }

  {
    DirectoryDomainStorage persist_storage{dir};
    ae::Domain persist_domain{persist_storage};
    auto persist = LoadApplication<WindowCatalog>(persist_domain, catalog_id);
    persist->AddWindow();
    persist->AddWindow();
    WindowHolder* p1 = AsHolder(persist->list->rows[0]);
    WindowHolder* p2 = AsHolder(persist->list->rows[1]);
    p1->window->SubmitFrameFromNative(111, 222, 333, 444);
    p2->window->SubmitFrameFromNative(50, 60, 400, 300);
    persist->CloseHolderWindow(*p1);
    persist->OpenHolderWindow(*p1);
    persist->CloseHolderWindow(*p2);

    persisted_holder1_id = p1->obj_id;
    persisted_holder2_id = p2->obj_id;
    persisted_window1_id = p1->window.id();
    persisted_window2_id = p2->window.id();
    persisted_base_id = persist->base.id();
    for (auto const& record : persist->journal) {
      if (!record.event.is_valid()) {
        continue;
      }
      if (record.event->GetClassId() != SetWindowCatalogOpenEvent::kClassId) {
        continue;
      }
      persisted_open_close_event_ids.push_back(record.event->obj_id);
    }
    CHECK(persisted_open_close_event_ids.size() == 3);

    saved_journal_size = persist->journal.size();
    ShutdownSaveCatalog(*persist, persist_storage);
    persist.Reset();
  }

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};
  auto loaded =
      LoadPersistedWindowCatalog(load_domain, catalog_id);

  CHECK(loaded->list->rows.size() == 2);
  CHECK(loaded->next_window_number == 3);
  WindowHolder* l1 = AsHolder(loaded->list->rows[0]);
  WindowHolder* l2 = AsHolder(loaded->list->rows[1]);
  CHECK(l1->obj_id == persisted_holder1_id);
  CHECK(l2->obj_id == persisted_holder2_id);
  CHECK(l1->window_number == 1);
  CHECK(l2->window_number == 2);
  CHECK(l1->window.is_loaded());
  CHECK(l1->window.id() == persisted_window1_id);
  CHECK(!l2->window.is_loaded());
  CHECK(l2->window.id() == persisted_window2_id);
  CHECK(!load_domain.Find(persisted_window2_id));

  std::size_t persisted_open_events = 0;
  for (auto const& record : loaded->journal) {
    if (!record.event.is_valid()) {
      continue;
    }
    if (record.event->GetClassId() != SetWindowCatalogOpenEvent::kClassId) {
      continue;
    }
    auto const& ev =
        static_cast<SetWindowCatalogOpenEvent const&>(*record.event);
    if (ev.open) {
      ++persisted_open_events;
    }
    bool found = false;
    for (ae::ObjId const id : persisted_open_close_event_ids) {
      if (id == record.event->obj_id) {
        found = true;
        break;
      }
    }
    CHECK(found);
  }
  CHECK(persisted_open_events == 1);

  loaded->OpenHolderWindow(*l2);
  CHECK(l2->window.is_loaded());
  CHECK(l2->window.id() == persisted_window2_id);
  CHECK(l2->window->x() == 50);
  CHECK(l2->window->y() == 60);
  CHECK(l2->window->client_width() == 400);
  CHECK(l2->window->client_height() == 300);

  auto const invalid_base_profile_dir =
      TestDir("apptraverse_window_catalog_open_close_invalid_base_copy");
  CopyStorageProfile(dir, invalid_base_profile_dir);

  {
    DirectoryDomainStorage replay_storage{invalid_base_profile_dir};
    ae::Domain replay_domain{replay_storage};
    auto replay_catalog =
        LoadPersistedWindowCatalog(replay_domain, catalog_id);
    std::size_t const journal_size_before = replay_catalog->journal.size();
    std::size_t const rows_before = replay_catalog->list->rows.size();
    std::uint32_t const next_before = replay_catalog->next_window_number;
    std::uint64_t const generation_before = replay_catalog->Generation();
    replay_catalog->base = {};
    CHECK(!replay_catalog->TryReplayFromBase());
    CHECK(replay_catalog->journal.size() == journal_size_before);
    CHECK(replay_catalog->list->rows.size() == rows_before);
    CHECK(replay_catalog->next_window_number == next_before);
    CHECK(replay_catalog->Generation() == generation_before);
    replay_catalog.Reset();
  }

  auto const replay_profile_dir =
      TestDir("apptraverse_window_catalog_open_close_replay_copy");
  CopyStorageProfile(dir, replay_profile_dir);

  ae::ObjId expected_replay_catalog_id{};
  ae::ObjId expected_replay_list_id{};
  std::vector<ae::ObjId> expected_replay_event_ids;

  {
    DirectoryDomainStorage replay_storage{replay_profile_dir};
    ae::Domain replay_domain{replay_storage};
    auto replay_catalog =
        LoadPersistedWindowCatalog(replay_domain, catalog_id);
    CHECK(replay_catalog->journal.size() == saved_journal_size);
    CHECK(replay_catalog->base.is_valid());
    CHECK(replay_catalog->base.is_loaded());
    CHECK(replay_catalog->base.id() != replay_catalog->obj_id);
    CHECK(replay_catalog->base.id() == persisted_base_id);

    CHECK(replay_catalog->list.is_valid());
    CHECK(replay_catalog->list.is_loaded());
    CatalogListContainer* list_container = replay_catalog->list.operator->();
    CHECK(list_container != nullptr);
    CHECK(list_container->GetClassId() == CatalogListContainer::kClassId);
    CHECK(list_container->base.is_valid());
    CHECK(list_container->base.is_loaded());
    CHECK(list_container->base.id() != list_container->obj_id);

    ae::ObjId const catalog_obj_id_before = replay_catalog->obj_id;
    ae::ObjId const list_obj_id_before = list_container->obj_id;
    std::size_t const catalog_journal_size_before = replay_catalog->journal.size();
    std::uint32_t const next_window_number_before =
        replay_catalog->next_window_number;
    std::vector<ae::ObjId> catalog_event_ids_before;
    catalog_event_ids_before.reserve(replay_catalog->journal.size());
    for (auto const& record : replay_catalog->journal) {
      if (record.event.is_valid()) {
        catalog_event_ids_before.push_back(record.event->obj_id);
      }
    }

    CHECK(list_container->TryReplayFromBase());
    CHECK(list_container->rows.empty());
    CHECK(list_container->selection_index == -1);
    CHECK(list_container->journal.empty());
    CHECK(replay_catalog->base.is_valid());
    CHECK(replay_catalog->base.is_loaded());
    CHECK(replay_catalog->obj_id == catalog_obj_id_before);
    CHECK(list_container->obj_id == list_obj_id_before);
    CHECK(replay_catalog->journal.size() == catalog_journal_size_before);
    CHECK(replay_catalog->next_window_number == next_window_number_before);
    CHECK(catalog_event_ids_before.size() == replay_catalog->journal.size());
    for (std::size_t i = 0; i < replay_catalog->journal.size(); ++i) {
      CHECK(replay_catalog->journal[i].event.is_valid());
      CHECK(replay_catalog->journal[i].event->obj_id ==
            catalog_event_ids_before[i]);
    }

    CHECK(list_container->rows.empty());
    CHECK(replay_catalog->list.is_valid());
    CHECK(replay_catalog->list.is_loaded());
    CHECK(replay_catalog->list.id() == list_obj_id_before);
    CHECK(replay_catalog->list->obj_id == list_obj_id_before);
    CHECK(replay_catalog->TryReplayFromBase());
    CHECK(replay_catalog->obj_id == catalog_obj_id_before);
    CHECK(list_container->obj_id == list_obj_id_before);
    CHECK(replay_catalog->list.is_valid());
    CHECK(replay_catalog->list.is_loaded());
    CHECK(replay_catalog->list.id() == list_obj_id_before);
    CHECK(replay_catalog->list.operator->() == list_container);
    CHECK(list_container->catalog.is_valid());
    CHECK(list_container->catalog.id() == catalog_obj_id_before);
    CHECK(list_container->catalog.is_loaded());
    CHECK(list_container->catalog.operator->() == replay_catalog.operator->());
    CheckLiveCatalogChromeLinks(*replay_catalog, catalog_obj_id_before);
    CHECK(replay_catalog->list->rows.size() == 2);
    CHECK(replay_catalog->list->selection_index == 1);
    CHECK(replay_catalog->selected.is_valid());
    CHECK(replay_catalog->selected->obj_id == persisted_holder2_id);
    WindowHolder* r1 = AsHolder(replay_catalog->list->rows[0]);
    WindowHolder* r2 = AsHolder(replay_catalog->list->rows[1]);
    CHECK(r1->obj_id == persisted_holder1_id);
    CHECK(r2->obj_id == persisted_holder2_id);
    CHECK(r1->obj_id != r2->obj_id);
    CHECK(r1->window.id() == persisted_window1_id);
    CHECK(r2->window.id() == persisted_window2_id);
    CHECK(r1->window.id() != r2->window.id());
    CHECK(!HasUnloadedByDefault(r1->window.flags()));
    CHECK(HasUnloadedByDefault(r2->window.flags()));
    CHECK(replay_catalog->next_window_number == 3);
    CHECK(replay_catalog->journal.size() == saved_journal_size);
    CHECK(catalog_event_ids_before.size() == replay_catalog->journal.size());
    for (std::size_t i = 0; i < replay_catalog->journal.size(); ++i) {
      CHECK(replay_catalog->journal[i].event->obj_id ==
            catalog_event_ids_before[i]);
    }

    CHECK(r1->window.is_loaded());
    CHECK(r1->window->x() == 111);
    CHECK(r1->window->y() == 222);
    CHECK(r1->window->client_width() == 333);
    CHECK(r1->window->client_height() == 444);

    expected_replay_catalog_id = catalog_obj_id_before;
    expected_replay_list_id = list_obj_id_before;
    expected_replay_event_ids = catalog_event_ids_before;

    ShutdownSaveCatalog(*replay_catalog, replay_storage);
    replay_catalog.Reset();
  }

  {
    DirectoryDomainStorage cold_storage{replay_profile_dir};
    ae::Domain cold_domain{cold_storage};
    auto cold_catalog = LoadPersistedWindowCatalog(cold_domain, catalog_id);

    CHECK(cold_catalog->obj_id == expected_replay_catalog_id);
    CHECK(cold_catalog->base.is_valid());
    CHECK(cold_catalog->base.is_loaded());
    CHECK(cold_catalog->base.id() == persisted_base_id);
    CHECK(cold_catalog->list.is_valid());
    CHECK(cold_catalog->list.is_loaded());
    CHECK(cold_catalog->list.id() == expected_replay_list_id);
    CHECK(cold_catalog->list->obj_id == expected_replay_list_id);
    CHECK(cold_catalog->list->catalog.is_valid());
    CHECK(cold_catalog->list->catalog.is_loaded());
    CHECK(cold_catalog->list->catalog.id() == expected_replay_catalog_id);
    CHECK(cold_catalog->list->catalog.operator->() == cold_catalog.operator->());
    CheckLiveCatalogChromeLinks(*cold_catalog, expected_replay_catalog_id);

    CHECK(cold_catalog->list->rows.size() == 2);
    CHECK(cold_catalog->list->selection_index == 1);
    CHECK(cold_catalog->selected.is_valid());
    CHECK(cold_catalog->selected->obj_id == persisted_holder2_id);
    CHECK(cold_catalog->next_window_number == 3);
    CHECK(cold_catalog->journal.size() == saved_journal_size);
    CHECK(expected_replay_event_ids.size() == cold_catalog->journal.size());
    for (std::size_t i = 0; i < cold_catalog->journal.size(); ++i) {
      CHECK(cold_catalog->journal[i].event.is_valid());
      CHECK(cold_catalog->journal[i].event->obj_id ==
            expected_replay_event_ids[i]);
    }

    WindowHolder* cold_h1 = AsHolder(cold_catalog->list->rows[0]);
    WindowHolder* cold_h2 = AsHolder(cold_catalog->list->rows[1]);
    CHECK(cold_h1->obj_id == persisted_holder1_id);
    CHECK(cold_h2->obj_id == persisted_holder2_id);
    CHECK(cold_h1->window.id() == persisted_window1_id);
    CHECK(cold_h2->window.id() == persisted_window2_id);
    CHECK(!HasUnloadedByDefault(cold_h1->window.flags()));
    CHECK(HasUnloadedByDefault(cold_h2->window.flags()));
    CHECK(cold_h1->window.is_loaded());
    CHECK(!cold_h2->window.is_loaded());
    CHECK(!cold_domain.Find(persisted_window2_id));
    CHECK(cold_h1->window->x() == 111);
    CHECK(cold_h1->window->y() == 222);
    CHECK(cold_h1->window->client_width() == 333);
    CHECK(cold_h1->window->client_height() == 444);

    std::size_t const open_events_before_lazy_open =
        CountSetWindowCatalogOpenEvents(*cold_catalog, true);
    cold_catalog->OpenHolderWindow(*cold_h2);
    CHECK(cold_h2->window.is_loaded());
    CHECK(cold_h2->window.id() == persisted_window2_id);
    CHECK(cold_domain.Find(persisted_window2_id));
    CHECK(cold_h2->window->x() == 50);
    CHECK(cold_h2->window->y() == 60);
    CHECK(cold_h2->window->client_width() == 400);
    CHECK(cold_h2->window->client_height() == 300);
    CHECK(CountSetWindowCatalogOpenEvents(*cold_catalog, true) ==
          open_events_before_lazy_open + 1);

    std::size_t const open_events_before_repeat =
        CountSetWindowCatalogOpenEvents(*cold_catalog, true);
    cold_catalog->OpenHolderWindow(*cold_h1);
    CHECK(cold_h1->window.is_loaded());
    CHECK(CountSetWindowCatalogOpenEvents(*cold_catalog, true) ==
          open_events_before_repeat);

    cold_catalog.Reset();
  }
}

struct CatalogChromeButtonExpectations {
  ae::ObjId button_id{};
  ae::ObjId base_id{};
  ae::ObjId label_id{};
  ae::ObjId catalog_id{};
  bool enabled{false};
  std::string label_text_en;
};

void AssertDemoChromeButtonReflectsCatalog(ui::UiPushButton& button,
                                           ae::ObjId const& catalog_id) {
  CHECK(button.domain != nullptr);
  std::vector<ae::Obj*> reachable;
  CollectReachableObjects(static_cast<ae::Obj&>(button), reachable);
  bool linked_to_catalog = false;
  for (ae::Obj* obj : reachable) {
    if (obj->obj_id == catalog_id) {
      linked_to_catalog = true;
      break;
    }
  }
  CHECK(linked_to_catalog);
}

void AssertCatalogChromeButtonMatchesExpectations(
    WindowCatalog const& catalog,
    ae::ObjPtr<ui::UiPushButton> const& catalog_button_slot,
    CatalogChromeButtonExpectations const& expect) {
  CHECK(catalog.obj_id == expect.catalog_id);
  CHECK(catalog_button_slot.is_valid());
  CHECK(catalog_button_slot.is_loaded());
  CHECK(catalog_button_slot.id() == expect.button_id);
  ui::UiPushButton& button = *catalog_button_slot.operator->();
  CHECK(button.obj_id == expect.button_id);
  CHECK(button.base.is_valid());
  CHECK(button.base.is_loaded());
  CHECK(button.base.id() == expect.base_id);
  CHECK(button.label.is_valid());
  CHECK(button.label.is_loaded());
  CHECK(button.label.id() == expect.label_id);
  CHECK(button.label.id() == catalog_button_slot->label.id());
  CHECK(button.label->Resolve(0) == expect.label_text_en);
  auto const& button_base =
      static_cast<ui::UiPushButton const&>(*button.base);
  CHECK(button_base.label.is_valid());
  CHECK(button_base.label.id() == expect.label_id);
  CHECK(button.enabled() == expect.enabled);
  AssertDemoChromeButtonReflectsCatalog(button, expect.catalog_id);
}

CatalogChromeButtonExpectations CaptureCatalogChromeButtonExpectations(
    WindowCatalog const& catalog,
    ae::ObjPtr<ui::UiPushButton> const& catalog_button_slot,
    ae::ObjId const& button_id, ae::ObjId const& label_id,
    ae::ObjId const& catalog_id, bool enabled,
    std::string label_text_en) {
  CatalogChromeButtonExpectations expect;
  expect.button_id = button_id;
  expect.base_id = catalog_button_slot->base.id();
  expect.label_id = label_id;
  expect.catalog_id = catalog_id;
  expect.enabled = enabled;
  expect.label_text_en = std::move(label_text_en);
  AssertCatalogChromeButtonMatchesExpectations(catalog, catalog_button_slot,
                                               expect);
  return expect;
}

void AssertCatalogChromeButtonDistilledReplay(
    WindowCatalog const& catalog,
    ae::ObjPtr<ui::UiPushButton> const& catalog_button_slot,
    CatalogChromeButtonExpectations const& expect) {
  AssertCatalogChromeButtonMatchesExpectations(catalog, catalog_button_slot,
                                               expect);
  ui::UiPushButton& button = *catalog_button_slot.operator->();
  CHECK(button.TryReplayFromBase());
  AssertCatalogChromeButtonMatchesExpectations(catalog, catalog_button_slot,
                                               expect);
}

void TestWindowCatalogChromeButtonDistilledBaseReplay() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);

  ae::ObjId const catalog_id = ToObjId(ObjId::Catalog);
  CHECK(catalog->obj_id == catalog_id);
  CHECK(catalog->texts.label_add->Resolve(0) == "Add");
  CHECK(catalog->texts.label_open->Resolve(0) == "Open");
  CHECK(catalog->texts.label_delete->Resolve(0) == "Delete");
  CHECK(catalog->texts.label_language->Resolve(0) == "RU");

  auto const add_expect = CaptureCatalogChromeButtonExpectations(
      *catalog, catalog->buttons.add_button, ToObjId(ObjId::AddButton),
      catalog->texts.label_add.id(), catalog_id, true, "Add");
  auto const open_expect = CaptureCatalogChromeButtonExpectations(
      *catalog, catalog->buttons.open_button, ToObjId(ObjId::OpenButton),
      catalog->texts.label_open.id(), catalog_id, false, "Open");
  auto const delete_expect = CaptureCatalogChromeButtonExpectations(
      *catalog, catalog->buttons.delete_button, ToObjId(ObjId::DeleteButton),
      catalog->texts.label_delete.id(), catalog_id, false, "Delete");
  auto const language_expect = CaptureCatalogChromeButtonExpectations(
      *catalog, catalog->buttons.language_button,
      ToObjId(ObjId::LanguageButton), catalog->texts.label_language.id(),
      catalog_id, true, "RU");

  AssertCatalogChromeButtonDistilledReplay(*catalog, catalog->buttons.add_button,
                                           add_expect);
  AssertCatalogChromeButtonDistilledReplay(*catalog,
                                           catalog->buttons.open_button,
                                           open_expect);
  AssertCatalogChromeButtonDistilledReplay(*catalog,
                                           catalog->buttons.delete_button,
                                           delete_expect);
  AssertCatalogChromeButtonDistilledReplay(*catalog,
                                           catalog->buttons.language_button,
                                           language_expect);

  auto const dir = TestDir("apptraverse_window_catalog_chrome_button_base");
  CatalogChromeButtonExpectations cold_add_expect;
  CatalogChromeButtonExpectations cold_open_expect;
  CatalogChromeButtonExpectations cold_delete_expect;
  CatalogChromeButtonExpectations cold_language_expect;
  {
    DirectoryDomainStorage persist_storage{dir};
    ae::Domain persist_domain{persist_storage};
    auto persist = BuildWindowCatalogGraph(persist_domain);
    FinalizeDistilledGraph(*persist);
    ae::ObjId const persist_catalog_id = persist->obj_id;
    cold_add_expect = CaptureCatalogChromeButtonExpectations(
        *persist, persist->buttons.add_button, ToObjId(ObjId::AddButton),
        persist->texts.label_add.id(), persist_catalog_id, true, "Add");
    cold_open_expect = CaptureCatalogChromeButtonExpectations(
        *persist, persist->buttons.open_button, ToObjId(ObjId::OpenButton),
        persist->texts.label_open.id(), persist_catalog_id, false, "Open");
    cold_delete_expect = CaptureCatalogChromeButtonExpectations(
        *persist, persist->buttons.delete_button,
        ToObjId(ObjId::DeleteButton), persist->texts.label_delete.id(),
        persist_catalog_id, false, "Delete");
    cold_language_expect = CaptureCatalogChromeButtonExpectations(
        *persist, persist->buttons.language_button,
        ToObjId(ObjId::LanguageButton), persist->texts.label_language.id(),
        persist_catalog_id, true, "RU");
    ShutdownSaveCatalog(*persist, persist_storage);
    persist.Reset();
  }
  {
    DirectoryDomainStorage cold_storage{dir};
    ae::Domain cold_domain{cold_storage};
    ae::ObjId const cold_catalog_id = ToObjId(ObjId::Catalog);
    auto cold_catalog = LoadPersistedWindowCatalog(cold_domain, cold_catalog_id);
    CHECK(cold_catalog->locale.is_valid());
    CHECK(cold_catalog->locale.is_loaded());
    cold_catalog->MaterializeCatalogStrings(
        cold_catalog->locale->language_index());

    AssertCatalogChromeButtonMatchesExpectations(
        *cold_catalog, cold_catalog->buttons.add_button, cold_add_expect);
    AssertCatalogChromeButtonMatchesExpectations(
        *cold_catalog, cold_catalog->buttons.open_button, cold_open_expect);
    AssertCatalogChromeButtonMatchesExpectations(
        *cold_catalog, cold_catalog->buttons.delete_button, cold_delete_expect);
    AssertCatalogChromeButtonMatchesExpectations(
        *cold_catalog, cold_catalog->buttons.language_button,
        cold_language_expect);

    AssertCatalogChromeButtonDistilledReplay(*cold_catalog,
                                             cold_catalog->buttons.add_button,
                                             cold_add_expect);
    AssertCatalogChromeButtonDistilledReplay(*cold_catalog,
                                             cold_catalog->buttons.open_button,
                                             cold_open_expect);
    AssertCatalogChromeButtonDistilledReplay(*cold_catalog,
                                             cold_catalog->buttons.delete_button,
                                             cold_delete_expect);
    AssertCatalogChromeButtonDistilledReplay(
        *cold_catalog, cold_catalog->buttons.language_button,
        cold_language_expect);
    cold_catalog.Reset();
  }
}

void TestManyHoldersClosedGraphsAbsentFromDomain() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);

  for (int i = 0; i < 100; ++i) {
    catalog->AddWindow();
    if (i % 10 != 0) {
      catalog->CloseHolderWindow(*AsHolder(catalog->list->rows.back()));
    }
  }
  CHECK(catalog->list->rows.size() == 100);
  std::size_t loaded_count = 0;
  for (auto const& row : catalog->list->rows) {
    auto* holder = AsHolder(row);
    if (holder->window.is_loaded()) {
      ++loaded_count;
    } else {
      CHECK(!domain.Find(holder->window.id()));
    }
  }
  CHECK(loaded_count == 10);
}

void TestCatalogPresentedWindowDistilledBaseReplay() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);

  CatalogWindow& main =
      static_cast<CatalogWindow&>(*catalog->main_window.operator->());
  ae::ObjId const main_presenter_id =
      MainWindowPresented(*catalog).presenter.id();
  ae::ObjId const main_window_id = catalog->main_window.id();
  CHECK(main.base.is_valid());
  CHECK(main.base.is_loaded());
  CHECK(main.base->GetClassId() == CatalogWindow::kClassId);
  auto& main_base = static_cast<CatalogWindow&>(*main.base);
  CHECK(CatalogWindowPresented(main_base).presenter.is_valid());
  CHECK(CatalogWindowPresented(main_base).presenter.id() == main_presenter_id);

  ui::UiLocalizationString& label_add = *catalog->texts.label_add;
  ae::ObjId const label_en_id = label_add.strings[0].id();
  ae::ObjId const label_ru_id = label_add.strings[1].id();
  CHECK(label_add.base.is_valid());
  CHECK(label_add.base.is_loaded());
  auto& label_base = static_cast<ui::UiLocalizationString&>(*label_add.base);
  CHECK(label_base.strings.size() == 2);
  CHECK(label_base.strings[0].id() == label_en_id);
  CHECK(label_base.strings[1].id() == label_ru_id);

  CHECK(main.TryReplayFromBase());
  CHECK(MainWindowPresented(*catalog).presenter.id() == main_presenter_id);
  CHECK(MainWindowPresented(*catalog).presenter->window.id() == main_window_id);
  CHECK(MainWindowPresented(*catalog).presenter->locale.id() ==
        catalog->locale.id());

  CHECK(label_add.TryReplayFromBase());
  CHECK(label_add.strings[0].id() == label_en_id);
  CHECK(label_add.strings[1].id() == label_ru_id);

  catalog->AddWindow();
  WindowHolder* holder = AsHolder(catalog->list->rows[0]);
  CatalogWindow& child = *holder->window;
  ae::ObjId const child_presenter_id =
      CatalogWindowPresented(child).presenter.id();
  ae::ObjId const child_window_id = child.obj_id;
  ae::ObjId const holder_title_id = holder->primary_text.id();
  ae::ObjId const holder_en_id = holder->primary_text->strings[0].id();
  ae::ObjId const holder_ru_id = holder->primary_text->strings[1].id();

  CHECK(child.base.is_valid());
  CHECK(child.base.is_loaded());
  auto& child_base = static_cast<CatalogWindow&>(*child.base);
  CHECK(CatalogWindowPresented(child_base).presenter.id() == child_presenter_id);

  CHECK(holder->primary_text->base.is_valid());
  CHECK(holder->primary_text->base.is_loaded());
  auto& holder_title_base =
      static_cast<ui::UiLocalizationString&>(*holder->primary_text->base);
  CHECK(holder_title_base.strings[0].id() == holder_en_id);
  CHECK(holder_title_base.strings[1].id() == holder_ru_id);

  CHECK(child.TryReplayFromBase());
  CHECK(CatalogWindowPresented(child).presenter.id() == child_presenter_id);
  CHECK(CatalogWindowPresented(child).presenter->window.id() == child_window_id);
  CHECK(CatalogWindowPresented(child).presenter->title.id() == holder_title_id);
  CHECK(CatalogWindowPresented(child).presenter->locale.id() ==
        catalog->locale.id());
  CHECK(child.GetClassId() == CatalogWindow::kClassId);
  CHECK(CatalogWindowPresented(child).presenter->GetClassId() ==
        ui::windows::WinDesktopWindowPresenter::kClassId);
}

void TestCatalogPresentedWindowGraph() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);

  CHECK(MainWindowPresented(*catalog).presenter.is_valid());
  CHECK(MainWindowPresented(*catalog).presenter->window.id() ==
        catalog->main_window.id());
  CHECK(MainWindowPresented(*catalog).presenter->locale.id() ==
        catalog->locale.id());
  CHECK(MainWindowPresented(*catalog).presenter->title.is_valid());

  catalog->AddWindow();
  WindowHolder* holder = AsHolder(catalog->list->rows[0]);
  CHECK(CatalogWindowPresented(*holder->window).presenter.is_valid());
  CHECK(CatalogWindowPresented(*holder->window).presenter->window.id() ==
        holder->window.id());
  CHECK(CatalogWindowPresented(*holder->window).presenter->title.id() ==
        holder->primary_text.id());
  CHECK(CatalogWindowPresented(*holder->window).presenter->locale.id() ==
        catalog->locale.id());

  ae::ObjId const child_window_id = holder->window.id();
  catalog->CloseHolderWindow(*holder);
  CHECK(holder->window.is_valid());
  CHECK(!holder->window.is_loaded());
  CHECK(!domain.Find(child_window_id));

  auto const dir = TestDir("apptraverse_window_catalog_presenter_graph");
  ae::ObjId const catalog_id{ToObjId(ObjId::Catalog)};
  {
    DirectoryDomainStorage persist{dir};
    ae::Domain persist_domain{persist};
    auto persist_catalog = BuildWindowCatalogGraph(persist_domain);
    FinalizeDistilledGraph(*persist_catalog);
    persist_catalog->AddWindow();
    WindowHolder* p_holder = AsHolder(persist_catalog->list->rows[0]);
    ae::ObjId const p_window_id = p_holder->window.id();
    ae::ObjId const p_presenter_id =
        CatalogWindowPresented(*p_holder->window).presenter.id();
    persist_catalog->CloseHolderWindow(*p_holder);
    ShutdownSaveCatalog(*persist_catalog, persist);
    persist_catalog.Reset();

    DirectoryDomainStorage reload_storage{dir};
    ae::Domain reload_domain{reload_storage};
    auto reloaded = LoadPersistedWindowCatalog(reload_domain, catalog_id);
    WindowHolder* r0 = AsHolder(reloaded->list->rows[0]);
    CHECK(r0->window.id() == p_window_id);
    CHECK(!r0->window.is_loaded());
    reloaded->OpenHolderWindow(*r0);
    CHECK(r0->window.is_loaded());
    CHECK(CatalogWindowPresented(*r0->window).presenter.id() == p_presenter_id);
    CHECK(CatalogWindowPresented(*r0->window).presenter->window.id() ==
          r0->window.id());
    CHECK(CatalogWindowPresented(*r0->window).presenter->title.id() ==
          r0->primary_text.id());
  }

  auto const cold_dir = TestDir("apptraverse_window_catalog_presenter_cold");
  ae::ObjId saved_child_window_id{};
  ae::ObjId saved_child_presenter_id{};
  ae::ObjId saved_title_id{};
  {
    DirectoryDomainStorage cold_storage{cold_dir};
    ae::Domain cold_domain{cold_storage};
    auto saved = BuildWindowCatalogGraph(cold_domain);
    FinalizeDistilledGraph(*saved);
    saved->AddWindow();
    WindowHolder* saved_holder = AsHolder(saved->list->rows[0]);
    saved_child_window_id = saved_holder->window.id();
    saved_child_presenter_id =
        CatalogWindowPresented(*saved_holder->window).presenter.id();
    saved_title_id = saved_holder->primary_text.id();
    ShutdownSaveCatalog(*saved, cold_storage);
    saved.Reset();
  }

  DirectoryDomainStorage cold_load_storage{cold_dir};
  ae::Domain cold_load_domain{cold_load_storage};
  auto cold = LoadPersistedWindowCatalog(cold_load_domain, catalog_id);
  CHECK(MainWindowPresented(*cold).presenter.id() ==
        ToObjId(ObjId::MainWindowPresenter));
  CHECK(cold->main_window.id() == ToObjId(ObjId::MainWindow));
  WindowHolder* cold_row = AsHolder(cold->list->rows[0]);
  CHECK(cold_row->primary_text.id() == saved_title_id);
  cold->OpenHolderWindow(*cold_row);
  CHECK(cold_row->window.id() == saved_child_window_id);
  CHECK(CatalogWindowPresented(*cold_row->window).presenter.id() ==
        saved_child_presenter_id);
  CHECK(CatalogWindowPresented(*cold_row->window).presenter->title.id() ==
        saved_title_id);
}

void TestCatalogRepeatedOpenKeepsPresenterIdentity() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  catalog->AddWindow();
  WindowHolder* holder = AsHolder(catalog->list->rows[0]);
  ae::ObjId const window_id = holder->window.id();
  ae::ObjId const presenter_id =
      CatalogWindowPresented(*holder->window).presenter.id();
  catalog->OpenHolderWindow(*holder);
  CHECK(holder->window.id() == window_id);
  CHECK(CatalogWindowPresented(*holder->window).presenter.id() == presenter_id);
}

void TestCatalogDeleteUnloadedWindowDoesNotLoad() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);
  catalog->AddWindow();
  WindowHolder* holder = AsHolder(catalog->list->rows[0]);
  ae::ObjId const window_id = holder->window.id();
  catalog->CloseHolderWindow(*holder);
  CHECK(holder->window.is_valid());
  CHECK(!holder->window.is_loaded());
  CHECK(!domain.Find(window_id));
  catalog->SelectHolder(WindowHolder::ptr::MakeFromThis(holder));
  catalog->DeleteSelected();
  CHECK(catalog->list->rows.empty());
}

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestCatalogPresentedWindowDistilledBaseReplay();
  apptraverse::test::TestCatalogPresentedWindowGraph();
  apptraverse::test::TestCatalogRepeatedOpenKeepsPresenterIdentity();
  apptraverse::test::TestCatalogDeleteUnloadedWindowDoesNotLoad();
  apptraverse::test::TestCatalogReflectionTopologyAndInitialPublication();
  apptraverse::test::TestCatalogHolderOpenCloseAndMonotonicNumbers();
  apptraverse::test::TestCatalogLocalizationUtf8Persisted();
  apptraverse::test::TestCatalogRuntimeSavePreservesOpenWindowLoadedState();
  apptraverse::test::TestCatalogSaveAfterLanguageToggleAndChildClose();
  apptraverse::test::TestOpenLoadedWindowSetsPendingFocusRequest();
  apptraverse::test::TestCatalogWindowFrameSurvivesCloseReload();
  apptraverse::test::TestWindowCatalogOpenCloseEventJournalPersistence();
  apptraverse::test::TestWindowCatalogChromeButtonDistilledBaseReplay();
  apptraverse::test::TestManyHoldersClosedGraphsAbsentFromDomain();
  std::cout << "window_catalog_model_test OK\n";
  return 0;
}
