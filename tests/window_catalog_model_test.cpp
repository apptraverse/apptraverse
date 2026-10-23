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

    SaveDistilledRoot(*persist_catalog);
    persist_catalog.Reset();
  }

  DirectoryDomainStorage load_storage{dir};
  ae::Domain load_domain{load_storage};
  auto reloaded = LoadApplication<WindowCatalog>(load_domain, catalog_id);
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
  apptraverse::test::TestManyHoldersClosedGraphsAbsentFromDomain();
  std::cout << "window_catalog_model_test OK\n";
  return 0;
}
