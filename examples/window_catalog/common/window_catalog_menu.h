#pragma once

#include "window_catalog_model.h"

namespace apptraverse {

// Supply-authoring policy of a backend, not a user preference. Native-following
// adapters (e.g. AppKit) omit the theme menu entirely. No simulated OS settings.
enum class CatalogThemeHandling : std::uint8_t { kNativeFollowsSystem, kAdapterAppliesTheme };
enum class CatalogMenuShell : std::uint8_t { kDefault, kWeb, kWindows, kMac, kIos };
enum class CatalogDisplayHandling : std::uint8_t { kNone, kDesktop, kPaged };
enum class CatalogMenuAction : std::uint8_t { kLanguage, kTheme, kLayoutFlow, kResetLayout, kFullscreen };
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
  virtual void MaterializeLanguage(int language);
  AE_OBJECT_REFLECT(AE_MMBR(groups), AE_MMBR(layout_items), AE_MMBR(shell_strings))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, groups, layout_items, shell_strings); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, groups, layout_items, shell_strings); }
};

// The opt-in desktop display definition is immutable supply data;
// fullscreen choices belong to the device-local state, keyed by window identity.
class CatalogWindowDisplayMenuDefinition
    : public NodeFor<CatalogWindowDisplayMenuDefinition, CatalogMenuDefinition> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::CatalogWindowDisplayMenuDefinition",
                          CatalogWindowDisplayMenuDefinition, CatalogMenuDefinition, 0)
 protected:
  CatalogWindowDisplayMenuDefinition() = default;
 public:
  explicit CatalogWindowDisplayMenuDefinition(ae::ObjProp p) : NodeFor{p} {}
  std::vector<CatalogMenuItem> fullscreen_items;  // enter, exit
  ui::UiLayoutNode fullscreen_toolbar;
  // Windows shell strings are authored after the existing display resources.
  ui::UiLocalizationString const& LoadingText() const { return *shell_strings[0]; }
  AE_OBJECT_REFLECT(AE_MMBR(fullscreen_items), AE_MMBR(fullscreen_toolbar))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, fullscreen_items, fullscreen_toolbar); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, fullscreen_items, fullscreen_toolbar); }
  void MaterializeLanguage(int language) override;
};

class SetCatalogWindowFullscreenEvent;

// Menu-enabled adapters opt into this LOCAL root during supply composition.
// Per-window display choices remain device-local and are enabled by supply.
class CatalogUiState : public NodeFor<CatalogUiState, ui::UiDeviceState> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::CatalogUiState", CatalogUiState, ui::UiDeviceState, 0)
 protected:
  CatalogUiState() = default;
  void ForgetWindowState(ae::ObjId window_id) override;
 public:
  explicit CatalogUiState(ae::ObjProp p) : NodeFor{p} {}
  WindowCatalog::ptr catalog;
  CatalogMenuDefinition::ptr menu;
  std::vector<ae::ObjId> fullscreen_windows;
  AE_OBJECT_REFLECT(AE_MMBR(catalog), AE_MMBR(menu), AE_MMBR(fullscreen_windows))
  template<class D> void Load(ae::Version<0>, D& d) { d(base_, catalog, menu, fullscreen_windows); }
  template<class D> void Save(ae::Version<0>, D& d) const { d(base_, catalog, menu, fullscreen_windows); }
  void CollectHistoricalCaptureLiveReferenceIds(std::set<ae::ObjId>& ids) const override {
    ids.insert(catalog.id()); ids.insert(menu.id());
  }
  using ui::UiDeviceState::Apply;
  void Apply(SetCatalogWindowFullscreenEvent const&);
  bool IsWindowFullscreen(ae::ObjId window_id) const;
  void RequestWindowFullscreenFromUi(ae::ObjId window_id, bool fullscreen);
  void ToggleWindowFullscreenFromUi(ae::ObjId window_id);
  std::set<ae::ObjId> WindowStateIds() const override;
  CatalogWindowDisplayMenuDefinition const& DisplayMenu() const {
    return static_cast<CatalogWindowDisplayMenuDefinition const&>(*menu);
  }
  void ExecuteMenuCommand(unsigned group, unsigned item);
  void ExecuteLayoutCommand(unsigned item);
  bool MenuItemSelected(CatalogMenuItem const& item) const;
};

class SetCatalogWindowFullscreenEvent
    : public EventFor<CatalogUiState, SetCatalogWindowFullscreenEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::SetCatalogWindowFullscreenEvent",
                          SetCatalogWindowFullscreenEvent, Event, 0)
 protected:
  SetCatalogWindowFullscreenEvent() = default;
 public:
  explicit SetCatalogWindowFullscreenEvent(ae::ObjProp p) : EventFor{p} {}
  ae::ObjId window_id;
  bool fullscreen;
  AE_OBJECT_REFLECT(AE_MMBR(window_id), AE_MMBR(fullscreen))
};

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain&, CatalogThemeHandling);
void ComposeCatalogMenu(ae::Domain&, WindowCatalog&, CatalogThemeHandling,
                        CatalogMenuShell shell = CatalogMenuShell::kDefault,
                        bool layout_context = false,
                        CatalogDisplayHandling display = CatalogDisplayHandling::kNone);
void PrepareCatalogMenuForRun(WindowCatalog&);
void EnsureCatalogMenuRegistration();

}  // namespace apptraverse
