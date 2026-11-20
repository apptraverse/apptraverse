#include "window_catalog_supply_constant_ids.h"

#include "window_catalog_model.h"
#include "window_catalog_ids.h"
#include "window_catalog_menu.h"

#include "apptraverse/ui/ui_localization_string.h"

namespace apptraverse {
namespace {

void AppendLocalizationStrings(ui::UiLocalizationString const& localization,
                               std::vector<ae::ObjId>& out) {
  out.push_back(localization.obj_id);
  out.push_back(localization.base.id());
  for (auto const& entry : localization.strings) {
    out.push_back(entry.id());
  }
  // Historical capture keeps Node identities but snapshots ordinary UiString
  // leaves at private identities. Those base-owned leaves are supply too.
  localization.base.Load();
  auto const& historical = static_cast<ui::UiLocalizationString const&>(*localization.base);
  for (auto const& entry : historical.strings) out.push_back(entry.id());
}

void AppendLocalizationRef(ui::UiLocalizationString::ptr const& localized,
                           std::vector<ae::ObjId>& out) {
  localized.Load();
  AppendLocalizationStrings(*localized, out);
}

}  // namespace

std::vector<ae::ObjId> CollectWindowCatalogSupplyConstantIds(
    WindowCatalog const& catalog) {
  std::vector<ae::ObjId> ids;
  catalog.persisted.main_window.Load();
  ids.push_back(catalog.persisted.main_window->layout_state->definition.id());
  ids.push_back(catalog.persisted.child_layout_definition.id());
  auto const& texts = catalog.persisted.texts;
  AppendLocalizationRef(texts.label_add, ids);
  AppendLocalizationRef(texts.label_open, ids);
  AppendLocalizationRef(texts.label_delete, ids);
  if (texts.label_language.id().is_valid()) AppendLocalizationRef(texts.label_language, ids);
  AppendLocalizationRef(texts.window_title_prefix, ids);
  AppendLocalizationRef(texts.label_close, ids);
  AppendLocalizationRef(texts.list_row_title_format, ids);
  AppendLocalizationRef(texts.list_row_title_closed_suffix, ids);
  catalog.persisted.main_window.Load();
  auto const& main_title =
      static_cast<ui::UiPresentedWindow const&>(*catalog.persisted.main_window)
          .presenter->title;
  AppendLocalizationRef(main_title, ids);
  catalog.persisted.device_state.Load();
  // Menus are an authored optional component: headless/common supplies can use
  // the plain device state, while menu supplies own this immutable LOCAL graph.
  if (ae::Registry::GetRegistry().GenerationDistance(
          CatalogUiState::kClassId, catalog.persisted.device_state->GetClassId()) >= 0) {
    auto const& state = static_cast<CatalogUiState const&>(*catalog.persisted.device_state);
    state.menu.Load();
    ids.push_back(state.menu.id());
    ids.push_back(state.menu->base.id());
    for (auto const& group : state.menu->groups) {
      AppendLocalizationRef(group.label, ids);
      for (auto const& item : group.items) AppendLocalizationRef(item.label, ids);
    }
    for (auto const& item : state.menu->layout_items) AppendLocalizationRef(item.label, ids);
    for (auto const& text : state.menu->shell_strings) AppendLocalizationRef(text, ids);
    if (state.menu->GetClassId() == CatalogWindowDisplayMenuDefinition::kClassId) {
      for (auto const& item : state.DisplayMenu().fullscreen_items)
        AppendLocalizationRef(item.label, ids);
    }
  }
  return ids;
}

}  // namespace apptraverse
