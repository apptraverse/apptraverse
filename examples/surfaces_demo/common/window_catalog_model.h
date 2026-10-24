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

namespace apptraverse {

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

  AE_OBJECT_REFLECT()

  template <typename Dnv>

  void Load(ae::Version<0>, Dnv& dnv) {

    dnv(base_);

  }

  template <typename Dnv>

  void Save(ae::Version<0>, Dnv& dnv) const {

    dnv(base_);

  }

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

struct WindowCatalogTexts {

  ae::ObjPtr<ui::UiLocalizationString> label_add;

  ae::ObjPtr<ui::UiLocalizationString> label_open;

  ae::ObjPtr<ui::UiLocalizationString> label_delete;

  ae::ObjPtr<ui::UiLocalizationString> label_language;

  ae::ObjPtr<ui::UiLocalizationString> window_title_prefix;

  AE_REFLECT_MEMBERS(label_add, label_open, label_delete, label_language,

                     window_title_prefix)

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

class WindowCatalog : public NodeFor<WindowCatalog> {

  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::WindowCatalog",

                           WindowCatalog, Node, 0)

 protected:

  WindowCatalog() : pending_focus_window_id{} {}

 public:

  explicit WindowCatalog(ae::ObjProp prop) : NodeFor{prop}, next_window_number{1}, pending_focus_window_id{} {}

  AE_OBJECT_REFLECT(AE_MMBR(next_window_number), AE_MMBR(selected),

                    AE_MMBR(list), AE_MMBR(main_window), AE_MMBR(locale),

                    AE_MMBR(texts), AE_MMBR(buttons), AE_MMBR(presenters))

  template <typename Dnv>

  void Load(ae::Version<0>, Dnv& dnv) {

    dnv(base_, next_window_number, selected, list, main_window, locale,

        texts.label_add, texts.label_open, texts.label_delete,

        texts.label_language, texts.window_title_prefix, buttons.add_button,

        buttons.open_button, buttons.delete_button, buttons.language_button,

        presenters.list_presenter, presenters.add_button_presenter,

        presenters.open_button_presenter, presenters.delete_button_presenter,

        presenters.language_button_presenter);

  }

  template <typename Dnv>

  void Save(ae::Version<0>, Dnv& dnv) const {

    dnv(base_, next_window_number, selected, list, main_window, locale,

        texts.label_add, texts.label_open, texts.label_delete,

        texts.label_language, texts.window_title_prefix, buttons.add_button,

        buttons.open_button, buttons.delete_button, buttons.language_button,

        presenters.list_presenter, presenters.add_button_presenter,

        presenters.open_button_presenter, presenters.delete_button_presenter,

        presenters.language_button_presenter);

  }

  std::uint32_t next_window_number;

  WindowHolder::ptr selected;

  ae::ObjPtr<CatalogListContainer> list;

  ui::UiDesktopWindow::ptr main_window;

  ae::ObjPtr<ui::UiLocaleSettings> locale;

  WindowCatalogTexts texts;

  WindowCatalogButtons buttons;

  WindowCatalogPresenters presenters;

  [[nodiscard]] bool CanApply(AddWindowCatalogEntryEvent const& event) const;

  void Apply(AddWindowCatalogEntryEvent const& event);

  void Apply(RemoveWindowCatalogEntryEvent const& event);

  void Apply(SetWindowCatalogSelectionEvent const& event);

  void Apply(SetWindowCatalogOpenEvent const& event);

  void AddWindow();

  void OpenSelected();

  void OpenRow(std::size_t row_index);

  void DeleteSelected();

  void UserCloseWindow(ae::ObjId window_obj_id);

  void SelectHolder(WindowHolder::ptr holder);

  [[nodiscard]] WindowHolder* FindHolderForWindow(ae::ObjId window_id);

  [[nodiscard]] WindowHolder* FindHolderForWindow(CatalogWindow const& window);

  [[nodiscard]] std::size_t RowIndexForHolder(WindowHolder const& holder) const;

  void OpenHolderWindow(WindowHolder& holder);

  void CloseHolderWindow(WindowHolder& holder);

  void MaterializeCatalogStrings(int language_index);

  void MaterializeHolderListText(WindowHolder& holder, int language_index);

  void SubmitSelectionFromUi(std::size_t row_index);

  void OpenListRowFromUi(std::size_t row_index);

  void ClearPendingFocusWindow();

  void RefreshChromeButtonEnabledState();

  // Model-thread one-shot focus request; copied to UI session on publish.

  std::uint32_t pending_focus_window_id;

};

void SaveWindowCatalogPersistentSnapshot(WindowCatalog& catalog);

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

void AssignInitialCatalogWindowBounds(CatalogWindow& window,

                                      std::uint32_t number);

void EnsureWindowCatalogModelRegistration();

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_MODEL_H_

