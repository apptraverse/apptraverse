// Target-ABI inspection of real Android lifecycle saves and portable transfers.
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include "aether-objects/obj/domain.h"
#include "apptraverse/client_local_state.h"
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#include "embedded_distilled_window_catalog_android_supply.h"
#include "window_catalog_graph_android.h"
#include "window_catalog_model.h"
#include "window_catalog_ids.h"
#include "window_catalog_prepare.h"

using namespace apptraverse;
void Require(bool condition, char const* message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char** argv) {
  Require(argc == 2 || argc == 3 || argc == 4, "usage: android_environment_fixture PROFILE [override PADDING|missing-control]");
  EnsureWindowCatalogAndroidGraphRegistration();
  std::filesystem::path profile{argv[1]};
  ae::RamDomainStorage user, local, backing;
  Require(LoadUserStorageFile(user, profile), "cannot read USER");
  Require(LoadUserStorageFile(local, profile / "local"), "cannot read LOCAL");
  auto const& graph = embedded_distilled::window_catalog::kEmbeddedGraph;
  InstallEmbeddedDistilledBacking(backing, graph);
  auto roots = CollectClientLocalRoots(backing);
  for (auto const& [id, layers] : user.state) {
    auto scope = user.ReadObjectScope(id);
    Require(roots.count(id) == 0 && (!scope || roots.count(scope->root_id) == 0),
        "LOCAL object/layer/Event leaked into portable USER");
  }
  ClientLocalDomainStorage routed{user, local, backing, roots};
  OverlayDomainStorage overlay{routed, backing, EmbeddedSupplyConstantIdSet(graph)};
  ae::Domain domain{overlay};
  auto catalog = LoadApplication<WindowCatalog>(domain, ae::ObjId{graph.root_object_id});
  PrepareWindowCatalogForRun(*catalog);
  if (argc >= 3) {
    Require(std::string{argv[2]} == "override" || std::string{argv[2]} == "missing-control", "unknown operation");
    auto root = catalog->persisted.main_window->layout->EffectiveRoot();
    if (argc == 4) root.padding = std::strtod(argv[3], nullptr);
    else {
      ui::UiLayoutNode absent;
      absent.control_id = 424242;
      absent.min_width = absent.min_height = 32;
      root.children.push_back(absent);
    }
    catalog->persisted.main_window->layout->SetOverride(std::move(root));
    SaveWindowCatalogPersistentSnapshot(*catalog);
    Require(SaveUserStorageFile(user, profile), "cannot write USER");
    Require(SaveUserStorageFile(local, profile / "local"), "cannot write LOCAL");
  }
  auto const& state = *catalog->persisted.device_state;
  auto const& layout = *catalog->persisted.main_window->layout;
  std::cout << "default_padding=" << layout.definition->root.padding
            << "\neffective_padding=" << layout.EffectiveRoot().padding
            << "\nhas_override=" << layout.has_override << '\n';
  std::cout << "row_count=" << catalog->persisted.list->rows.size()
            << "\nlanguage=" << catalog->persisted.locale->language_index()
            << "\nactive_id=" << catalog->ResolveActiveWindow().id().id()
            << "\nnext_number=" << catalog->persisted.next_window_number
            << "\nlocal_has_environment=" << state.has_environment
            << "\nlocal_revision=" << state.observation_revision
            << "\nlocal_last_reason=" << static_cast<int>(state.last_reason)
            << "\nlocal_last_changes=" << state.last_changes
            << "\nlocal_theme=" << static_cast<int>(state.appearance.theme)
            << "\nlocal_soft=" << state.appearance.soften_dark
            << "\ndpi=" << state.environment.system_dpi_x
            << "\ntext_scale=" << state.environment.text_scale_milli
            << "\nos_dark=" << state.environment.dark
            << "\nreduced_motion=" << state.environment.reduced_motion
            << "\nwindow_count=" << state.windows.size() << '\n';
  for (std::size_t i = 0; i < catalog->persisted.list->rows.size(); ++i) {
    auto& holder = *static_cast<WindowHolder*>(catalog->persisted.list->rows[i].operator->());
    std::cout << "holder." << i << '=' << holder.obj_id.id() << ','
              << holder.window.id().id() << ',' << holder.window_number << ','
              << catalog->IsHolderWindowOpen(holder) << '\n';
  }
  for (auto const& monitor : state.environment.monitors)
    std::cout << "monitor=" << monitor.key << ',' << monitor.bounds.width << ','
              << monitor.bounds.height << '\n';
  std::cout << "ANDROID_ENVIRONMENT_INSPECT_OK\n";
}
