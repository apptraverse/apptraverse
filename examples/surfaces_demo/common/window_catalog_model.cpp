#include "window_catalog_model.h"
#include <algorithm>
#include <cassert>
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "apptraverse/object_clone.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_window_presenter.h"
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
ae::ObjFlags const kLazyPrefabLinkFlags{
    ae::ObjFlags::kUnloadedByDefault | ae::ObjFlags::kUnloaded};
ae::ObjId const kWindowPrefabId{
    window_catalog_demo::ToObjId(window_catalog_demo::ObjId::WindowPrefab)};
ae::ObjId const kWindowPrefabPresenterId{window_catalog_demo::ToObjId(
    window_catalog_demo::ObjId::WindowPrefabPresenter)};
void RestoreUnloadedWindowPrefabLink(WindowCatalog& catalog) {
  ae::ObjId const prefab_id = catalog.window_prefab.id();
  catalog.window_prefab.Reset();
  if (!prefab_id.is_valid()) {
    return;
  }
  catalog.window_prefab = CatalogWindow::ptr::Declare(
      ae::CreateWith{*catalog.domain}
          .with_id(prefab_id)
          .with_flags(kLazyPrefabLinkFlags));
}
ui::UiPresentedWindow& Presented(CatalogWindow& window) {
  return static_cast<ui::UiPresentedWindow&>(window);
}
ui::UiPresentedWindow const& Presented(CatalogWindow const& window) {
  return static_cast<ui::UiPresentedWindow const&>(window);
}
void EvictTransientPrefabObjects(ae::Domain& domain,
                                 CatalogWindow& prefab_window) {
  ui::UiPresentedWindow& presented = Presented(prefab_window);
  ae::Obj* presenter_obj = nullptr;
  if (presented.presenter.is_valid()) {
    ae::Ptr<ae::Obj> const presenter_loaded = presented.presenter.Load();
    if (presenter_loaded) {
      presenter_obj = presenter_loaded.get();
    }
  }
  domain.RemoveObject(&prefab_window);
  if (presenter_obj != nullptr) {
    domain.RemoveObject(presenter_obj);
  }
}
ui::UiPresentedWindow& MainPresented(WindowCatalog const& catalog) {
  return static_cast<ui::UiPresentedWindow&>(*catalog.main_window);
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
  auto en_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  auto ru_string = ui::UiString::ptr::Create(
      ae::CreateWith{domain}.with_flags(ae::ObjFlags::kUnloadedByDefault));
  localized->strings = {en_string, ru_string};
  InitializeRuntimeNode(*localized, catalog);
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
void BindLiveWindowNotifiers(WindowCatalog& catalog, CatalogWindow& window) {
  std::vector<Node*> live;
  CollectLiveReachableNodes(window, live);
  for (Node* node : live) {
    node->CopyMaterializedChangeNotifierFrom(catalog);
  }
}
void SaveWindowCatalogPersistentSnapshotImpl(WindowCatalog& catalog) {
  if (catalog.main_window.is_loaded()) {
    ui::SavePresentedWindow(catalog.main_window);
  }
  for (auto const& row : catalog.list->rows) {
    auto* holder = static_cast<WindowHolder*>(&*row);
    if (!holder->primary_text.is_loaded()) {
      holder->primary_text.Load();
    }
    holder->primary_text.Save();
    holder->primary_text.Reset();
    if (holder->window.is_loaded()) {
      ui::SavePresentedWindow(holder->window);
    }
  }
  WindowCatalog::ptr::MakeFromThis(&catalog).Save();
}
}  // namespace
void SaveWindowCatalogPersistentSnapshot(WindowCatalog& catalog) {
  SaveWindowCatalogPersistentSnapshotImpl(catalog);
}
void RestoreLoadedCatalogWindows(WindowCatalog& catalog) {
  int const language = catalog.locale->language_index();
  for (auto const& row : catalog.list->rows) {
    WindowHolder& holder = *static_cast<WindowHolder*>(&*row);
    if (!holder.window.is_loaded()) {
      continue;
    }
    ui::LoadPresentedWindow(holder.window);
    catalog.MaterializeHolderListText(holder, language);
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
bool WindowCatalog::CanApply(AddWindowCatalogEntryEvent const& event) const {
  if (!event.holder.is_valid() || !list) {
    return false;
  }
  for (auto const& row : list->rows) {
    if (!row) {
      continue;
    }
    if (static_cast<WindowHolder const*>(&*row)->obj_id ==
        event.holder->obj_id) {
      return false;
    }
  }
  return true;
}
void WindowCatalog::Apply(AddWindowCatalogEntryEvent const& event) {
  assert(CanApply(event));
  list->rows.push_back(event.holder);
  ++next_window_number;
  int const language = locale ? locale->language_index() : 0;
  MaterializeHolderListText(*event.holder, language);
  NoteMaterializedChange();
}
void WindowCatalog::Apply(RemoveWindowCatalogEntryEvent const& event) {
  auto& rows = list->rows;
  auto const it = std::find_if(
      rows.begin(), rows.end(),
      [&](WindowHolder::ptr const& row) { return &*row == &*event.holder; });
  assert(it != rows.end());
  rows.erase(it);
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
WindowHolder* FindListHolderByIdentity(WindowCatalog const& catalog,
                                       WindowHolder const& target) {
  if (!catalog.list) {
    return nullptr;
  }
  for (auto const& row : catalog.list->rows) {
    if (!row) {
      continue;
    }
    WindowHolder* holder = AsHolder(*row);
    if (holder->obj_id == target.obj_id) {
      return holder;
    }
  }
  return nullptr;
}
}  // namespace
void WindowCatalog::Apply(SetWindowCatalogOpenEvent const& event) {
  if (!event.holder.is_valid()) {
    return;
  }
  WindowHolder* holder = FindListHolderByIdentity(*this, *event.holder);
  if (holder == nullptr || !holder->window.is_valid()) {
    return;
  }
  if (event.open) {
    holder->window.SetFlags(OpenObjFlags(holder->window.flags()));
  } else {
    holder->window.SetFlags(SetUnloadedDefault(holder->window.flags()));
  }
  NoteMaterializedChange();
}
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
  texts.label_add->MaterializeLanguage(language_index);
  texts.label_open->MaterializeLanguage(language_index);
  texts.label_delete->MaterializeLanguage(language_index);
  texts.label_language->MaterializeLanguage(language_index);
  texts.window_title_prefix->MaterializeLanguage(language_index);
  if (main_window.is_loaded()) {
    MainPresented(*this).presenter->title->MaterializeLanguage(language_index);
  }
  for (auto const& row : list->rows) {
    MaterializeHolderListText(*AsHolder(*row), language_index);
  }
}
void WindowCatalog::MaterializeHolderListText(WindowHolder& holder,
                                              int language_index) {
  if (!holder.primary_text.is_loaded()) {
    holder.primary_text.Load();
  }
  holder.primary_text->MaterializeLanguage(language_index);
  if (holder.window.is_loaded()) {
    Presented(*holder.window).presenter->title->MaterializeLanguage(
        language_index);
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
void WindowCatalog::OpenListRowFromUi(std::size_t row_index) {
  SubmitSelectionFromUi(row_index);
  OpenRow(row_index);
}
void WindowCatalog::OpenHolderWindow(WindowHolder& holder) {
  if (holder.window.is_loaded()) {
    pending_focus_window = holder.window;
    NoteMaterializedChange();
    return;
  }
  ae::Domain& domain = *list->domain;
  ui::LoadPresentedWindow(holder.window);
  auto open_event = SetWindowCatalogOpenEvent::ptr::Create(
      ae::CreateWith{domain});
  open_event->holder = WindowHolder::ptr::MakeFromThis(&holder);
  open_event->open = true;
  Commit(std::move(open_event));
  int const language = locale->language_index();
  MaterializeHolderListText(holder, language);
  BindLiveWindowNotifiers(*this, *holder.window);
  pending_focus_window = holder.window;
  NoteMaterializedChange();
}
void WindowCatalog::CloseHolderWindow(WindowHolder& holder) {
  auto close_event = SetWindowCatalogOpenEvent::ptr::Create(
      ae::CreateWith{*domain});
  close_event->holder = WindowHolder::ptr::MakeFromThis(&holder);
  close_event->open = false;
  Commit(std::move(close_event));
  ui::UnloadPresentedWindow(holder.window);
}
void WindowCatalog::AddWindow() {
  assert(list && list->domain != nullptr);
  ae::Domain& domain = *list->domain;
  std::uint32_t const number = next_window_number;
  if (!window_prefab.is_valid()) {
    assert(false && "WindowCatalog missing window_prefab");
    return;
  }
  auto holder = WindowHolder::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*holder, *this);
  holder->window_number = number;
  holder->primary_text = MakeHolderTitleString(domain, *this, number);
  holder->delete_button = {};
  ae::DomainGraph load_graph{&domain};
  ae::Ptr<ae::Obj> prefab_root;
  if (window_prefab.is_loaded()) {
    prefab_root = domain.Find(window_prefab.id());
  } else if (!load_graph.LoadRoot(window_prefab.id(), prefab_root).IsOk() ||
             !prefab_root) {
    assert(false && "LoadRoot window prefab failed");
    return;
  }
  apptraverse::CloneOptions clone_options{};
  clone_options.stable_origin = kWindowPrefabId;
  clone_options.stable_prefab_ids = {kWindowPrefabId, kWindowPrefabPresenterId};
  clone_options.boundary_ids = {texts.window_title_prefix.id(), locale.id()};
  apptraverse::CloneCanonicalResult const cloned =
      apptraverse::CloneCanonicalRoot(domain, *prefab_root, domain,
                                      clone_options);
  if (!cloned) {
    assert(false && "CloneCanonicalRoot catalog window failed");
    return;
  }
  EvictTransientPrefabObjects(domain, static_cast<CatalogWindow&>(*prefab_root));
  RestoreUnloadedWindowPrefabLink(*this);
  ae::Ptr<CatalogWindow> window_body{ae::Ptr<ae::Obj>{cloned.root.Load()}};
  CatalogWindow::ptr window =
      CatalogWindow::ptr::MakeFromThis(window_body.get());
  Presented(*window_body).presenter->title = holder->primary_text;
  AssignInitialCatalogWindowBounds(*window_body, number);
  holder->window = window;
  holder->window.SetFlags(OpenObjFlags(holder->window.flags()));
  Presented(*window_body).presenter.SetFlags(
      OpenObjFlags(Presented(*window_body).presenter.flags()));
  BindLiveWindowNotifiers(*this, *window_body);
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
  if (target->window.is_loaded()) {
    ui::UnloadPresentedWindow(target->window);
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
  pending_focus_window = {};
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