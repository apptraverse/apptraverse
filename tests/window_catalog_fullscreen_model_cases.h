#pragma once

#include <algorithm>
#include <deque>
#include "window_catalog_graph_builder.h"
#include "window_catalog_prepare.h"
#include "window_catalog_supply_constant_ids.h"
#include "apptraverse/distill.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/overlay_domain_storage.h"

namespace {
WindowCatalog::ptr BuildMenuModel(ae::Domain& domain, bool display) {
  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& d, ae::ObjId id, WindowCatalog::ptr const&) {
    return ui::UiDesktopWindowPresenter::ptr::Create(ae::CreateWith{d}.with_id(id));
  };
  factory.create_list = [](ae::Domain& d, ae::ObjId id, WindowCatalog::ptr const&) {
    return ui::UiListContainerPresenter::ptr::Create(ae::CreateWith{d}.with_id(id));
  };
  factory.create_button = [](ae::Domain& d, ae::ObjId id) {
    return ui::UiPushButtonPresenter::ptr::Create(id.is_valid()
        ? ae::CreateWith{d}.with_id(id) : ae::CreateWith{d});
  };
  WindowCatalogGraphOptions options;
  options.language_button = false;
  options.create_device = [](ae::Domain& d, ae::ObjId id) {
    auto state = CatalogUiState::ptr::Create(ae::CreateWith{d}.with_id(id));
    state->catalog = {}; state->menu = {}; state->fullscreen_windows.clear();
    return ui::UiDeviceState::ptr{state};
  };
  auto catalog = BuildWindowCatalogGraph(domain, factory, std::move(options));
  // Non-display shape matches the existing Linux/Android/iOS menu composition.
  ComposeCatalogMenu(domain, *catalog, CatalogThemeHandling::kAdapterAppliesTheme,
                     false, display, display);
  return catalog;
}

void TestFullscreenModelAndPublication(bool compact) {
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  auto catalog = BuildMenuModel(domain, true);
  PrepareWindowCatalogForRun(*catalog);
  catalog->EnableLocalWindowLifecycle();
  auto& state = static_cast<CatalogUiState&>(*catalog->persisted.device_state);
  state.SetImmediateEventCompaction(compact);
  auto const main = catalog->persisted.main_window.id();
  CHECK(state.DisplayMenu().fullscreen_items.size() == 2);
  CHECK(state.DisplayMenu().fullscreen_toolbar.children[0].control_id == 5300);
  CHECK(state.DisplayMenu().fullscreen_items[0].label->Resolve(0) == "Enter fullscreen (F11)");
  state.ExecuteMenuCommand(1, 1);
  CHECK(state.DisplayMenu().fullscreen_items[1].label->Resolve(1) ==
        "Выйти из полноэкранного режима (Esc)");
  CHECK(!state.DisplayMenu().fullscreen_items[1].label->strings[0].is_loaded());
  CHECK(state.DisplayMenu().fullscreen_items[1].label->strings[1].is_loaded());
  auto const constants = CollectWindowCatalogSupplyConstantIds(*catalog);
  CHECK(std::find(constants.begin(), constants.end(),
      state.DisplayMenu().fullscreen_items[1].label.id()) != constants.end());

  ByteSink initial;
  SerializeInitialPublication(*catalog, initial);
  ae::RamDomainStorage ui_storage;
  ae::Domain ui_domain{ui_storage};
  ByteSource initial_source{initial.bytes.data(), initial.bytes.size()};
  auto mirror = LoadInitialPublication(initial_source, ui_domain, ui_storage);
  auto& ui_catalog = static_cast<WindowCatalog&>(*mirror);
  auto& ui_state = static_cast<CatalogUiState&>(*ui_catalog.persisted.device_state);
  CHECK(!ui_state.IsWindowFullscreen(main));
  std::deque<ModelObjectProxy::ModelWork> pending;
  ModelObjectProxy proxy{[&](ModelObjectProxy::ModelWork work) { pending.push_back(std::move(work)); }};
  auto drain = [&] {
    while (!pending.empty()) {
      auto work = std::move(pending.front()); pending.pop_front(); work(domain);
    }
  };
  auto const catalog_generation = catalog->Generation();
  proxy.Invoke(state.obj_id, &CatalogUiState::RequestWindowFullscreenFromUi, main, true);
  CHECK(!state.IsWindowFullscreen(main) && !ui_state.IsWindowFullscreen(main));
  drain();
  CHECK(state.IsWindowFullscreen(main) && !ui_state.IsWindowFullscreen(main));
  CHECK(catalog->Generation() == catalog_generation);
  CHECK(state.journal.empty() == compact);
  if (!compact) {
    CHECK(state.journal.back().event->object_scope().root_id == state.obj_id);
    CHECK(state.TryReplayFromBase());
    CHECK(state.IsWindowFullscreen(main));
  }
  ByteSink changed;
  SerializeIncrementalNodePublication(state, changed);
  ByteSource changed_source{changed.bytes.data(), changed.bytes.size()};
  ApplyIncrementalPublication(changed_source, ui_domain, ui_storage);
  CHECK(ui_state.IsWindowFullscreen(main));
  auto const state_generation = state.Generation();
  state.RequestWindowFullscreenFromUi(main, true);
  CHECK(state.Generation() == state_generation);
  state.ToggleWindowFullscreenFromUi(main);
  CHECK(!state.IsWindowFullscreen(main) && state.fullscreen_windows.empty());

  catalog->AddWindow();
  auto child = WindowHolder::ptr::MakeFromThis(
      static_cast<WindowHolder*>(&*catalog->persisted.list->rows[0]));
  auto const child_id = child->window.id();
  CHECK(child->window->device_state.id() == state.obj_id);
  CHECK(!state.IsWindowFullscreen(child_id));
  state.RequestWindowFullscreenFromUi(child_id, true);
  CHECK(state.IsWindowFullscreen(child_id));
  catalog->AddWindow();
  auto sibling = WindowHolder::ptr::MakeFromThis(
      static_cast<WindowHolder*>(&*catalog->persisted.list->rows[1]));
  CHECK(sibling->window.id() != child_id);
  CHECK(sibling->window->device_state.id() == state.obj_id);
  CHECK(!state.IsWindowFullscreen(sibling->window.id()));
  catalog->CloseHolderWindow(*child);
  auto const closed_generation = state.Generation();
  state.RequestWindowFullscreenFromUi(child_id, false);
  CHECK(state.Generation() == closed_generation && state.IsWindowFullscreen(child_id));
  CHECK(!child->window.is_loaded());
  catalog->OpenHolderWindow(*child);
  catalog->OpenHolderWindow(*child);
  CHECK(child->window.id() == child_id && state.IsWindowFullscreen(child_id));
  proxy.Invoke(state.obj_id, &CatalogUiState::RequestWindowFullscreenFromUi, child_id, true);
  catalog->SelectHolder(child);
  catalog->DeleteSelected();
  CHECK(!state.IsWindowFullscreen(child_id));
  auto const deleted_generation = state.Generation();
  drain();
  CHECK(state.Generation() == deleted_generation);
  CHECK(!state.WindowStateIds().contains(child_id));
}
struct MenuSupply {
  ae::RamDomainStorage data;
  ae::ObjId root;
  std::set<ae::ObjId> constants;
  explicit MenuSupply(bool display) {
    ae::Domain domain{data};
    auto catalog = BuildMenuModel(domain, display);
    root = catalog.id();
    auto const ids = CollectWindowCatalogSupplyConstantIds(*catalog);
    constants.insert(ids.begin(), ids.end());
    FinalizeDistilledGraph(*catalog);
    SaveDistilledRoot(*catalog);
  }
};

void TestFullscreenColdAndPlatformSeed(bool source_display, bool compact) {
  MenuSupply source{source_display}, destination{!source_display};
  auto const original_supply = source.data.state;
  ae::RamDomainStorage user, local;
  ae::ObjId main, child;
  {
    ClientLocalDomainStorage routed{user, local, source.data, CollectClientLocalRoots(source.data)};
    OverlayDomainStorage overlay{routed, source.data, source.constants};
    ae::Domain domain{overlay};
    auto catalog = LoadApplication<WindowCatalog>(domain, source.root);
    PrepareWindowCatalogForRun(*catalog); PrepareCatalogMenuForRun(*catalog);
    auto& state = static_cast<CatalogUiState&>(*catalog->persisted.device_state);
    state.SetImmediateEventCompaction(compact);
    main = catalog->persisted.main_window.id();
    state.SetAppearance({ui::UiThemeMode::kDark, true});
    state.RequestWindowFullscreenFromUi(main, true);
    CHECK(state.IsWindowFullscreen(main) == source_display);
    catalog->AddWindow();
    child = static_cast<WindowHolder&>(*catalog->persisted.list->rows[0]).window.id();
    state.RequestWindowFullscreenFromUi(child, true);
    CHECK(state.IsWindowFullscreen(child) == source_display);
    SaveWindowCatalogPersistentSnapshot(*catalog);
    CHECK(local.Load({state.obj_id, ui::UiDeviceState::kClassId, 0}).result == ae::DomainLoadResult::kLoaded);
    CHECK(user.Load({state.obj_id, ui::UiDeviceState::kClassId, 0}).result == ae::DomainLoadResult::kEmpty);
    CHECK(user.Load({state.obj_id, CatalogUiState::kClassId, 0}).result == ae::DomainLoadResult::kEmpty);
    for (auto const& [id, _] : local.state) {
      auto const scope = local.ReadObjectScope(id);
      CHECK(scope && scope->root_id == state.obj_id);
    }
  }
  CHECK(source.data.state == original_supply);
  // Cold load with matching supply restores both folded and journal state.
  {
    ClientLocalDomainStorage routed{user, local, source.data, CollectClientLocalRoots(source.data)};
    OverlayDomainStorage overlay{routed, source.data, source.constants};
    ae::Domain domain{overlay};
    auto catalog = LoadApplication<WindowCatalog>(domain, source.root);
    PrepareWindowCatalogForRun(*catalog); PrepareCatalogMenuForRun(*catalog);
    auto& state = static_cast<CatalogUiState&>(*catalog->persisted.device_state);
    CHECK(state.IsWindowFullscreen(main) == source_display);
    CHECK(state.IsWindowFullscreen(child) == source_display);
    CHECK(state.appearance.soften_dark);
  }
  // USER alone carries the catalog/child identities and none of the LOCAL
  // fullscreen preferences, even when the destination supports fullscreen.
  {
    ae::RamDomainStorage empty_local;
    ClientLocalDomainStorage routed{user, empty_local, source.data, CollectClientLocalRoots(source.data)};
    OverlayDomainStorage overlay{routed, source.data, source.constants};
    ae::Domain domain{overlay};
    auto catalog = LoadApplication<WindowCatalog>(domain, source.root);
    PrepareWindowCatalogForRun(*catalog); PrepareCatalogMenuForRun(*catalog);
    auto& state = static_cast<CatalogUiState&>(*catalog->persisted.device_state);
    CHECK(catalog->persisted.list->rows.size() == 1);
    CHECK(static_cast<WindowHolder&>(*catalog->persisted.list->rows[0]).window.id() == child);
    CHECK(!state.IsWindowFullscreen(main) && !state.IsWindowFullscreen(child));
  }
  ae::RamDomainStorage seeded;
  SeedClientLocalStorage(seeded, local);
  CHECK(seeded.state == local.state);
  {
    ClientLocalDomainStorage routed{user, seeded, destination.data, CollectClientLocalRoots(destination.data)};
    OverlayDomainStorage overlay{routed, destination.data, destination.constants};
    ae::Domain domain{overlay};
    auto catalog = LoadApplication<WindowCatalog>(domain, destination.root);
    PrepareWindowCatalogForRun(*catalog); PrepareCatalogMenuForRun(*catalog);
    auto& state = static_cast<CatalogUiState&>(*catalog->persisted.device_state);
    CHECK(state.GetClassId() == CatalogUiState::kClassId);
    CHECK(state.appearance.soften_dark);
    CHECK(state.menu->GetClassId() == (!source_display
        ? CatalogWindowDisplayMenuDefinition::kClassId : CatalogMenuDefinition::kClassId));
    CHECK(state.IsWindowFullscreen(main) == source_display);
    CHECK(state.IsWindowFullscreen(child) == source_display);
    auto const generation = state.Generation();
    state.RequestWindowFullscreenFromUi(main, !source_display);
    if (source_display) CHECK(state.Generation() == generation); // Destination has no display capability.
    else CHECK(state.IsWindowFullscreen(main));
  }
  auto const own_local = seeded.state;
  SeedClientLocalStorage(seeded, local);
  CHECK(seeded.state == own_local);
}
}  // namespace
