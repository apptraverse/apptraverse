#pragma once

#include "window_catalog_model.h"

namespace apptraverse {

// Supply-authoring policy of a backend, not a user preference. Native-following
// adapters (e.g. AppKit) omit the theme menu entirely. No simulated OS settings.
enum class CatalogThemeHandling : std::uint8_t { kNativeFollowsSystem, kAdapterAppliesTheme };
enum class CatalogMenuAction : std::uint8_t { kLanguage, kTheme, kLayoutFlow, kResetLayout };
struct CatalogMenuItem {
  ui::UiLocalizationString::ptr label;
  CatalogMenuAction action;
  int value;
  AE_REFLECT_MEMBERS(label, action, value)
};
struct CatalogMenuGroup {
  ui::UiLocalizationString::ptr label;
  std::vector<CatalogMenuItem> items;
  AE_REFLECT_MEMBERS(label, items)
};
class CatalogMenuDefinition : public NodeFor<CatalogMenuDefinition> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::CatalogMenuDefinition", CatalogMenuDefinition, Node, 0)
 protected:
  CatalogMenuDefinition() = default;
 public:
  explicit CatalogMenuDefinition(ae::ObjProp p) : NodeFor{p} {}
  std::vector<CatalogMenuGroup> groups;
  std::vector<CatalogMenuItem> layout_items;
  std::vector<ui::UiLocalizationString::ptr> shell_strings;
  void MaterializeLanguage(int language);
  AE_OBJECT_REFLECT(AE_MMBR(groups), AE_MMBR(layout_items), AE_MMBR(shell_strings))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, groups, layout_items, shell_strings); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, groups, layout_items, shell_strings); }
};
// Menu-enabled adapters opt into this LOCAL root during supply composition.
// Only language and existing appearance preferences persist here.
class CatalogUiState : public NodeFor<CatalogUiState, ui::UiDeviceState> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::CatalogUiState", CatalogUiState, ui::UiDeviceState, 0)
 protected:
  CatalogUiState() = default;
 public:
  explicit CatalogUiState(ae::ObjProp p) : NodeFor{p} {}
  WindowCatalog::ptr catalog;
  CatalogMenuDefinition::ptr menu;
  AE_OBJECT_REFLECT(AE_MMBR(catalog), AE_MMBR(menu))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, catalog, menu); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, catalog, menu); }
  void CollectHistoricalCaptureLiveReferenceIds(std::set<ae::ObjId>& ids) const override {
    ids.insert(catalog.id()); ids.insert(menu.id());
  }
  void ExecuteMenuCommand(unsigned group, unsigned item);
  void ExecuteLayoutCommand(unsigned item);
  bool MenuItemSelected(CatalogMenuItem const& item) const;
};

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain&, CatalogThemeHandling);
void ComposeCatalogMenu(ae::Domain&, WindowCatalog&, CatalogThemeHandling,
                        bool web_shell = false, bool layout_context = false);
void PrepareCatalogMenuForRun(WindowCatalog&);
void EnsureCatalogMenuRegistration();

}  // namespace apptraverse
