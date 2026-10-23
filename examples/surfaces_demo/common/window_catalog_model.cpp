#include "window_catalog_model.h"

#include <algorithm>
#include <cassert>

#include "window_catalog_localization.h"

#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"

namespace apptraverse {

namespace {

ae::ObjFlags ClearUnloadedDefault(ae::ObjFlags flags) {
  return static_cast<ae::ObjFlags>(
      flags & ~ae::ObjFlags::kUnloadedByDefault);
}

ae::ObjFlags ClearUnloaded(ae::ObjFlags flags) {
  return static_cast<ae::ObjFlags>(flags & ~ae::ObjFlags::kUnloaded);
}

ae::ObjFlags OpenObjFlags(ae::ObjFlags flags) {
  return ClearUnloaded(ClearUnloadedDefault(flags));
}

ae::ObjFlags SetUnloadedDefault(ae::ObjFlags flags) {
  return static_cast<ae::ObjFlags>(
      flags | ae::ObjFlags::kUnloadedByDefault);
}

std::string ReadPrefixText(ui::UiLocalizationString& prefix, int language_index,
                           int restore_language_index) {
  prefix.MaterializeLanguage(language_index);
  std::string const text = prefix.Resolve(language_index);
  prefix.MaterializeLanguage(restore_language_index);
  return text;
}

void PersistUiStringUtf8(ui::UiString::ptr& string) {
  if (!string.is_valid()) {
    return;
  }
  if (!string.is_loaded()) {
    string.Load();
  }
  string.Save();
  string.Reset();
}

ui::UiLocalizationString::ptr MakeHolderTitleString(ae::Domain& domain,
                                                    WindowCatalog& catalog,
                                                    std::uint32_t number) {
  auto localized = ui::UiLocalizationString::ptr::Create(
      ae::CreateWith{domain});
  InitializeRuntimeNode(*localized, catalog);
  auto en_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  auto ru_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  localized->strings = {en_string, ru_string};

  int const restore_language =
      catalog.locale ? catalog.locale->language_index() : 0;
  std::string const en_prefix =
      ReadPrefixText(*catalog.texts.window_title_prefix, 0, restore_language);
  std::string const ru_prefix =
      ReadPrefixText(*catalog.texts.window_title_prefix, 1, restore_language);
  en_string.Load();
  en_string->utf8 = en_prefix + std::to_string(number);
  PersistUiStringUtf8(en_string);
  ru_string.Load();
  ru_string->utf8 = ru_prefix + std::to_string(number);
  PersistUiStringUtf8(ru_string);
  localized->MaterializeLanguage(restore_language);
  return localized;
}

void SaveCatalogWindowGraph(CatalogWindow& window) {
  if (window.window_presenter.is_valid() &&
      window.window_presenter.is_loaded()) {
    window.window_presenter.Save();
  }
  CatalogWindow::ptr::MakeFromThis(&window).Save();
}

void UnloadCatalogWindowGraph(CatalogWindow::ptr& window) {
  if (!window.is_valid()) {
    return;
  }
  if (!window.is_loaded()) {
    return;
  }
  SaveCatalogWindowGraph(*window);
  if (window->window_presenter.is_valid() &&
      window->window_presenter.is_loaded()) {
    window->window_presenter.Reset();
  }
  window.SetFlags(SetUnloadedDefault(window.flags()));
  window.Reset();
}

void BindLiveWindowNotifiers(WindowCatalog& catalog, CatalogWindow& window) {
  std::vector<Node*> live;
  CollectLiveReachableNodes(window, live);
  for (Node* node : live) {
    node->CopyMaterializedChangeNotifierFrom(catalog);
  }
}

void EnsureHolderWindowTitleLinksImpl(WindowHolder& holder,
                                      WindowCatalog& catalog) {
  if (!holder.primary_text.is_valid()) {
    return;
  }
  if (holder.window.is_valid() && holder.window.is_loaded()) {
    holder.window->title = holder.primary_text;
    if (holder.window->window_presenter.is_valid() &&
        holder.window->window_presenter.is_loaded()) {
      holder.window->window_presenter->title = holder.primary_text;
      holder.window->window_presenter->locale = catalog.locale;
    }
  }
}

void SaveWindowCatalogPersistentSnapshotImpl(WindowCatalog& catalog) {
  if (catalog.main_window.is_valid() && catalog.main_window.is_loaded()) {
    SaveCatalogWindowGraph(
        static_cast<CatalogWindow&>(*catalog.main_window.operator->()));
  }
  if (catalog.main_window_presenter.is_valid() &&
      catalog.main_window_presenter.is_loaded()) {
    catalog.main_window_presenter.Save();
  }
  if (catalog.list) {
    for (auto const& row : catalog.list->rows) {
      if (!row) {
        continue;
      }
      auto* holder = static_cast<WindowHolder*>(&*row);
      if (holder->primary_text.is_valid()) {
        if (!holder->primary_text.is_loaded()) {
          holder->primary_text.Load();
        }
        holder->primary_text.Save();
        holder->primary_text.Reset();
      }
      if (holder->window.is_valid() && holder->window.is_loaded()) {
        SaveCatalogWindowGraph(*holder->window);
      }
    }
  }
}

}  // namespace

void EnsureHolderWindowTitleLinks(WindowHolder& holder,
                                    WindowCatalog& catalog) {
  EnsureHolderWindowTitleLinksImpl(holder, catalog);
}

void SaveWindowCatalogPersistentSnapshot(WindowCatalog& catalog) {
  SaveWindowCatalogPersistentSnapshotImpl(catalog);
}

void RestoreLoadedCatalogWindows(WindowCatalog& catalog) {
  if (!catalog.list) {
    return;
  }
  int const language = catalog.locale ? catalog.locale->language_index() : 0;
  for (auto const& row : catalog.list->rows) {
    if (!row) {
      continue;
    }
    WindowHolder& holder = *static_cast<WindowHolder*>(&*row);
    if (!holder.window.is_valid() || !holder.window.is_loaded()) {
      continue;
    }
    holder.window.SetFlags(OpenObjFlags(holder.window.flags()));
    if (holder.window->window_presenter.is_valid()) {
      if (!holder.window->window_presenter.is_loaded()) {
        holder.window->window_presenter.Load();
      }
      holder.window->window_presenter.SetFlags(
          OpenObjFlags(holder.window->window_presenter.flags()));
      holder.window->window_presenter->locale = catalog.locale;
    }
    catalog.MaterializeHolderListText(holder, language);
    EnsureHolderWindowTitleLinksImpl(holder, catalog);
    BindLiveWindowNotifiers(catalog, *holder.window);
  }
}

void AssignInitialCatalogWindowBounds(CatalogWindow& window,
                                      std::uint32_t number) {
  constexpr std::int32_t kWidth = 480;
  constexpr std::int32_t kHeight = 320;
  constexpr std::int32_t kBaseX = 160;
  constexpr std::int32_t kBaseY = 120;
  constexpr std::int32_t kCascade = 28;
  std::int32_t const offset =
      static_cast<std::int32_t>((number > 0 ? number - 1 : 0) * kCascade);
  window.SubmitFrameFromNative(kBaseX + offset, kBaseY + offset, kWidth,
                               kHeight);
}

void CatalogListContainer::MarkRowsMaterialized() {
  NoteMaterializedChange();
}

void WindowCatalog::Apply(AddWindowCatalogEntryEvent const& event) {
  list->rows.push_back(event.holder);
  ++next_window_number;
  list->MarkRowsMaterialized();
  NoteMaterializedChange();
}

void WindowCatalog::Apply(RemoveWindowCatalogEntryEvent const& event) {
  auto& rows = list->rows;
  auto const it = std::find_if(
      rows.begin(), rows.end(),
      [&](WindowHolder::ptr const& row) { return &*row == &*event.holder; });
  assert(it != rows.end());
  rows.erase(it);
  list->MarkRowsMaterialized();
  if (selected && &*selected == &*event.holder) {
    selected = {};
  }
  NoteMaterializedChange();
}

void WindowCatalog::Apply(SetWindowCatalogSelectionEvent const& event) {
  selected = event.holder;
  if (list) {
    list->selection_index =
        event.holder ? static_cast<std::int32_t>(RowIndexForHolder(*event.holder))
                     : -1;
  }
  RefreshChromeButtonEnabledState();
  NoteMaterializedChange();
}

void CatalogListContainer::HandleListRowActivated(std::size_t row_index) {
  if (!catalog) {
    return;
  }
  catalog->OpenRow(row_index);
}

namespace {

WindowHolder* AsHolder(ui::UiListRow& row) {
  return static_cast<WindowHolder*>(&row);
}

WindowHolder const* AsHolder(ui::UiListRow const& row) {
  return static_cast<WindowHolder const*>(&row);
}

}  // namespace

WindowHolder* WindowCatalog::FindHolderForWindow(ae::ObjId window_id) {
  for (auto const& row : list->rows) {
    if (!row) {
      continue;
    }
    WindowHolder* holder = AsHolder(*row);
    if (holder->window.is_valid() && holder->window.id() == window_id) {
      return holder;
    }
  }
  return nullptr;
}

WindowHolder* WindowCatalog::FindHolderForWindow(CatalogWindow const& window) {
  return FindHolderForWindow(window.obj_id);
}

std::size_t WindowCatalog::RowIndexForHolder(WindowHolder const& holder) const {
  auto const& rows = list->rows;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i] && AsHolder(*rows[i]) == &holder) {
      return i;
    }
  }
  return rows.size();
}

void WindowCatalog::MaterializeCatalogStrings(int language_index) {
  if (texts.label_add) {
    texts.label_add->MaterializeLanguage(language_index);
  }
  if (texts.label_open) {
    texts.label_open->MaterializeLanguage(language_index);
  }
  if (texts.label_delete) {
    texts.label_delete->MaterializeLanguage(language_index);
  }
  if (texts.label_language) {
    texts.label_language->MaterializeLanguage(language_index);
  }
  if (texts.window_title_prefix) {
    texts.window_title_prefix->MaterializeLanguage(language_index);
  }
  if (main_window_presenter && main_window_presenter->title.is_valid()) {
    main_window_presenter->title->MaterializeLanguage(language_index);
  }
  for (auto const& row : list->rows) {
    if (row) {
      MaterializeHolderListText(*AsHolder(*row), language_index);
    }
  }
}

void WindowCatalog::MaterializeHolderListText(WindowHolder& holder,
                                              int language_index) {
  if (!holder.primary_text) {
    return;
  }
  holder.primary_text->MaterializeLanguage(language_index);
  EnsureHolderWindowTitleLinksImpl(holder, *this);
  if (holder.window.is_loaded() && holder.window->title) {
    holder.window->title->MaterializeLanguage(language_index);
  }
}

void WindowCatalog::SelectHolder(WindowHolder::ptr holder) {
  auto event = SetWindowCatalogSelectionEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->holder = std::move(holder);
  Commit(std::move(event));
}

void WindowCatalog::SubmitSelectionFromUi(std::size_t row_index) {
  if (row_index >= list->rows.size() || !list->rows[row_index]) {
    SelectHolder({});
    return;
  }
  SelectHolder(WindowHolder::ptr::MakeFromThis(AsHolder(*list->rows[row_index])));
}

void WindowCatalog::OpenHolderWindow(WindowHolder& holder) {
  if (!holder.window.is_valid()) {
    return;
  }
  if (holder.window.is_loaded()) {
    EnsureHolderWindowTitleLinksImpl(holder, *this);
    holder.window.SetFlags(OpenObjFlags(holder.window.flags()));
    if (holder.window->window_presenter.is_valid()) {
      if (!holder.window->window_presenter.is_loaded()) {
        holder.window->window_presenter.Load();
      }
      holder.window->window_presenter.SetFlags(
          OpenObjFlags(holder.window->window_presenter.flags()));
      EnsureHolderWindowTitleLinksImpl(holder, *this);
    }
    pending_focus_window_id = holder.window.id().id();
    NoteMaterializedChange();
    return;
  }
  holder.window.Load();
  holder.window.SetFlags(OpenObjFlags(holder.window.flags()));
  if (holder.window->window_presenter.is_valid()) {
    if (!holder.window->window_presenter.is_loaded()) {
      holder.window->window_presenter.Load();
    }
    holder.window->window_presenter.SetFlags(
        OpenObjFlags(holder.window->window_presenter.flags()));
  }
  int const language = locale ? locale->language_index() : 0;
  MaterializeHolderListText(holder, language);
  EnsureHolderWindowTitleLinksImpl(holder, *this);
  BindLiveWindowNotifiers(*this, *holder.window);
  pending_focus_window_id = holder.window.id().id();
  NoteMaterializedChange();
}

void WindowCatalog::CloseHolderWindow(WindowHolder& holder) {
  UnloadCatalogWindowGraph(holder.window);
  NoteMaterializedChange();
}

void WindowCatalog::AddWindow() {
  assert(list && list->domain != nullptr);
  ae::Domain& domain = *list->domain;
  std::uint32_t const number = next_window_number;
  auto holder = WindowHolder::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*holder, *this);
  holder->window_number = number;
  holder->primary_text = MakeHolderTitleString(domain, *this, number);
  holder->delete_button = {};

  auto window = CatalogWindow::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*window, *this);
  AssignInitialCatalogWindowBounds(*window, number);
  window->title = holder->primary_text;

  auto presenter = ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain});
  presenter->window = window;
  presenter->locale = locale;
  presenter->title = holder->primary_text;
  window->window_presenter = presenter;
  window->title = holder->primary_text;

  holder->window = window;
  holder->window.SetFlags(OpenObjFlags(holder->window.flags()));
  holder->window->window_presenter.SetFlags(
      OpenObjFlags(holder->window->window_presenter.flags()));

  BindLiveWindowNotifiers(*this, *window);

  auto add = AddWindowCatalogEntryEvent::ptr::Create(ae::CreateWith{domain});
  add->holder = holder;
  Commit(std::move(add));
  SelectHolder(holder);
}

void WindowCatalog::OpenRow(std::size_t row_index) {
  if (row_index >= list->rows.size() || !list->rows[row_index]) {
    return;
  }
  OpenHolderWindow(*AsHolder(*list->rows[row_index]));
}

void WindowCatalog::OpenSelected() {
  if (!selected) {
    return;
  }
  OpenHolderWindow(*selected);
}

void WindowCatalog::DeleteSelected() {
  if (!selected) {
    return;
  }
  WindowHolder::ptr target = selected;
  if (target->window.is_valid() && target->window.is_loaded()) {
    UnloadCatalogWindowGraph(target->window);
  }
  auto remove = RemoveWindowCatalogEntryEvent::ptr::Create(
      ae::CreateWith{*domain});
  remove->holder = target;
  Commit(std::move(remove));
}

void WindowCatalog::UserCloseWindow(ae::ObjId window_obj_id) {
  WindowHolder* holder = FindHolderForWindow(window_obj_id);
  if (holder == nullptr) {
    return;
  }
  if (!holder->window.is_loaded()) {
    return;
  }
  CloseHolderWindow(*holder);
}

void WindowCatalog::ClearPendingFocusWindow() {
  pending_focus_window_id = {};
}

void WindowCatalog::RefreshChromeButtonEnabledState() {
  bool const has_selection = selected.is_valid();
  if (buttons.open_button) {
    buttons.open_button->MaterializeEnabled(has_selection);
  }
  if (buttons.delete_button) {
    buttons.delete_button->MaterializeEnabled(has_selection);
  }
}

}  // namespace apptraverse
