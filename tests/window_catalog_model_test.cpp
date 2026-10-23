#include <filesystem>
#include <iostream>
#include <unordered_set>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/graph_walk.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

#include "window_catalog_graph.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/ui/ui_localization_string.h"

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

void ShutdownSaveCatalog(WindowCatalog& catalog,
                         DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  CHECK(SavePersistentModelSnapshotToStorage(catalog, storage));
  CHECK(!storage.SaveSessionHadFailure());
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
  ExpectReflectedGraphEdge(reflected_ids, catalog->main_window_presenter);
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

  CHECK(ui_catalog->main_window_presenter.is_loaded());
  CHECK(ui_domain.Find(ui_catalog->main_window_presenter.id()));
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
  auto reloaded = LoadApplication<WindowCatalog>(load_domain, ae::ObjId{catalog_id});
  RepairWindowCatalogEmbeddedStrings(*reloaded);
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
  CHECK(r4->window->window_presenter.is_loaded());
  CHECK(r4->window->title.is_valid());

  reloaded->OpenHolderWindow(*r3);
  CHECK(r3->window.is_loaded());
  CHECK(r3->window.id() == saved_window3_id);
  CHECK(r3->window->window_presenter.is_loaded());
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
  auto reloaded = LoadApplication<WindowCatalog>(
      reload_domain,
      ae::ObjId{window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)});
  RepairWindowCatalogEmbeddedStrings(*reloaded);
  reloaded->MaterializeCatalogStrings(0);
  CHECK(reloaded->texts.label_add->Resolve(0) == "Add");
  CHECK(ReadInactiveUtf8(*reloaded->texts.label_add, 1) == "Добавить");
  WindowHolder* r0 = AsHolder(reloaded->list->rows[0]);
  CHECK(r0->primary_text->Resolve(0) == "Window 1");
  CHECK(ReadInactiveUtf8(*r0->primary_text, 1) == "Окно 1");
  CHECK(reloaded->main_window_presenter->title->Resolve(0) == "Window Catalog");
  CHECK(ReadInactiveUtf8(*reloaded->main_window_presenter->title, 1) ==
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
    RepairWindowCatalogEmbeddedStrings(*catalog);
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

    ShutdownSaveCatalog(*catalog, storage_b);
    catalog.Reset();
  }

  DirectoryDomainStorage storage_c{dir};
  ae::Domain domain_c{storage_c};
  auto catalog = LoadApplication<WindowCatalog>(domain_c, ae::ObjId{catalog_id});
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
  RepairWindowCatalogEmbeddedStrings(*catalog);
  CHECK(r0->primary_text->Resolve(0) == "Window 1");
  CHECK(r1->primary_text->Resolve(0) == "Window 2");
  CHECK(r2->primary_text->Resolve(0) == "Window 3");
  int const language = catalog->locale ? catalog->locale->language_index() : 0;
  for (auto const& row : catalog->list->rows) {
    if (!row) {
      continue;
    }
    auto* holder = AsHolder(row);
    if (holder->primary_text.is_valid() && !holder->primary_text.is_loaded()) {
      holder->primary_text.Load();
    }
    catalog->MaterializeHolderListText(*holder, language);
    EnsureHolderWindowTitleLinks(*holder, *catalog);
  }
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
  auto catalog = LoadApplication<WindowCatalog>(load_domain, catalog_id);
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
  catalog->pending_focus_window_id = 0;
  catalog->OpenHolderWindow(*holder);
  CHECK(catalog->pending_focus_window_id == window_id.id());
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

}  // namespace
}  // namespace apptraverse::test

int main() {
  apptraverse::test::TestCatalogReflectionTopologyAndInitialPublication();
  apptraverse::test::TestCatalogHolderOpenCloseAndMonotonicNumbers();
  apptraverse::test::TestCatalogLocalizationUtf8Persisted();
  apptraverse::test::TestCatalogRuntimeSavePreservesOpenWindowLoadedState();
  apptraverse::test::TestCatalogSaveAfterLanguageToggleAndChildClose();
  apptraverse::test::TestOpenLoadedWindowSetsPendingFocusRequest();
  apptraverse::test::TestManyHoldersClosedGraphsAbsentFromDomain();
  std::cout << "window_catalog_model_test OK\n";
  return 0;
}
