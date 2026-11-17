#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "apptraverse/distill.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/client_local_state.h"
#include "apptraverse/single_file_user_storage.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_row_text.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"
#include "window_catalog_supply_constant_ids.h"

#if defined(APPTRAVERSE_PORTABLE_ANDROID)
#include "embedded_distilled_window_catalog_android_supply.h"
#include "window_catalog_graph_android.h"
#elif defined(__EMSCRIPTEN__)
#include "embedded_distilled_window_catalog_web_supply.h"
#include "window_catalog_graph_web.h"
#else
#include "embedded_distilled_window_catalog_supply.h"
#include "window_catalog_graph.h"
#endif

namespace {
using namespace apptraverse;
using Snapshot = std::map<std::string, std::string>;

void Require(bool condition, char const* message) {
  if (!condition) {
    std::cerr << "PORTABLE_USER_FAILURE " << message << std::endl;
    std::exit(1);
  }
}

WindowHolder& Holder(WindowCatalog& catalog, std::size_t index) {
  return *static_cast<WindowHolder*>(catalog.persisted.list->rows[index].operator->());
}

void Prepare(WindowCatalog& catalog) {
  PrepareWindowCatalogForRun(catalog);
}

template <typename T>
void Put(Snapshot& snapshot, std::string key, T value) {
  snapshot.emplace(std::move(key), std::to_string(value));
}

Snapshot Inspect(WindowCatalog& catalog) {
  catalog.persisted.list.Load();
  catalog.persisted.locale.Load();
  catalog.MaterializeCatalogStrings(catalog.persisted.locale->language_index());
  Snapshot result;
  Put(result, "catalog", catalog.obj_id.id());
  Put(result, "locale", catalog.persisted.locale.id().id());
  Put(result, "language", catalog.persisted.locale->language_index());
  Put(result, "next_window_number", catalog.persisted.next_window_number);
  Put(result, "row_count", catalog.persisted.list->rows.size());
  Put(result, "selection", catalog.persisted.list->selection_index);
  Put(result, "selected_id", catalog.persisted.selected.id().id());
  Put(result, "active_id", catalog.ResolveActiveWindow().id().id());
  Put(result, "window_order_count", catalog.persisted.window_order.size());
  for (std::size_t i = 0; i < catalog.persisted.window_order.size(); ++i) {
    Put(result, "window_order." + std::to_string(i),
        catalog.persisted.window_order[i].id().id());
  }
  for (std::size_t i = 0; i < catalog.persisted.list->rows.size(); ++i) {
    auto& holder = Holder(catalog, i);
    auto const prefix = "row." + std::to_string(i) + ".";
    bool const was_open = catalog.IsHolderWindowOpen(holder);
    Put(result, prefix + "holder", holder.obj_id.id());
    Put(result, prefix + "window", holder.window.id().id());
    Put(result, prefix + "number", holder.window_number);
    Put(result, prefix + "open", was_open);
    result.emplace(prefix + "localized_row",
        ResolveWindowCatalogHolderListRowText(holder, catalog,
            catalog.persisted.locale->language_index()));
    // Explicitly traverse a closed lazy subgraph in the cold domain as well.
    holder.window.Load();
    Require(holder.window.is_loaded(), "cold lazy window missing");
    auto& window = *holder.window;
    window.presenter.Load();
    Require(window.presenter.is_loaded(), "cold lazy presenter missing");
    Put(result, prefix + "presenter", window.presenter.id().id());
    Put(result, prefix + "x", window.x());
    Put(result, prefix + "y", window.y());
    Put(result, prefix + "restore_width", window.restore_client_width());
    Put(result, prefix + "restore_height", window.restore_client_height());
    if (!was_open) {
      holder.window.Reset();
    }
  }
  return result;
}

Snapshot ReadManifest(std::filesystem::path const& profile) {
  std::ifstream input{profile / "manifest.txt", std::ios::binary};
  Require(static_cast<bool>(input), "manifest missing");
  Snapshot result;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto const split = line.find('=');
    Require(split != std::string::npos, "bad manifest line");
    result.emplace(line.substr(0, split), line.substr(split + 1));
  }
  return result;
}

void CheckManifest(Snapshot const& actual, Snapshot const& expected) {
  if (actual == expected) return;
  for (auto const& [key, value] : expected) {
    auto found = actual.find(key);
    if (found == actual.end() || found->second != value) {
      std::cerr << "PORTABLE_USER_MISMATCH " << key << " expected=" << value
                << " actual=" << (found == actual.end() ? "<absent>" : found->second)
                << '\n';
    }
  }
  Require(false, "semantic state changed during transfer");
}

void Save(WindowCatalog& catalog, ae::RamDomainStorage& user, ae::RamDomainStorage& local,
          std::filesystem::path const& profile) {
  Require(!std::filesystem::exists(profile / "user.bin"), "output profile already exists");
  std::filesystem::create_directories(profile);
  auto const snapshot = Inspect(catalog);
  SaveWindowCatalogPersistentSnapshot(catalog);
  Require(SaveUserStorageFile(user, profile), "USER save failed");
  Require(SaveUserStorageFile(local, profile / "local"), "LOCAL save failed");
  std::ofstream manifest{profile / "manifest.txt", std::ios::binary};
  for (auto const& [key, value] : snapshot) manifest << key << '=' << value << '\n';
  Require(static_cast<bool>(manifest), "manifest save failed");
  for (auto const& id : CollectWindowCatalogSupplyConstantIds(catalog)) {
    Require(user.state.count(id) == 0, "constant bodies leaked into USER");
  }
}

void Seed(WindowCatalog& catalog) {
  for (int i = 0; i < 3; ++i) catalog.AddWindow();
  catalog.persisted.list.Load();
  Require(catalog.persisted.list->rows.size() == 3, "seed did not create three rows");
  for (std::size_t i = 0; i < 3; ++i) {
    auto& holder = Holder(catalog, i);
    catalog.OpenHolderWindow(holder);
    holder.window->RequestFrame(-1200 + static_cast<int>(i) * 90,
        -70 + static_cast<int>(i) * 55, 520 + static_cast<int>(i) * 40,
        380 + static_cast<int>(i) * 30);
  }
  catalog.CloseHolderWindow(Holder(catalog, 1));
  catalog.SelectHolder(WindowHolder::ptr::MakeFromThis(&Holder(catalog, 2)));
  catalog.persisted.locale->SubmitLanguageIndexFromUi(1);
  catalog.MaterializeCatalogStrings(1);
}

void Continue(WindowCatalog& catalog) {
  // Active-window identity must not retain a child after Close on any host.
  // The destination's subsequent Add must not reopen it.
  ae::ObjId const previous_active = catalog.ResolveActiveWindow().id();
  WindowHolder* previous_active_holder = catalog.FindHolderForWindow(previous_active);
  if (previous_active_holder != nullptr) {
    catalog.CloseHolderWindow(*previous_active_holder);
    Require(!catalog.IsHolderWindowOpen(*previous_active_holder),
            "previous active page remained open after close");
    Require(!catalog.domain->Find(previous_active),
            "previous active page retained its unloaded graph");
  }
  // Reopen a genuinely cold lazy window, mutate, close it, then clone again.
  auto& closed = Holder(catalog, 1);
  catalog.OpenHolderWindow(closed);
  Require(closed.window->presenter.is_loaded(), "reopened presenter missing");
  closed.window->RequestFrame(-980, -95, 633, 477);
  catalog.CloseHolderWindow(closed);
  // A viewport changes client size, never the saved free-window restore frame.
  auto& open = Holder(catalog, 0);
  auto const restore_x = open.window->x();
  auto const restore_y = open.window->y();
  auto const restore_w = open.window->restore_client_width();
  auto const restore_h = open.window->restore_client_height();
  catalog.SubmitHolderChildWindowClientSizeFromUi(open.window.id().id(), 393, 759, 160);
  Require(open.window->x() == restore_x && open.window->y() == restore_y &&
          open.window->restore_client_width() == restore_w &&
          open.window->restore_client_height() == restore_h,
          "viewport overwrote restore geometry");
  catalog.AddWindow();
  if (previous_active_holder != nullptr) {
    Require(!catalog.IsHolderWindowOpen(*previous_active_holder),
            "subsequent Add resurrected a previously active page");
  }
  catalog.persisted.locale->SubmitLanguageIndexFromUi(
      catalog.persisted.locale->language_index() == 0 ? 1 : 0);
  catalog.MaterializeCatalogStrings(catalog.persisted.locale->language_index());
}
}  // namespace

int main(int argc, char** argv) {
  Require(argc >= 3, "usage: seed|verify|continue|inspect|verify-reopened INPUT [OUTPUT_OR_SOURCE]");
  std::cout << "PORTABLE_USER_ABI " << APPTRAVERSE_PORTABLE_PLATFORM
            << " pointer_bits=" << sizeof(void*) * 8
            << " size_t_bits=" << sizeof(std::size_t) * 8 << std::endl;
#if defined(APPTRAVERSE_PORTABLE_ANDROID)
  apptraverse::EnsureWindowCatalogAndroidGraphRegistration();
  auto const& graph = apptraverse::embedded_distilled::window_catalog::kEmbeddedGraph;
#elif defined(__EMSCRIPTEN__)
  apptraverse::EnsureWebWindowCatalogGraphRegistration();
  auto const& graph = apptraverse::embedded_distilled::window_catalog_web::kEmbeddedGraph;
#else
  apptraverse::EnsureWindowCatalogGraphRegistration();
  auto const& graph = apptraverse::embedded_distilled::window_catalog::kEmbeddedGraph;
#endif
  std::string const operation = argv[1];
  std::filesystem::path seed_dir, destination_local;
  for (int i = 3; i + 1 < argc; ++i) {
    if (std::string{argv[i]} == "--local-seed-dir") seed_dir = argv[++i];
    else if (std::string{argv[i]} == "--destination-local-dir") destination_local = argv[++i];
  }
  std::filesystem::path const profile = argv[2];
  ae::RamDomainStorage backing;
  apptraverse::InstallEmbeddedDistilledBacking(backing, graph);
  ae::RamDomainStorage user, local;
  if (operation != "seed") {
    Require(std::filesystem::is_regular_file(profile / "user.bin"), "USER missing");
    Require(apptraverse::LoadUserStorageFile(user, profile), "USER decode failed");
  }
  if (operation != "transfer") {
    Require(LoadUserStorageFile(local, profile / "local"), "LOCAL decode failed");
  } else if (!destination_local.empty()) {
    Require(LoadUserStorageFile(local, destination_local), "destination LOCAL decode failed");
  }
  auto const existing_local = local.state;
  if (local.state.empty() && !seed_dir.empty()) {
    ae::RamDomainStorage seed;
    Require(LoadUserStorageFile(seed, seed_dir), "LOCAL seed decode failed");
    SeedClientLocalStorage(local, seed);
  }
  apptraverse::ClientLocalDomainStorage routed{user, local, backing, CollectClientLocalRoots(backing)};
  apptraverse::OverlayDomainStorage overlay{
      routed, backing, apptraverse::EmbeddedSupplyConstantIdSet(graph)};
  ae::Domain domain{overlay};
  auto catalog = apptraverse::LoadApplication<apptraverse::WindowCatalog>(domain,
      ae::ObjId{apptraverse::window_catalog_demo::ToObjId(
          apptraverse::window_catalog_demo::ObjId::Catalog)});
  Require(catalog.is_loaded(), "catalog load failed");
  Prepare(*catalog);
  if (operation == "seed") {
    Seed(*catalog);
    ui::UiDeviceEnvironment environment;
    environment.monitors.push_back({"fixture-device", {0,0,1280,960}, {0,0,1280,920}, true, 0, 60});
    catalog->persisted.device_state->ObserveEnvironment(environment, ui::UiEnvironmentReason::kStartup);
    catalog->persisted.device_state->SetAppearance({ui::UiThemeMode::kDark, true});
    Save(*catalog, user, local, profile);
  } else if (operation == "transfer") {
    Require(argc >= 4, "transfer requires separate output profile");
    if (!existing_local.empty()) Require(local.state == existing_local, "loading portable USER changed existing LOCAL");
    Save(*catalog, user, local, argv[3]);
    if (!existing_local.empty()) Require(local.state == existing_local, "transfer save changed existing LOCAL");
  } else if (operation == "inspect-local") {
    auto const& device = *catalog->persisted.device_state;
    std::cout << "theme=" << static_cast<int>(device.appearance.theme) << "\nsoft=" << device.appearance.soften_dark
      << "\nenvironment=" << device.has_environment << "\nrevision=" << device.observation_revision
      << "\nmonitor=" << (device.environment.monitors.empty() ? "" : device.environment.monitors.front().key)
      << "\nlayout.padding=" << catalog->persisted.main_window->layout_state->EffectiveRoot().padding
      << "\nlayout.override=" << catalog->persisted.main_window->layout_state->has_override << '\n';
    if (auto const* p = device.PlacementFor(catalog->persisted.main_window.id()))
      std::cout << "main.outer.width=" << p->frame_dip.width << "\nmain.outer.height=" << p->frame_dip.height << '\n';
  } else if (operation == "set-layout-override" || operation == "clear-layout-override") {
    Require(argc == 4, "layout edit requires separate output profile");
    auto& state = *catalog->persisted.main_window->layout_state;
    if (operation == "set-layout-override") {
      auto root = state.EffectiveRoot(); root.padding = 44; state.SetOverride(root);
    } else state.ClearOverride();
    Save(*catalog, user, local, argv[3]);
  } else if (operation == "inspect") {
    for (auto const& [key, value] : Inspect(*catalog)) {
      std::cout << key << '=' << value << '\n';
    }
  } else if (operation == "verify-reopened") {
    // Native GUI transfer scenario: the only deliberate edit is opening row 1.
    Require(argc == 4, "verify-reopened requires original source manifest");
    auto expected = ReadManifest(argv[3]);
    std::size_t reopened = 0;
    auto const rows = std::stoul(expected.at("row_count"));
    while (reopened < rows && expected.at("row." + std::to_string(reopened) + ".open") != "0") ++reopened;
    Require(reopened < rows, "native transfer source has no closed child");
    auto prefix = "row." + std::to_string(reopened) + ".";
    expected[prefix + "open"] = "1";
    auto& row_text = expected.at(prefix + "localized_row");
    row_text = row_text.substr(0, row_text.find(" ("));
    expected["selection"] = std::to_string(reopened);
    expected["selected_id"] = expected.at(prefix + "holder");
    expected["active_id"] = expected.at(prefix + "window");
    std::vector<std::string> order{expected.at(prefix + "window")};
    auto const previous_count = std::stoul(expected.at("window_order_count"));
    for (std::size_t i = 0; i < previous_count; ++i) {
      auto id = expected.at("window_order." + std::to_string(i));
      if (id != order.front()) order.push_back(id);
    }
    expected["window_order_count"] = std::to_string(order.size());
    for (std::size_t i = 0; i < order.size(); ++i)
      expected["window_order." + std::to_string(i)] = order[i];
    CheckManifest(Inspect(*catalog), expected);
  } else {
    CheckManifest(Inspect(*catalog), ReadManifest(profile));
    if (operation == "continue") {
      Require(argc == 4, "continue requires separate output profile");
      Continue(*catalog);
      Save(*catalog, user, local, argv[3]);
    } else {
      Require(operation == "verify", "unknown operation");
    }
  }
  std::cout << "PORTABLE_USER_OK " << APPTRAVERSE_PORTABLE_PLATFORM << ' '
            << operation << std::endl;
}
