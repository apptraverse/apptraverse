#include <iostream>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"

#include "window_catalog_graph.h"
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

void TestCatalogHolderOpenCloseAndMonotonicNumbers() {
  EnsureObjectRegistration();
  EnsureWindowCatalogModelRegistration();
  EnsureWindowCatalogGraphRegistration();

  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildWindowCatalogGraph(domain);
  FinalizeDistilledGraph(*catalog);

  catalog->AddWindow();
  catalog->AddWindow();
  catalog->AddWindow();
  CHECK(catalog->list->rows.size() == 3);
  CHECK(catalog->next_window_number == 4);
  auto* row0 = static_cast<WindowHolder*>(catalog->list->rows[0].operator->());
  auto* row2 = static_cast<WindowHolder*>(catalog->list->rows[2].operator->());
  CHECK(row0->window_number == 1);
  CHECK(row2->window_number == 3);
  CHECK(row0->window.is_loaded());
  CHECK(row2->window.is_loaded());

  WindowHolder& middle =
      *static_cast<WindowHolder*>(catalog->list->rows[1].operator->());
  ae::ObjId const middle_window_id = middle.window.id();
  catalog->CloseHolderWindow(middle);
  CHECK(!middle.window.is_loaded());
  CHECK(middle.window.is_valid());
  CHECK(!domain.Find(middle_window_id));
  CHECK(row0->window.is_loaded());
  CHECK(row2->window.is_loaded());

  catalog->OpenHolderWindow(middle);
  CHECK(middle.window.is_loaded());
  CHECK(middle.window.id() == middle_window_id);

  catalog->SelectHolder(catalog->list->rows[0]);
  catalog->DeleteSelected();
  catalog->SelectHolder(catalog->list->rows[0]);
  catalog->DeleteSelected();
  CHECK(catalog->list->rows.size() == 1);
  catalog->AddWindow();
  CHECK(static_cast<WindowHolder*>(catalog->list->rows.back().operator->())
            ->window_number == 4);

  ByteSink sink;
  SerializeInitialPublication(*catalog, sink);
  ae::RamDomainStorage storage2;
  ae::Domain domain2{storage2};
  ByteSource in;
  in.data = sink.bytes.data();
  in.size = sink.bytes.size();
  auto reloaded = LoadInitialPublication(in, domain2, storage2);
  CHECK(reloaded);
  auto catalog2 = WindowCatalog::ptr::MakeFromThis(
      static_cast<WindowCatalog*>(reloaded.get()));
  CHECK(catalog2->list->rows.size() == 2);
  CHECK(catalog2->next_window_number == 5);
  CHECK(!static_cast<WindowHolder*>(catalog2->list->rows[0].operator->())
             ->window.is_loaded());
  CHECK(static_cast<WindowHolder*>(catalog2->list->rows[1].operator->())
             ->window.is_loaded());
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
      catalog->CloseHolderWindow(*static_cast<WindowHolder*>(
          catalog->list->rows.back().operator->()));
    }
  }
  CHECK(catalog->list->rows.size() == 100);
  std::size_t loaded_count = 0;
  for (auto const& row : catalog->list->rows) {
    auto* holder = static_cast<WindowHolder*>(row.operator->());
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
  apptraverse::test::TestCatalogHolderOpenCloseAndMonotonicNumbers();
  apptraverse::test::TestManyHoldersClosedGraphsAbsentFromDomain();
  std::cout << "window_catalog_model_test OK\n";
  return 0;
}
