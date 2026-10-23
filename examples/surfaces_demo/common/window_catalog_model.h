#ifndef APPTRAVERSE_WINDOW_CATALOG_MODEL_H_
#define APPTRAVERSE_WINDOW_CATALOG_MODEL_H_

#include <cstdint>
#include <vector>

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/presenter.h"

#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_list_container.h"
#include "apptraverse/ui/ui_locale_settings.h"
#include "apptraverse/ui/ui_localization_string.h"
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

  AE_OBJECT_REFLECT(AE_MMBR(window_presenter), AE_MMBR(title))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, window_presenter, title);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, window_presenter, title);
  }

  ui::windows::WinDesktopWindowPresenter::ptr window_presenter;
  ae::ObjPtr<ui::UiLocalizationString> title;
};

class WindowHolder : public ui::UiListRow {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::WindowHolder",
                           WindowHolder, ui::UiListRow, 0)

 protected:
  WindowHolder() = default;

 public:
  explicit WindowHolder(ae::ObjProp prop) : UiListRow{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window_number), AE_MMBR(window))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, primary_text, delete_button, presenter, window_number, window);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, primary_text, delete_button, presenter, window_number, window);
  }

  std::uint32_t window_number{0};
  CatalogWindow::ptr window;
};

class AddWindowCatalogEntryEvent;
class RemoveWindowCatalogEntryEvent;
class SetWindowCatalogSelectionEvent;

class CatalogListContainer : public ui::UiListContainer {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogListContainer",
                           CatalogListContainer, ui::UiListContainer, 0)

 protected:
  CatalogListContainer() = default;

 public:
  explicit CatalogListContainer(ae::ObjProp prop) : UiListContainer{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog_id))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, rows, catalog_id);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, rows, catalog_id);
  }

  ae::ObjId catalog_id;

  void HandleListRowActivated(std::size_t row_index) override;
};

class WindowCatalog : public NodeFor<WindowCatalog> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::WindowCatalog",
                           WindowCatalog, Node, 0)

 protected:
  WindowCatalog() = default;

 public:
  explicit WindowCatalog(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(next_window_number), AE_MMBR(selected),
                    AE_MMBR(list), AE_MMBR(main_window),
                    AE_MMBR(main_window_presenter), AE_MMBR(locale),
                    AE_MMBR(label_add), AE_MMBR(label_open),
                    AE_MMBR(label_delete), AE_MMBR(label_language),
                    AE_MMBR(window_title_prefix), AE_MMBR(add_button),
                    AE_MMBR(open_button), AE_MMBR(delete_button),
                    AE_MMBR(language_button), AE_MMBR(list_presenter),
                    AE_MMBR(add_button_presenter), AE_MMBR(open_button_presenter),
                    AE_MMBR(delete_button_presenter),
                    AE_MMBR(language_button_presenter))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, next_window_number, selected, list, main_window,
        main_window_presenter, locale, label_add, label_open, label_delete,
        label_language, window_title_prefix, add_button, open_button,
        delete_button, language_button, list_presenter, add_button_presenter,
        open_button_presenter, delete_button_presenter, language_button_presenter);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, next_window_number, selected, list, main_window,
        main_window_presenter, locale, label_add, label_open, label_delete,
        label_language, window_title_prefix, add_button, open_button,
        delete_button, language_button, list_presenter, add_button_presenter,
        open_button_presenter, delete_button_presenter, language_button_presenter);
  }

  std::uint32_t next_window_number{1};
  WindowHolder::ptr selected;
  CatalogListContainer::ptr list;
  ui::UiDesktopWindow::ptr main_window;
  ui::windows::WinDesktopWindowPresenter::ptr main_window_presenter;
  ae::ObjPtr<ui::UiLocaleSettings> locale;
  ae::ObjPtr<ui::UiLocalizationString> label_add;
  ae::ObjPtr<ui::UiLocalizationString> label_open;
  ae::ObjPtr<ui::UiLocalizationString> label_delete;
  ae::ObjPtr<ui::UiLocalizationString> label_language;
  ae::ObjPtr<ui::UiLocalizationString> window_title_prefix;
  ae::ObjPtr<ui::UiPushButton> add_button;
  ae::ObjPtr<ui::UiPushButton> open_button;
  ae::ObjPtr<ui::UiPushButton> delete_button;
  ae::ObjPtr<ui::UiPushButton> language_button;
  Presenter::ptr list_presenter;
  Presenter::ptr add_button_presenter;
  Presenter::ptr open_button_presenter;
  Presenter::ptr delete_button_presenter;
  Presenter::ptr language_button_presenter;

  void Apply(AddWindowCatalogEntryEvent const& event);
  void Apply(RemoveWindowCatalogEntryEvent const& event);
  void Apply(SetWindowCatalogSelectionEvent const& event);

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

  ae::ObjId pending_focus_window_id{};
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
