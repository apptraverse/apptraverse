#ifndef APPTRAVERSE_WINDOW_CATALOG_MODEL_H_

#define APPTRAVERSE_WINDOW_CATALOG_MODEL_H_

#include <cstdint>

#include <vector>

#include "aether-miscpp/reflect/reflect.h"

#include "apptraverse/event_for.h"

#include "apptraverse/node_for.h"

#include "apptraverse/object_macros.h"

#include "apptraverse/presenter.h"

#include "apptraverse/ui/ui_desktop_window.h"

#include "apptraverse/ui/ui_list_container.h"

#include "apptraverse/ui/ui_locale_settings.h"

#include "apptraverse/ui/ui_localization_string.h"

#include "apptraverse/ui/ui_presented_window.h"

#include "apptraverse/ui/ui_push_button.h"

#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse {

namespace example::surfaces {

class CatalogCloseButton;

}  // namespace example::surfaces

class CatalogListContainer;

class WindowCatalog;

class WindowHolder;

class CatalogWindow;

class CatalogWindow : public ui::UiDesktopWindow {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogWindow",

                           CatalogWindow, ui::UiDesktopWindow, 0)

 protected:

  CatalogWindow() = default;

 public:

  explicit CatalogWindow(ae::ObjProp prop) : UiDesktopWindow{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(close_button), AE_MMBR(close_button_presenter))

  template <typename Dnv>

  void Load(ae::Version<0>, Dnv& dnv) {

    dnv(base_, close_button, close_button_presenter);

  }

  template <typename Dnv>

  void Save(ae::Version<0>, Dnv& dnv) const {

    dnv(base_, close_button, close_button_presenter);

  }

  // Mobile pager child windows only: Close lives in the window prefab graph.
  ae::ObjPtr<example::surfaces::CatalogCloseButton> close_button;

  ui::UiPushButtonPresenter::ptr close_button_presenter;

};

class WindowHolder : public ui::UiListRow {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::WindowHolder",

                           WindowHolder, ui::UiListRow, 0)

 protected:

  WindowHolder() = default;

 public:

  explicit WindowHolder(ae::ObjProp prop) : UiListRow{prop}, window_number{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_number), AE_MMBR(window))

  template <typename Dnv>

  void Load(ae::Version<0>, Dnv& dnv) {

    dnv(base_, window_number, window);

  }

  template <typename Dnv>

  void Save(ae::Version<0>, Dnv& dnv) const {

    dnv(base_, window_number, window);

  }

  std::uint32_t window_number;

  CatalogWindow::ptr window;

};

class AddWindowCatalogEntryEvent;

class RemoveWindowCatalogEntryEvent;

class SetWindowCatalogSelectionEvent;

class SetWindowCatalogOpenEvent;

class SetWindowCatalogDesktopZOrderEvent;

class SetWindowCatalogMobileActiveEvent;

class SetWindowCatalogMainClientViewportEvent;

struct WindowCatalogTexts {

  ae::ObjPtr<ui::UiLocalizationString> label_add;

  ae::ObjPtr<ui::UiLocalizationString> label_open;

  ae::ObjPtr<ui::UiLocalizationString> label_delete;

  ae::ObjPtr<ui::UiLocalizationString> label_language;

  ae::ObjPtr<ui::UiLocalizationString> window_title_prefix;

  ae::ObjPtr<ui::UiLocalizationString> label_close;

  ae::ObjPtr<ui::UiLocalizationString> list_row_title_format;

  ae::ObjPtr<ui::UiLocalizationString> list_row_title_closed_suffix;

  AE_REFLECT_MEMBERS(label_add, label_open, label_delete, label_language,

                     window_title_prefix, label_close, list_row_title_format,

                     list_row_title_closed_suffix)

};

struct WindowCatalogButtons {

  ae::ObjPtr<ui::UiPushButton> add_button;

  ae::ObjPtr<ui::UiPushButton> open_button;

  ae::ObjPtr<ui::UiPushButton> delete_button;

  ae::ObjPtr<ui::UiPushButton> language_button;

  AE_REFLECT_MEMBERS(add_button, open_button, delete_button, language_button)

};

struct WindowCatalogPresenters {

  Presenter::ptr list_presenter;

  Presenter::ptr add_button_presenter;

  Presenter::ptr open_button_presenter;

  Presenter::ptr delete_button_presenter;

  Presenter::ptr language_button_presenter;

  AE_REFLECT_MEMBERS(list_presenter, add_button_presenter,

                     open_button_presenter, delete_button_presenter,

                     language_button_presenter)

};

struct WindowCatalogMainClientViewport {
  std::int32_t width{0};
  std::int32_t height{0};
  // DisplayMetrics.densityDpi on Android (160 = mdpi); 160 on Web/Desktop.
  std::int32_t density_dpi{160};

  AE_REFLECT_MEMBERS(width, height, density_dpi)
};

// Serialized snapshot of the catalog chrome graph. Kept as one reflected member
// on WindowCatalog so MSVC does not flatten a very wide AE_OBJECT_REFLECT list
// into an initializer nest that hits compiler limits (C1054).
struct WindowCatalogPersisted {
  std::uint32_t next_window_number{1};

  WindowHolder::ptr selected;

  ae::ObjPtr<CatalogListContainer> list;

  CatalogWindow::ptr main_window;

  ae::ObjPtr<ui::UiLocaleSettings> locale;

  WindowCatalogTexts texts;

  WindowCatalogButtons buttons;

  WindowCatalogPresenters presenters;

  CatalogWindow::ptr window_prefab;

  std::vector<CatalogWindow::ptr> desktop_z_order;

  bool mobile_pager{false};

  CatalogWindow::ptr mobile_active_window;

  // Main-window client viewport last reported by a presenter (0 = unknown
  // width/height). Drives adaptive chrome layout of the list and the four main
  // buttons.
  WindowCatalogMainClientViewport main_client_viewport;

  AE_REFLECT_MEMBERS(next_window_number, selected, list, main_window, locale,

                     texts, buttons, presenters, window_prefab, desktop_z_order,

                     mobile_pager, mobile_active_window, main_client_viewport)

};

class WindowCatalog : public NodeFor<WindowCatalog> {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::WindowCatalog",

                           WindowCatalog, Node, 0)

 protected:

  WindowCatalog() = default;

 public:

  explicit WindowCatalog(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(persisted))

  template <typename Dnv>

  void Load(ae::Version<0>, Dnv& dnv) {

    dnv(base_, persisted);

  }

  template <typename Dnv>

  void Save(ae::Version<0>, Dnv& dnv) const {

    dnv(base_, persisted);

  }

  WindowCatalogPersisted persisted;

  [[nodiscard]] CatalogWindow::ptr ResolveMobileActiveWindow();

  [[nodiscard]] bool CanApply(AddWindowCatalogEntryEvent const& event) const;

  void Apply(AddWindowCatalogEntryEvent const& event);

  void Apply(RemoveWindowCatalogEntryEvent const& event);

  void Apply(SetWindowCatalogSelectionEvent const& event);

  void Apply(SetWindowCatalogOpenEvent const& event);

  void Apply(SetWindowCatalogDesktopZOrderEvent const& event);

  void Apply(SetWindowCatalogMobileActiveEvent const& event);

  void Apply(SetWindowCatalogMainClientViewportEvent const& event);

  // Presenter -> model: main window client size changed. No-op when unchanged.
  void SubmitMainClientViewportFromUi(std::int32_t width, std::int32_t height,
                                      std::int32_t density_dpi = 160);

  void SubmitHolderChildWindowClientSizeFromUi(std::uint32_t window_obj_id,
                                               std::int32_t width,
                                               std::int32_t height);

  void SetDesktopZOrderFromObjIds(std::vector<std::uint32_t> ordered_ids);

  void SetMobileActiveWindow(CatalogWindow::ptr window);

  [[nodiscard]] std::vector<CatalogWindow::ptr> BuildMobileRing() const;

  [[nodiscard]] CatalogWindow::ptr MobileRingPredecessor(

      CatalogWindow const& window) const;

  void SubmitMobileActiveFromUi(ae::ObjId window_obj_id);

  void SubmitMobileSwipeFromUi(int delta);

  void AddWindow();

  void OpenSelected();

  void OpenRow(std::size_t row_index);

  void DeleteSelected();

  void UserCloseWindow(ae::ObjId window_obj_id);

  void SelectHolder(WindowHolder::ptr holder);

  [[nodiscard]] WindowHolder* FindHolderForWindow(ae::ObjId window_id);

  [[nodiscard]] WindowHolder* FindHolderForWindow(CatalogWindow const& window);

  [[nodiscard]] std::size_t RowIndexForHolder(WindowHolder const& holder);

  void OpenHolderWindow(WindowHolder& holder);

  [[nodiscard]] bool IsHolderWindowOpen(WindowHolder const& holder) const;

  void CloseHolderWindow(WindowHolder& holder);

  void MaterializeCatalogStrings(int language_index);

  void SubmitSelectionFromUi(std::size_t row_index);

  void OpenListRowFromUi(std::size_t row_index);

  void ClearPendingFocusWindow();

  void RefreshChromeButtonEnabledState();

  // Model-thread one-shot focus request (runtime-only); copied to UI on publish.

  CatalogWindow::ptr pending_focus_window;

  CatalogWindow::ptr pending_mobile_active_window;

};

void SaveWindowCatalogPersistentSnapshot(WindowCatalog& catalog);

void RebindWindowCatalogHolderListPrimaryText(WindowCatalog& catalog);

void RestoreLoadedCatalogWindows(WindowCatalog& catalog);

class CatalogListContainer : public ui::UiListContainer {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogListContainer",

                           CatalogListContainer, ui::UiListContainer, 0)

 protected:

  CatalogListContainer() = default;

 public:

  explicit CatalogListContainer(ae::ObjProp prop) : UiListContainer{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog))

  template <typename Dnv>

  void Load(ae::Version<0>, Dnv& dnv) {

    dnv(base_, catalog);

  }

  template <typename Dnv>

  void Save(ae::Version<0>, Dnv& dnv) const {

    dnv(base_, catalog);

  }

  ae::ObjPtr<WindowCatalog> catalog;

  void HandleListRowActivated(std::size_t row_index) override;

};

class AddWindowCatalogEntryEvent

    : public EventFor<WindowCatalog, AddWindowCatalogEntryEvent> {

  APPTRAVERSE_NAMED_OBJECT(

      "apptraverse::example::surfaces::AddWindowCatalogEntryEvent",

      AddWindowCatalogEntryEvent, Event, 0)

 protected:

  AddWindowCatalogEntryEvent() = default;

 public:

  explicit AddWindowCatalogEntryEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(holder))

  WindowHolder::ptr holder;

};

class RemoveWindowCatalogEntryEvent

    : public EventFor<WindowCatalog, RemoveWindowCatalogEntryEvent> {

  APPTRAVERSE_NAMED_OBJECT(

      "apptraverse::example::surfaces::RemoveWindowCatalogEntryEvent",

      RemoveWindowCatalogEntryEvent, Event, 0)

 protected:

  RemoveWindowCatalogEntryEvent() = default;

 public:

  explicit RemoveWindowCatalogEntryEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(holder))

  WindowHolder::ptr holder;

};

class SetWindowCatalogOpenEvent

    : public EventFor<WindowCatalog, SetWindowCatalogOpenEvent> {

  APPTRAVERSE_NAMED_OBJECT(

      "apptraverse::example::surfaces::SetWindowCatalogOpenEvent",

      SetWindowCatalogOpenEvent, Event, 0)

 protected:

  SetWindowCatalogOpenEvent() = default;

 public:

  explicit SetWindowCatalogOpenEvent(ae::ObjProp prop) : EventFor{prop}, open{false} {}

  AE_OBJECT_REFLECT(AE_MMBR(holder), AE_MMBR(open))

  WindowHolder::ptr holder;

  bool open;

};

class SetWindowCatalogDesktopZOrderEvent
    : public EventFor<WindowCatalog, SetWindowCatalogDesktopZOrderEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SetWindowCatalogDesktopZOrderEvent",
      SetWindowCatalogDesktopZOrderEvent, Event, 0)

 protected:
  SetWindowCatalogDesktopZOrderEvent() = default;

 public:
  explicit SetWindowCatalogDesktopZOrderEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(ordered))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, ordered);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, ordered);
  }

  std::vector<CatalogWindow::ptr> ordered;
};

class SetWindowCatalogMobileActiveEvent
    : public EventFor<WindowCatalog, SetWindowCatalogMobileActiveEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SetWindowCatalogMobileActiveEvent",
      SetWindowCatalogMobileActiveEvent, Event, 0)

 protected:
  SetWindowCatalogMobileActiveEvent() = default;

 public:
  explicit SetWindowCatalogMobileActiveEvent(ae::ObjProp prop)
      : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  CatalogWindow::ptr window;
};

class SetWindowCatalogMainClientViewportEvent
    : public EventFor<WindowCatalog, SetWindowCatalogMainClientViewportEvent> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::SetWindowCatalogMainClientViewportEvent",
      SetWindowCatalogMainClientViewportEvent, Event, 0)

 protected:
  SetWindowCatalogMainClientViewportEvent() = default;

 public:
  explicit SetWindowCatalogMainClientViewportEvent(ae::ObjProp prop)
      : EventFor{prop}, width{0}, height{0} {}

  AE_OBJECT_REFLECT(AE_MMBR(width), AE_MMBR(height), AE_MMBR(density_dpi))

  std::int32_t width;
  std::int32_t height;
  std::int32_t density_dpi{160};
};

class SetWindowCatalogSelectionEvent

    : public EventFor<WindowCatalog, SetWindowCatalogSelectionEvent> {

  APPTRAVERSE_NAMED_OBJECT(

      "apptraverse::example::surfaces::SetWindowCatalogSelectionEvent",

      SetWindowCatalogSelectionEvent, Event, 0)

 protected:

  SetWindowCatalogSelectionEvent() = default;

 public:

  explicit SetWindowCatalogSelectionEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(holder))

  WindowHolder::ptr holder;

};

namespace example::surfaces {

// Child-window Close button (mobile pager). Defined here so CatalogWindow can
// own it as part of the window prefab graph.
class CatalogCloseButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogCloseButton",
                           CatalogCloseButton, ui::UiPushButton, 0)

 protected:
  CatalogCloseButton() = default;

 public:
  explicit CatalogCloseButton(ae::ObjProp prop) : UiPushButton{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog), AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog, window);
  }

  WindowCatalog::ptr catalog;
  CatalogWindow::ptr window;

  void HandlePushButtonClicked() override;
};

}  // namespace example::surfaces

void AssignInitialCatalogWindowBounds(CatalogWindow& window,

                                      std::uint32_t number);

void EnsureWindowCatalogModelRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_MODEL_H_

