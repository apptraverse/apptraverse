#pragma once

#include <string>
#include <vector>
#include <utility>

#include "window_catalog_menu.h"

namespace apptraverse::example::surfaces {

// Runtime-only native menu projection. Viewport/OS observations do not change
// this value, so they must not clear an open Android menu or submenu.
struct AndroidCatalogMenuItemProjection {
  std::string title;
  CatalogMenuAction action{};
  int value{};
  bool selected{};
  bool operator==(AndroidCatalogMenuItemProjection const&) const = default;
};
struct AndroidCatalogMenuGroupProjection {
  std::string title;
  std::vector<AndroidCatalogMenuItemProjection> items;
  bool operator==(AndroidCatalogMenuGroupProjection const&) const = default;
};
struct AndroidCatalogMenuProjection {
  ae::ObjId catalog_id, state_id, menu_id, locale_id, layout_id;
  ae::ObjId active_window_id, main_window_id;
  std::vector<AndroidCatalogMenuGroupProjection> groups;
  std::vector<AndroidCatalogMenuItemProjection> layout_items;
  AndroidCatalogMenuItemProjection fullscreen;
  bool fullscreen_intent{};
  bool operator==(AndroidCatalogMenuProjection const&) const = default;
};

inline std::string AndroidCatalogMenuCaption(ui::UiLocalizationString const& text,
                                             int language) {
  // Android has no menu mnemonic marker. Preserve escaped literal '&'.
  auto const source = text.Resolve(language);
  std::string result;
  for (std::size_t index = 0; index < source.size(); ++index) {
    if (source[index] != '&') result += source[index];
    else if (index + 1 < source.size() && source[index + 1] == '&') {
      result += '&';
      ++index;
    }
  }
  return result;
}

inline AndroidCatalogMenuProjection BuildAndroidCatalogMenuProjection(
    WindowCatalog& catalog) {
  auto const& state = static_cast<CatalogUiState const&>(*catalog.persisted.device_state);
  int const language = catalog.persisted.locale->language_index();
  AndroidCatalogMenuProjection result;
  result.catalog_id = catalog.obj_id;
  result.state_id = state.obj_id;
  result.menu_id = state.menu.id();
  result.locale_id = catalog.persisted.locale.id();
  result.layout_id = catalog.persisted.main_window->layout_state.id();
  result.active_window_id = catalog.ResolveActiveWindow().id();
  result.main_window_id = catalog.persisted.main_window.id();
  auto item = [&](CatalogMenuItem const& value) {
    return AndroidCatalogMenuItemProjection{
        AndroidCatalogMenuCaption(*value.label, language), value.action,
        value.value, state.MenuItemSelected(value)};
  };
  for (auto const& group : state.menu->groups) {
    AndroidCatalogMenuGroupProjection projected;
    projected.title = AndroidCatalogMenuCaption(*group.label, language);
    for (auto const& value : group.items) projected.items.push_back(item(value));
    result.groups.push_back(std::move(projected));
  }
  if (result.active_window_id == result.main_window_id)
    for (auto const& value : state.menu->layout_items) result.layout_items.push_back(item(value));
  bool const fullscreen = state.IsWindowFullscreen(result.active_window_id);
  result.fullscreen = item(state.DisplayMenu().fullscreen_items[fullscreen ? 1 : 0]);
  result.fullscreen.selected = false; // Native fullscreen row is an action, not a check.
  result.fullscreen_intent = !fullscreen;
  return result;
}

}  // namespace apptraverse::example::surfaces
