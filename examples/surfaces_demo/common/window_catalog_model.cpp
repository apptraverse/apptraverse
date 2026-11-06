#include "window_catalog_model.h"
#include <algorithm>
#include <cassert>
#include <unordered_set>
#include "window_catalog_chrome_layout.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "apptraverse/object_clone.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presenters.h"
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
ae::ObjId const kWindowPrefabCloseButtonId{window_catalog_demo::ToObjId(
    window_catalog_demo::ObjId::WindowPrefabCloseButton)};
ae::ObjId const kWindowPrefabCloseButtonPresenterId{
    window_catalog_demo::ToObjId(
        window_catalog_demo::ObjId::WindowPrefabCloseButtonPresenter)};
void RestoreUnloadedWindowPrefabLink(WindowCatalog& catalog) {
  ae::ObjId const prefab_id = catalog.persisted.window_prefab.id();
  catalog.persisted.window_prefab.Reset();
  if (!prefab_id.is_valid()) {
    return;
  }
  catalog.persisted.window_prefab = CatalogWindow::ptr::Declare(
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
  // Mobile prefab graph also owns the Close button and its presenter.
  ae::Ptr<ae::Obj> close_button_keepalive;
  if (prefab_window.close_button.is_valid()) {
    close_button_keepalive = prefab_window.close_button.Load();
  }
  ae::Ptr<ae::Obj> close_presenter_keepalive;
  if (prefab_window.close_button_presenter.is_valid()) {
    close_presenter_keepalive = prefab_window.close_button_presenter.Load();
  }
  domain.RemoveObject(&prefab_window);
  if (presenter_obj != nullptr) {
    domain.RemoveObject(presenter_obj);
  }
  if (close_button_keepalive) {
    domain.RemoveObject(close_button_keepalive.get());
  }
  if (close_presenter_keepalive) {
    domain.RemoveObject(close_presenter_keepalive.get());
  }
}

// Points the window-owned Close button graph at this window: close_button ->
// {catalog, window}, close_button_presenter -> window presenter.
void WireWindowCloseButton(WindowCatalog& catalog, CatalogWindow& window) {
  if (!window.close_button.is_valid()) {
    return;
  }
  window.close_button.Load();
  window.close_button->catalog = WindowCatalog::ptr::MakeFromThis(&catalog);
  window.close_button->window = CatalogWindow::ptr::MakeFromThis(&window);
  if (window.close_button_presenter.is_valid()) {
    window.close_button_presenter.Load();
    window.close_button_presenter->window_presenter =
        Presented(window).presenter;
  }
}

void LoadWindowCloseButton(WindowCatalog& catalog, CatalogWindow& window) {
  if (window.close_button.is_valid()) {
    window.close_button.Load();
  }
  if (window.close_button_presenter.is_valid()) {
    window.close_button_presenter.Load();
  }
  WireWindowCloseButton(catalog, window);
}

void MarkWindowCloseButtonUnloadedByDefault(CatalogWindow& window) {
  window.close_button.SetFlags(SetUnloadedDefault(window.close_button.flags()));
  window.close_button_presenter.SetFlags(
      SetUnloadedDefault(window.close_button_presenter.flags()));
}
ui::UiPresentedWindow& MainPresented(WindowCatalog const& catalog) {
  return static_cast<ui::UiPresentedWindow&>(*catalog.persisted.main_window);
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
  int const restore_language = catalog.persisted.locale->language_index();
  std::string const en_prefix =
      ReadPrefixText(*catalog.persisted.texts.window_title_prefix, 0,
                     restore_language);
  std::string const ru_prefix =
      ReadPrefixText(*catalog.persisted.texts.window_title_prefix, 1,
                     restore_language);
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
  if (catalog.persisted.main_window.is_loaded()) {
    ui::SavePresentedWindow(catalog.persisted.main_window);
  }
  for (auto const& row : catalog.persisted.list->rows) {
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
  int const language = catalog.persisted.locale->language_index();
  for (auto const& row : catalog.persisted.list->rows) {
    WindowHolder& holder = *static_cast<WindowHolder*>(&*row);
    if (!holder.window.is_loaded()) {
      continue;
    }
    ui::LoadPresentedWindow(holder.window);
    LoadWindowCloseButton(catalog, *holder.window);
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
                               kHeight, true);
}
bool WindowCatalog::CanApply(AddWindowCatalogEntryEvent const& event) const {
  if (!event.holder.is_valid() || !persisted.list) {
    return false;
  }
  ae::ObjId const incoming_id = event.holder.id();
  for (auto const& row : persisted.list->rows) {
    if (row.is_valid() && row.id() == incoming_id) {
      return false;
    }
  }
  return true;
}
void WindowCatalog::Apply(AddWindowCatalogEntryEvent const& event) {
  assert(CanApply(event));
  persisted.list->rows.push_back(event.holder);
  ++persisted.next_window_number;
  int const language =
      persisted.locale ? persisted.locale->language_index() : 0;
  MaterializeHolderListText(*event.holder, language);
  NoteMaterializedChange();
}
void WindowCatalog::Apply(RemoveWindowCatalogEntryEvent const& event) {
  persisted.list.Load();
  ae::ObjId const target_id = event.holder.id();
  auto& rows = persisted.list->rows;
  auto const it = std::find_if(rows.begin(), rows.end(),
                               [&](WindowHolder::ptr const& row) {
                                 return row.is_valid() && row.id() == target_id;
                               });
  assert(it != rows.end());
  rows.erase(it);
  if (persisted.selected.is_valid() && persisted.selected.id() == target_id) {
    persisted.selected = {};
  }
  NoteMaterializedChange();
}
void WindowCatalog::Apply(SetWindowCatalogSelectionEvent const& event) {
  persisted.selected = event.holder;
  persisted.list.Load();
  if (event.holder.is_valid()) {
    persisted.list->selection_index =
        static_cast<std::int32_t>(RowIndexForHolder(*event.holder));
  } else {
    persisted.list->selection_index = -1;
  }
  RefreshChromeButtonEnabledState();
  NoteMaterializedChange();
}
void CatalogListContainer::HandleListRowActivated(std::size_t row_index) {
  catalog.Load();
  catalog->OpenRow(row_index);
}
namespace {
WindowHolder* AsHolder(ui::UiListRow& row) {
  return static_cast<WindowHolder*>(&row);
}
WindowHolder const* AsHolder(ui::UiListRow const& row) {
  return static_cast<WindowHolder const*>(&row);
}
WindowHolder* FindListHolderByIdentity(WindowCatalog& catalog,
                                       WindowHolder const& target) {
  catalog.persisted.list.Load();
  for (auto& row : catalog.persisted.list->rows) {
    row.Load();
    WindowHolder* holder = AsHolder(*row);
    if (holder->obj_id == target.obj_id) {
      return holder;
    }
  }
  return nullptr;
}
}  // namespace
void WindowCatalog::Apply(SetWindowCatalogDesktopZOrderEvent const& event) {
  persisted.desktop_z_order = event.ordered;
  NoteMaterializedChange();
}
void WindowCatalog::Apply(SetWindowCatalogMobileActiveEvent const& event) {
  if (!event.window.is_valid()) {
    persisted.mobile_active_window = persisted.main_window;
  } else {
    persisted.mobile_active_window = event.window;
  }
  NoteMaterializedChange();
}
namespace {

void ApplyChromeRect(ui::UiPushButtonPresenter& presenter,
                     WindowCatalogChromeRect const& rect) {
  presenter.layout_x = rect.x;
  presenter.layout_y = rect.y;
  presenter.layout_width = rect.width;
  presenter.layout_height = rect.height;
}

void ApplyButtonChromeRect(Presenter::ptr& presenter,
                           WindowCatalogChromeRect const& rect) {
  if (!presenter.is_valid()) {
    return;
  }
  presenter.Load();
  ApplyChromeRect(static_cast<ui::UiPushButtonPresenter&>(*presenter), rect);
}

}  // namespace

void WindowCatalog::Apply(SetWindowCatalogMainClientViewportEvent const& event) {
  if (event.width <= 0 || event.height <= 0) {
    return;
  }
  std::int32_t const dpi =
      event.density_dpi > 0 ? event.density_dpi : 160;
  if (event.width == persisted.main_client_viewport.width &&
      event.height == persisted.main_client_viewport.height &&
      dpi == persisted.main_client_viewport.density_dpi) {
    return;
  }
  persisted.main_client_viewport.width = event.width;
  persisted.main_client_viewport.height = event.height;
  persisted.main_client_viewport.density_dpi = dpi;
  WindowCatalogMainChromeLayout const layout =
      ComputeWindowCatalogMainChromeLayout(event.width, event.height, dpi);
  // Chrome presenters only: the desktop window frame is owned by the platform.
  if (persisted.presenters.list_presenter.is_valid()) {
    persisted.presenters.list_presenter.Load();
    auto& list_presenter = static_cast<ui::UiListContainerPresenter&>(
        *persisted.presenters.list_presenter);
    list_presenter.layout_x = layout.list.x;
    list_presenter.layout_y = layout.list.y;
    list_presenter.layout_width = layout.list.width;
    list_presenter.layout_height = layout.list.height;
  }
  ApplyButtonChromeRect(persisted.presenters.add_button_presenter, layout.add);
  ApplyButtonChromeRect(persisted.presenters.open_button_presenter, layout.open);
  ApplyButtonChromeRect(persisted.presenters.delete_button_presenter,
                        layout.delete_button);
  ApplyButtonChromeRect(persisted.presenters.language_button_presenter,
                        layout.language);
  NoteMaterializedChange();
}

void WindowCatalog::SubmitMainClientViewportFromUi(std::int32_t width,
                                                   std::int32_t height,
                                                   std::int32_t density_dpi) {
  if (width <= 0 || height <= 0) {
    return;
  }
  std::int32_t const dpi = density_dpi > 0 ? density_dpi : 160;
  if (width == persisted.main_client_viewport.width &&
      height == persisted.main_client_viewport.height &&
      dpi == persisted.main_client_viewport.density_dpi) {
    return;
  }
  auto event = SetWindowCatalogMainClientViewportEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->width = width;
  event->height = height;
  event->density_dpi = dpi;
  Commit(std::move(event));
}

void WindowCatalog::SubmitHolderChildWindowClientSizeFromUi(
    std::uint32_t window_obj_id, std::int32_t width, std::int32_t height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  ae::ObjId const wid{window_obj_id};
  if (!wid.is_valid() || wid == persisted.main_window.id()) {
    return;
  }
  auto window = domain->Find(wid).as<CatalogWindow>();
  if (window == nullptr) {
    return;
  }
  if (window->client_width() == width && window->client_height() == height) {
    return;
  }
  WindowHolder* holder = FindHolderForWindow(window->obj_id);
  if (holder == nullptr) {
    return;
  }
  std::int32_t const offset =
      static_cast<std::int32_t>(holder->window_number > 0
                                    ? holder->window_number - 1
                                    : 0) *
      28;
  window->SubmitFrameFromNative(160 + offset, 120 + offset, width, height, false);
}

CatalogWindow::ptr WindowCatalog::ResolveMobileActiveWindow() {
  if (!persisted.mobile_pager) {
    return persisted.main_window;
  }
  if (!persisted.mobile_active_window.is_valid()) {
    return persisted.main_window;
  }
  if (persisted.mobile_active_window.id() == persisted.main_window.id()) {
    return persisted.main_window;
  }
  WindowHolder* holder =
      FindHolderForWindow(persisted.mobile_active_window.id());
  if (holder == nullptr || !holder->window.is_loaded()) {
    return persisted.main_window;
  }
  return holder->window;
}
void WindowCatalog::SetMobileActiveWindow(CatalogWindow::ptr window) {
  if (!persisted.mobile_pager) {
    return;
  }
  auto event = SetWindowCatalogMobileActiveEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->window = std::move(window);
  Commit(std::move(event));
  pending_mobile_active_window = ResolveMobileActiveWindow();
}
std::vector<CatalogWindow::ptr> WindowCatalog::BuildMobileRing() const {
  std::vector<CatalogWindow::ptr> ring;
  ring.push_back(persisted.main_window);
  if (!persisted.list) {
    return ring;
  }
  persisted.list.Load();
  for (auto const& row : persisted.list->rows) {
    row.Load();
    WindowHolder const* holder = AsHolder(*row);
    if (holder->window.is_valid() && holder->window.is_loaded()) {
      ring.push_back(holder->window);
    }
  }
  return ring;
}
CatalogWindow::ptr WindowCatalog::MobileRingPredecessor(
    CatalogWindow const& window) const {
  std::vector<CatalogWindow::ptr> const ring = BuildMobileRing();
  if (ring.empty()) {
    return persisted.main_window;
  }
  for (std::size_t i = 0; i < ring.size(); ++i) {
    if (ring[i].is_valid() && ring[i].id() == window.obj_id) {
      if (i == 0) {
        return ring.back();
      }
      return ring[i - 1];
    }
  }
  return persisted.main_window;
}
void WindowCatalog::SubmitMobileActiveFromUi(ae::ObjId window_obj_id) {
  if (!persisted.mobile_pager) {
    return;
  }
  ae::ObjId const main_id = persisted.main_window.id();
  if (window_obj_id == main_id) {
    SetMobileActiveWindow(persisted.main_window);
    return;
  }
  WindowHolder* holder = FindHolderForWindow(window_obj_id);
  if (holder == nullptr || !holder->window.is_loaded()) {
    return;
  }
  SetMobileActiveWindow(holder->window);
}
void WindowCatalog::SubmitMobileSwipeFromUi(int delta) {
  if (!persisted.mobile_pager || delta == 0) {
    return;
  }
  std::vector<CatalogWindow::ptr> const ring = BuildMobileRing();
  if (ring.size() <= 1) {
    return;
  }
  CatalogWindow::ptr const current = ResolveMobileActiveWindow();
  std::size_t index = 0;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    if (ring[i].is_valid() && ring[i].id() == current.id()) {
      index = i;
      break;
    }
  }
  std::size_t next_index = index;
  if (delta > 0) {
    next_index = (index + 1) % ring.size();
  } else {
    next_index = index == 0 ? ring.size() - 1 : index - 1;
  }
  SetMobileActiveWindow(ring[next_index]);
}
void WindowCatalog::SetDesktopZOrderFromObjIds(
    std::vector<std::uint32_t> ordered_ids) {
  ae::ObjId const main_id = persisted.main_window.id();
  auto const is_open_catalog_window = [&](ae::ObjId id) {
    if (id == main_id) {
      return true;
    }
    WindowHolder* holder = FindHolderForWindow(id);
    if (holder == nullptr) {
      return false;
    }
    return holder->window.is_valid() && holder->window.is_loaded();
  };
  std::vector<CatalogWindow::ptr> ordered;
  ordered.reserve(ordered_ids.size());
  std::unordered_set<ae::ObjId::Type> seen;
  for (std::uint32_t const raw_id : ordered_ids) {
    ae::ObjId const id{raw_id};
    if (!id.is_valid() || !seen.insert(raw_id).second) {
      continue;
    }
    if (!is_open_catalog_window(id)) {
      continue;
    }
    ordered.push_back(CatalogWindow::ptr::Declare(
        ae::CreateWith{*domain}
            .with_id(id)
            .with_flags(kLazyPrefabLinkFlags)));
  }
  if (persisted.desktop_z_order.size() == ordered.size()) {
    bool same = true;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
      if (persisted.desktop_z_order[i].id() != ordered[i].id()) {
        same = false;
        break;
      }
    }
    if (same) {
      return;
    }
  }
  auto event = SetWindowCatalogDesktopZOrderEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->ordered = std::move(ordered);
  Commit(std::move(event));
}
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
  persisted.list.Load();
  for (auto& row : persisted.list->rows) {
    row.Load();
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
std::size_t WindowCatalog::RowIndexForHolder(WindowHolder const& holder) {
  persisted.list.Load();
  auto& rows = persisted.list->rows;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    rows[i].Load();
    if (AsHolder(*rows[i]) == &holder) {
      return i;
    }
  }
  return rows.size();
}
void WindowCatalog::MaterializeCatalogStrings(int language_index) {
  persisted.texts.label_add->MaterializeLanguage(language_index);
  persisted.texts.label_open->MaterializeLanguage(language_index);
  persisted.texts.label_delete->MaterializeLanguage(language_index);
  persisted.texts.label_language->MaterializeLanguage(language_index);
  persisted.texts.window_title_prefix->MaterializeLanguage(language_index);
  if (persisted.texts.label_close.is_valid()) {
    persisted.texts.label_close.Load();
    persisted.texts.label_close->MaterializeLanguage(language_index);
  }
  if (persisted.main_window.is_loaded()) {
    MainPresented(*this).presenter->title->MaterializeLanguage(language_index);
  }
  persisted.list.Load();
  for (auto& row : persisted.list->rows) {
    row.Load();
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
  persisted.list.Load();
  if (row_index >= persisted.list->rows.size()) {
    SelectHolder({});
    return;
  }
  persisted.list->rows[row_index].Load();
  SelectHolder(WindowHolder::ptr::MakeFromThis(
      AsHolder(*persisted.list->rows[row_index])));
}
void WindowCatalog::OpenListRowFromUi(std::size_t row_index) {
  SubmitSelectionFromUi(row_index);
  OpenRow(row_index);
}
bool WindowCatalog::IsHolderWindowOpen(WindowHolder const& holder) const {
  if (!holder.window.is_valid()) {
    return false;
  }
  // Open == neither lazy bit set. Add and Close mark the window lazy
  // (kUnloadedByDefault, then kUnloaded after Reset); OpenObjFlags clears both.
  ae::ObjFlags const flags = holder.window.flags();
  return (flags & ae::ObjFlags::kUnloaded) == ae::ObjFlags{} &&
         (flags & ae::ObjFlags::kUnloadedByDefault) == ae::ObjFlags{};
}

void WindowCatalog::OpenHolderWindow(WindowHolder& holder) {
  if (IsHolderWindowOpen(holder)) {
    pending_focus_window = holder.window;
    if (persisted.mobile_pager) {
      SetMobileActiveWindow(holder.window);
    } else {
      NoteMaterializedChange();
    }
    return;
  }
  ae::Domain& domain = *persisted.list->domain;
  ui::LoadPresentedWindow(holder.window);
  LoadWindowCloseButton(*this, *holder.window);
  // An open window persists with its presenter (and Close graph) eager.
  Presented(*holder.window).presenter.SetFlags(
      OpenObjFlags(Presented(*holder.window).presenter.flags()));
  if (holder.window->close_button.is_valid()) {
    holder.window->close_button.SetFlags(
        OpenObjFlags(holder.window->close_button.flags()));
  }
  if (holder.window->close_button_presenter.is_valid()) {
    holder.window->close_button_presenter.SetFlags(
        OpenObjFlags(holder.window->close_button_presenter.flags()));
  }
  auto open_event = SetWindowCatalogOpenEvent::ptr::Create(
      ae::CreateWith{domain});
  open_event->holder = WindowHolder::ptr::MakeFromThis(&holder);
  open_event->open = true;
  Commit(std::move(open_event));
  int const language = persisted.locale->language_index();
  MaterializeHolderListText(holder, language);
  BindLiveWindowNotifiers(*this, *holder.window);
  pending_focus_window = holder.window;
  if (persisted.mobile_pager) {
    SetMobileActiveWindow(holder.window);
  } else {
    NoteMaterializedChange();
  }
}
void WindowCatalog::CloseHolderWindow(WindowHolder& holder) {
  // One-shot runtime requests must not keep a closing window alive.
  if (holder.window.is_valid()) {
    ae::ObjId const closing_id = holder.window.id();
    if (pending_focus_window.is_valid() &&
        pending_focus_window.id() == closing_id) {
      pending_focus_window = {};
    }
    if (pending_mobile_active_window.is_valid() &&
        pending_mobile_active_window.id() == closing_id) {
      pending_mobile_active_window = {};
    }
  }
  auto close_event = SetWindowCatalogOpenEvent::ptr::Create(
      ae::CreateWith{*domain});
  close_event->holder = WindowHolder::ptr::MakeFromThis(&holder);
  close_event->open = false;
  Commit(std::move(close_event));
  ui::UnloadPresentedWindow(holder.window);
}
void WindowCatalog::AddWindow() {
  ae::Domain& domain = *persisted.list->domain;
  std::uint32_t const number = persisted.next_window_number;
  auto holder = WindowHolder::ptr::Create(ae::CreateWith{domain});
  InitializeRuntimeNode(*holder, *this);
  holder->window_number = number;
  holder->primary_text = MakeHolderTitleString(domain, *this, number);
  holder->delete_button = {};
  ae::DomainGraph load_graph{&domain};
  ae::Ptr<ae::Obj> prefab_root;
  if (persisted.window_prefab.is_loaded()) {
    prefab_root = domain.Find(persisted.window_prefab.id());
  } else {
    ae::seri::SeriResult const load_result =
        load_graph.LoadRoot(persisted.window_prefab.id(), prefab_root);
    if (load_result.IsErr() || !prefab_root) {
      WriteFatalStderr("fatal: LoadRoot window prefab failed\n");
      std::abort();
    }
  }
  apptraverse::CloneOptions clone_options{};
  clone_options.stable_origin = kWindowPrefabId;
  clone_options.stable_prefab_ids = {kWindowPrefabId, kWindowPrefabPresenterId};
  clone_options.boundary_ids = {persisted.texts.window_title_prefix.id(),
                                persisted.locale.id()};
  CatalogWindow& prefab_window = static_cast<CatalogWindow&>(*prefab_root);
  if (prefab_window.close_button.is_valid()) {
    // Close button graph lives in the prefab: clone it with the window, but
    // keep the shared Close label outside the clone. The prefab button has no
    // catalog ref; WireWindowCloseButton assigns it after the clone.
    clone_options.stable_prefab_ids.insert(kWindowPrefabCloseButtonId);
    clone_options.stable_prefab_ids.insert(kWindowPrefabCloseButtonPresenterId);
    clone_options.boundary_ids.insert(persisted.texts.label_close.id());
  }
  apptraverse::CloneCanonicalResult const cloned =
      apptraverse::CloneCanonicalRoot(domain, *prefab_root, domain,
                                      clone_options);
  if (!cloned) {
    WriteFatalStderr("fatal: CloneCanonicalRoot catalog window failed\n");
    std::abort();
  }
  EvictTransientPrefabObjects(domain, prefab_window);
  RestoreUnloadedWindowPrefabLink(*this);
  ae::Ptr<CatalogWindow> window_body{ae::Ptr<ae::Obj>{cloned.root.Load()}};
  CatalogWindow::ptr window =
      CatalogWindow::ptr::MakeFromThis(window_body.get());
  Presented(*window_body).presenter->title = holder->primary_text;
  AssignInitialCatalogWindowBounds(*window_body, number);
  holder->window = window;
  holder->window.SetFlags(SetUnloadedDefault(holder->window.flags()));
  Presented(*window_body).presenter.SetFlags(
      SetUnloadedDefault(Presented(*window_body).presenter.flags()));
  if (window_body->close_button.is_valid()) {
    // Clone gave the window its own Close graph; point it at this holder's
    // window + catalog before it is first presented.
    LoadWindowCloseButton(*this, *window_body);
    MarkWindowCloseButtonUnloadedByDefault(*window_body);
  }
  BindLiveWindowNotifiers(*this, *window_body);
  auto add = AddWindowCatalogEntryEvent::ptr::Create(ae::CreateWith{domain});
  add->holder = holder;
  Commit(std::move(add));
  SelectHolder(holder);
  OpenHolderWindow(*holder);
}
void WindowCatalog::OpenRow(std::size_t row_index) {
  persisted.list.Load();
  if (row_index >= persisted.list->rows.size()) {
    return;
  }
  persisted.list->rows[row_index].Load();
  OpenHolderWindow(*AsHolder(*persisted.list->rows[row_index]));
}
void WindowCatalog::OpenSelected() {
  if (!persisted.selected.is_valid()) {
    return;
  }
  persisted.selected.Load();
  OpenHolderWindow(*persisted.selected);
}
void WindowCatalog::DeleteSelected() {
  if (!persisted.selected.is_valid()) {
    return;
  }
  WindowHolder::ptr target = persisted.selected;
  target.Load();
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
  CatalogWindow::ptr predecessor;
  if (persisted.mobile_pager) {
    predecessor = MobileRingPredecessor(*holder->window);
  }
  CloseHolderWindow(*holder);
  if (persisted.mobile_pager && predecessor.is_valid()) {
    SetMobileActiveWindow(predecessor);
  }
}
void WindowCatalog::ClearPendingFocusWindow() {
  pending_focus_window = {};
}
void WindowCatalog::RefreshChromeButtonEnabledState() {
  bool const has_selection = persisted.selected.is_valid();
  persisted.buttons.open_button.Load();
  persisted.buttons.delete_button.Load();
  persisted.buttons.open_button->MaterializeEnabled(has_selection);
  persisted.buttons.delete_button->MaterializeEnabled(has_selection);
}
}  // namespace apptraverse