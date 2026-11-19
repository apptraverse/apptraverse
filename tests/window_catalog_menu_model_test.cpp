#include <cstdlib>
#include <iostream>
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "window_catalog_graph.h"
#include "window_catalog_menu.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
using namespace apptraverse;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed: " #condition << " line " << __LINE__ << '\n'; std::exit(1); } } while(false)
#include "window_catalog_fullscreen_model_cases.h"
int main() {
  using namespace ui::windows;
  WinUiPalette const hc{RGB(0,0,0), RGB(255,255,0), RGB(0,0,0), RGB(200,200,200), RGB(255,255,255), false, true};
  WinUiPalette const light{RGB(255,255,255), RGB(0,0,0), RGB(240,240,240), RGB(100,100,100), RGB(0,0,0), false, false};
  for (auto theme : {ui::UiAppearancePreferences{}, ui::UiAppearancePreferences{ui::UiThemeMode::kLight,false},
       ui::UiAppearancePreferences{ui::UiThemeMode::kDark,false}, ui::UiAppearancePreferences{ui::UiThemeMode::kDark,true}})
    CHECK(ResolveWinUiPalette(hc, light, theme) == hc);
  EnsureWindowCatalogGraphRegistration();
  for (bool compact : {false, true}) {
    TestFullscreenModelAndPublication(compact);
    TestFullscreenColdAndPlatformSeed(false, compact);
    TestFullscreenColdAndPlatformSeed(true, compact);
  }
  for (auto handling : {CatalogThemeHandling::kAdapterAppliesTheme, CatalogThemeHandling::kNativeFollowsSystem}) {
    ae::RamDomainStorage storage;
    ae::Domain domain{storage};
    auto catalog = BuildWindowCatalogGraph(domain, handling);
    std::vector<ae::Ptr<ae::Obj>> nodes;
    CollectReachableNodeRefs(*catalog, nodes);
    for (auto const& node : nodes) static_cast<Node&>(*node).SetImmediateEventCompaction(true);
    auto& state = static_cast<CatalogUiState&>(*catalog->persisted.device_state);
    CHECK(state.menu->object_scope().is_constant);
    CHECK(state.menu->groups.size() == (handling == CatalogThemeHandling::kAdapterAppliesTheme ? 2 : 1));
    auto const portable_generation = catalog->Generation();
    unsigned const language_group = static_cast<unsigned>(state.menu->groups.size() - 1);
    state.ExecuteMenuCommand(language_group, 1);
    CHECK(catalog->persisted.locale->language_index() == 1);
    CHECK(catalog->persisted.locale->object_scope().root_id == state.obj_id);
    CHECK(catalog->persisted.locale->journal.empty());
    for (auto const& group : state.menu->groups) {
      CHECK(!group.label->strings[0].is_loaded());
      CHECK(group.label->strings[1].is_loaded());
    }
    if (handling == CatalogThemeHandling::kAdapterAppliesTheme) {
      state.ExecuteMenuCommand(0, 3);
      CHECK(state.appearance == (ui::UiAppearancePreferences{ui::UiThemeMode::kDark,true}));
      state.ExecuteMenuCommand(0, 0);
      CHECK(state.appearance == ui::UiAppearancePreferences{});
      CHECK(state.journal.empty());
    } else {
      state.ExecuteMenuCommand(1, 3); // No theme command exists in this supply.
      CHECK(state.appearance == ui::UiAppearancePreferences{});
    }
    CHECK(catalog->Generation() == portable_generation);
  }
  std::cout << "Catalog menu capabilities/LOCAL/folding/contrast PASS\n";
}
