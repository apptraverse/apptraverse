#include "window_catalog_model.h"
#include <algorithm>
#include <cassert>
#include <unordered_set>
#include "window_catalog_chrome_layout.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_row_text.h"
#include "window_catalog_localization.h"
#include "apptraverse/object_clone.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_string.h"

#include <set>
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
ui::UiPresentedWindow& Presented(CatalogWindow& window) {
  return static_cast<ui::UiPresentedWindow&>(window);
}
ui::UiPresentedWindow const& Presented(CatalogWindow const& window) {
  return static_cast<ui::UiPresentedWindow const&>(window);
}
bool LoadWindowCloseButton(CatalogWindow& window) {
  // Desktop prefabs use the native Close control. The optional in-content
  // Close graph, when supplied, always includes both button and presenter.
  if (!window.close_button.is_valid()) { return false; }
  window.close_button.Load();
  window.close_button_presenter.Load();
  return true;
}

void MarkWindowCloseButtonUnloadedByDefault(CatalogWindow& window) {
  window.close_button.SetFlags(SetUnloadedDefault(window.close_button.flags()));
  window.close_button_presenter.SetFlags(
      SetUnloadedDefault(window.close_button_presenter.flags()));
}
ui::UiPresentedWindow& MainPresented(WindowCatalog const& catalog) {
  return static_cast<ui::UiPresentedWindow&>(*catalog.persisted.main_window);
}
void BindLiveWindowNotifiers(WindowCatalog& catalog, CatalogWindow& window) {
  std::vector<Node*> live;
  CollectLiveReachableNodes(window, live);
  for (Node* node : live) {
    node->CopyMaterializedChangeNotifierFrom(catalog);
  }
}

CatalogWindow::ptr WindowIdentity(WindowCatalog& catalog, ae::ObjId id) {
  return CatalogWindow::ptr::Declare(
      ae::CreateWith{*catalog.domain}.with_id(id).with_flags(kLazyPrefabLinkFlags));
}

void MoveWindowToFront(WindowCatalog& catalog, ae::ObjId window_id) {
  auto& order = catalog.persisted.window_order;
  order.erase(std::remove_if(order.begin(), order.end(),
      [window_id](auto const& window) { return window.id() == window_id; }),
      order.end());
  order.insert(order.begin(), WindowIdentity(catalog, window_id));
  catalog.pending_focus_window = WindowIdentity(catalog, window_id);
}

void ForgetWindowPresentationState(WindowCatalog& catalog, ae::ObjId window_id) {
  if (catalog.pending_focus_window.id() == window_id) {
    catalog.pending_focus_window = {};
  }
  auto& order = catalog.persisted.window_order;
  bool const was_active = !order.empty() && order.front().id() == window_id;
  order.erase(std::remove_if(order.begin(), order.end(),
      [window_id](auto const& window) { return window.id() == window_id; }),
      order.end());
  if (was_active) {
    assert(!order.empty());  // The main window lives until application shutdown.
    catalog.pending_focus_window = WindowIdentity(catalog, order.front().id());
  }
}


void SaveWindowCatalogPersistentSnapshotImpl(WindowCatalog& catalog) {
  if (catalog.persisted.main_window.is_loaded()) {
    ui::SavePresentedWindow(catalog.persisted.main_window);
  }
  for (auto const& row : catalog.persisted.list->rows) {
    auto* holder = static_cast<WindowHolder*>(&*row);
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
  for (auto const& row : catalog.persisted.list->rows) {
    WindowHolder& holder = *static_cast<WindowHolder*>(&*row);
    if (!holder.window.is_loaded()) {
      continue;
    }
    ui::LoadPresentedWindow(holder.window);
    LoadWindowCloseButton(*holder.window);
    BindLiveWindowNotifiers(catalog, *holder.window);
  }
}
bool WindowCatalog::CanApply(AddWindowCatalogEntryEvent const& event) const {
  ae::ObjId const incoming_id = event.holder.id();
  for (auto const& row : persisted.list->rows) {
    if (row.id() == incoming_id) {
      return false;
    }
  }
  return true;
}
void WindowCatalog::Apply(AddWindowCatalogEntryEvent const& event) {
  assert(CanApply(event));
  persisted.list->rows.push_back(event.holder);
  ++persisted.next_window_number;
  NoteMaterializedChange();
}
void WindowCatalog::Apply(RemoveWindowCatalogEntryEvent const& event) {
  persisted.list.Load();
  ae::ObjId const target_id = event.holder.id();
  auto& rows = persisted.list->rows;
  auto const it = std::find_if(rows.begin(), rows.end(),
                               [&](WindowHolder::ptr const& row) {
                                 return row.id() == target_id;
                               });
  assert(it != rows.end());
  ForgetWindowPresentationState(*this, static_cast<WindowHolder&>(**it).window.id());
  rows.erase(it);
  if (persisted.selected.is_valid() && persisted.selected.id() == target_id) {
    persisted.selected = {};
  }
  // Keep list selection_index consistent with persisted.selected after the row
  // vector shrinks (indices shift; a stale index makes Open follow the list path
  // while selected is empty or points elsewhere).
  if (persisted.selected.is_valid()) {
    persisted.selected.Load();
    std::size_t const idx = RowIndexForHolder(*persisted.selected);
    if (idx >= rows.size()) {
      persisted.selected = {};
      persisted.list->selection_index = -1;
    } else {
      persisted.list->selection_index = static_cast<std::int32_t>(idx);
    }
  } else {
    persisted.list->selection_index = -1;
  }
  RefreshChromeButtonEnabledState();
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

void WindowCatalog::Apply(SetWindowCatalogOrderEvent const& event) {
  assert(!event.ordered.empty());
  auto const previous_active = persisted.window_order.empty()
      ? ae::ObjId{} : persisted.window_order.front().id();
  persisted.window_order = event.ordered;
  // History and order retain identity only; the holder owns the loaded graph.
  for (auto& window : persisted.window_order) {
    window.SetFlags(kLazyPrefabLinkFlags);
    window.Reset();
  }
  if (previous_active != persisted.window_order.front().id()) {
    pending_focus_window = WindowIdentity(*this, persisted.window_order.front().id());
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
  {
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

CatalogWindow::ptr WindowCatalog::ResolveActiveWindow() {
  assert(!persisted.window_order.empty());
  auto const active_id = persisted.window_order.front().id();
  if (active_id == persisted.main_window.id()) {
    return persisted.main_window;
  }
  auto* holder = FindHolderForWindow(active_id);
  assert(holder != nullptr && IsHolderWindowOpen(*holder));
  return holder->window;
}

void WindowCatalog::ActivateWindow(CatalogWindow::ptr window) {
  SubmitActiveWindowFromUi(window.id());
  // An explicit model action (Open/Next) may need to refocus an already active
  // window after another application took native focus. Native echo reports
  // themselves do not request focus or publish an unchanged order.
  if (persisted.window_order.front().id() == window.id()) {
    pending_focus_window = WindowIdentity(*this, window.id());
    NoteMaterializedChange();
  }
}

std::vector<CatalogWindow::ptr> WindowCatalog::BuildWindowNavigationRing() const {
  // Stable catalog order, independent of activation recency. Moving the chosen
  // window to front must not make repeated Next alternate between two windows.
  std::vector<CatalogWindow::ptr> ring;
  ring.push_back(persisted.main_window);
  for (auto const& row : persisted.list->rows) {
    auto const* holder = AsHolder(*row);
    if (IsHolderWindowOpen(*holder)) {
      ring.push_back(holder->window);
    }
  }
  return ring;
}

void WindowCatalog::SubmitActiveWindowFromUi(ae::ObjId window_obj_id) {
  if (window_obj_id != persisted.main_window.id()) {
    auto* holder = FindHolderForWindow(window_obj_id);
    if (holder == nullptr || !IsHolderWindowOpen(*holder)) {
      return;  // A queued activation can outlive its open-window membership.
    }
  }
  if (persisted.window_order.front().id() == window_obj_id) return;
  std::vector<std::uint32_t> ids{window_obj_id.id()};
  for (auto const& window : persisted.window_order) {
    if (window.id() != window_obj_id) ids.push_back(window.id().id());
  }
  SetWindowOrderFromObjIds(std::move(ids));
  // Native focus already happened. Do not replay it later over a newer native
  // activation while this publication is travelling to the mirror.
  pending_focus_window = {};
}

void WindowCatalog::SubmitWindowNavigationFromUi(int delta) {
  if (delta == 0) return;
  auto const ring = BuildWindowNavigationRing();
  if (ring.size() <= 1) return;
  auto const current = ResolveActiveWindow().id();
  auto const it = std::find_if(ring.begin(), ring.end(),
      [current](auto const& window) { return window.id() == current; });
  assert(it != ring.end());
  auto const index = static_cast<std::size_t>(it - ring.begin());
  auto const next = delta > 0 ? (index + 1) % ring.size()
                             : (index == 0 ? ring.size() - 1 : index - 1);
  ActivateWindow(ring[next]);
}

void WindowCatalog::SetWindowOrderFromObjIds(
    std::vector<std::uint32_t> ordered_ids) {
  ae::ObjId const main_id = persisted.main_window.id();
  auto const is_open_catalog_window = [&](ae::ObjId id) {
    if (id == main_id) return true;
    auto* holder = FindHolderForWindow(id);
    return holder != nullptr && IsHolderWindowOpen(*holder);
  };
  std::vector<CatalogWindow::ptr> ordered;
  std::unordered_set<ae::ObjId::Type> seen;
  auto const append = [&](ae::ObjId id) {
    if (id.is_valid() && is_open_catalog_window(id) && seen.insert(id.id()).second) {
      ordered.push_back(WindowIdentity(*this, id));
    }
  };
  for (auto const id : ordered_ids) append(ae::ObjId{id});
  // Native reports can precede creation of a newly opened native window. Keep
  // omitted open model windows, and filter stale closed/deleted native handles.
  for (auto const& window : persisted.window_order) append(window.id());
  if (ordered.size() == persisted.window_order.size() &&
      std::equal(ordered.begin(), ordered.end(), persisted.window_order.begin(),
          [](auto const& a, auto const& b) { return a.id() == b.id(); })) {
    return;
  }
  auto event = SetWindowCatalogOrderEvent::ptr::Create(ae::CreateWith{*domain});
  event->ordered = std::move(ordered);
  Commit(std::move(event));
}
void WindowCatalog::Apply(SetWindowCatalogOpenEvent const& event) {
  WindowHolder* holder = FindListHolderByIdentity(*this, *event.holder);
  if (holder == nullptr) {
    return;
  }
  if (event.open) {
    holder->window.SetFlags(OpenObjFlags(holder->window.flags()));
    MoveWindowToFront(*this, holder->window.id());
  } else {
    holder->window.SetFlags(SetUnloadedDefault(holder->window.flags()));
    ForgetWindowPresentationState(*this, holder->window.id());
  }
  NoteMaterializedChange();
}
WindowHolder* WindowCatalog::FindHolderForWindow(ae::ObjId window_id) {
  persisted.list.Load();
  for (auto& row : persisted.list->rows) {
    row.Load();
    WindowHolder* holder = AsHolder(*row);
    if (holder->window.id() == window_id) {
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
  persisted.texts.list_row_title_format->MaterializeLanguage(language_index);
  persisted.texts.list_row_title_closed_suffix->MaterializeLanguage(
      language_index);
  persisted.texts.label_close.Load();
  persisted.texts.label_close->MaterializeLanguage(language_index);
  if (persisted.main_window.is_loaded()) {
    MainPresented(*this).presenter->title->MaterializeLanguage(language_index);
  }
}
void WindowCatalog::SelectHolder(WindowHolder::ptr holder) {
  auto event = SetWindowCatalogSelectionEvent::ptr::Create(
      ae::CreateWith{*domain});
  event->holder = std::move(holder);
  Commit(std::move(event));
}
namespace {
WindowHolder* FindCurrentHolderById(WindowCatalog& catalog, ae::ObjId holder_id) {
  catalog.persisted.list.Load();
  for (auto const& row : catalog.persisted.list->rows) {
    if (row.id() == holder_id) {
      row.Load();
      return AsHolder(*row);
    }
  }
  return nullptr;  // An accepted UI command may outlive the row's membership.
}
}  // namespace

void WindowCatalog::SubmitSelectionByIdFromUi(ae::ObjId holder_id) {
  if (auto* holder = FindCurrentHolderById(*this, holder_id)) {
    SelectHolder(WindowHolder::ptr::MakeFromThis(holder));
  }
}

void WindowCatalog::OpenHolderByIdFromUi(ae::ObjId holder_id) {
  if (auto* holder = FindCurrentHolderById(*this, holder_id)) {
    OpenHolderWindow(*holder);
  }
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
  return IsWindowCatalogHolderWindowOpen(holder);
}

void WindowCatalog::OpenHolderWindow(WindowHolder& holder) {
  if (IsHolderWindowOpen(holder)) {
    ActivateWindow(holder.window);
    return;
  }
  ae::Domain& domain = *persisted.list->domain;
  ui::LoadPresentedWindow(holder.window);
  // An open window persists with its presenter (and Close graph) eager.
  Presented(*holder.window).presenter.SetFlags(
      OpenObjFlags(Presented(*holder.window).presenter.flags()));
  if (LoadWindowCloseButton(*holder.window)) {
    holder.window->close_button.SetFlags(
        OpenObjFlags(holder.window->close_button.flags()));
    holder.window->close_button_presenter.SetFlags(
        OpenObjFlags(holder.window->close_button_presenter.flags()));
  }
  auto open_event = SetWindowCatalogOpenEvent::ptr::Create(
      ae::CreateWith{domain});
  open_event->holder = WindowHolder::ptr::MakeFromThis(&holder);
  open_event->open = true;
  Commit(std::move(open_event));
  BindLiveWindowNotifiers(*this, *holder.window);
}
void WindowCatalog::CloseHolderWindow(WindowHolder& holder) {
  // One-shot runtime requests must not keep a closing window alive.
  ae::ObjId const closing_id = holder.window.id();
  if (pending_focus_window.is_valid() &&
      pending_focus_window.id() == closing_id) {
    pending_focus_window = {};
  }
  auto close_event = SetWindowCatalogOpenEvent::ptr::Create(
      ae::CreateWith{*domain});
  close_event->holder = WindowHolder::ptr::MakeFromThis(&holder);
  close_event->open = false;
  Commit(std::move(close_event));
  ui::UnloadPresentedWindow(holder.window);
  WindowHolder::ptr::MakeFromThis(&holder).Save();
}
void WindowCatalog::AddWindow() {
  ae::Domain& domain = *persisted.list->domain;
  std::uint32_t const number = persisted.next_window_number;
  auto holder = WindowHolder::ptr::Create(ae::CreateWith{domain});
  holder->window_number = number;
  holder->primary_text = persisted.texts.list_row_title_format;
  holder->delete_button = {};
  holder->presenter = {};
  // Materialize the prefab in a short-lived child domain. Its ordinary graph
  // ownership releases the whole source subgraph after cloning.
  ae::Domain prefab_domain{domain.domain_storage()};
  prefab_domain.set_parent_domain(&domain);
  ae::DomainGraph load_graph{&prefab_domain};
  ae::Ptr<ae::Obj> prefab_root;
  auto const load_result =
      load_graph.LoadRoot(persisted.window_prefab.id(), prefab_root);
  if (load_result.IsErr() || !prefab_root) {
    WriteFatalStderr("fatal: LoadRoot window prefab failed\n");
    std::abort();
  }
  apptraverse::CloneOptions clone_options{};
  clone_options.stable_origin = kWindowPrefabId;
  clone_options.stable_prefab_ids = {kWindowPrefabId, kWindowPrefabPresenterId};
  // Shared constants and ancestor-owned objects are discovered by the clone
  // mechanism from the prefab's persisted object-domain membership.
  auto const& prefab_window = static_cast<CatalogWindow const&>(*prefab_root);
  if (prefab_window.close_button.is_valid()) {
    clone_options.stable_prefab_ids.insert(kWindowPrefabCloseButtonId);
    clone_options.stable_prefab_ids.insert(kWindowPrefabCloseButtonPresenterId);
  }
  apptraverse::CloneCanonicalResult const cloned =
      apptraverse::CloneCanonicalRoot(prefab_domain, *prefab_root, domain,
                                      clone_options);
  if (!cloned) {
    WriteFatalStderr("fatal: CloneCanonicalRoot catalog window failed\n");
    std::abort();
  }
  ae::Ptr<CatalogWindow> window_body{ae::Ptr<ae::Obj>{cloned.root.Load()}};
  CatalogWindow::ptr window =
      CatalogWindow::ptr::MakeFromThis(window_body.get());
  holder->window = window;
  holder->window.SetFlags(SetUnloadedDefault(holder->window.flags()));
  Presented(*window_body).presenter.SetFlags(
      SetUnloadedDefault(Presented(*window_body).presenter.flags()));
  // Clone preserves the supplied internal and external Close graph links.
  if (LoadWindowCloseButton(*window_body)) {
    MarkWindowCloseButtonUnloadedByDefault(*window_body);
  }
  // Capture the initial base only after the holder's persistent links exist.
  InitializeRuntimeNode(*holder, *this);
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
  auto remove = RemoveWindowCatalogEntryEvent::ptr::Create(
      ae::CreateWith{*domain});
  remove->holder = target;
  Commit(std::move(remove));
  if (target->window.is_loaded()) {
    ui::UnloadPresentedWindow(target->window);
  }
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
  bool const has_selection = persisted.selected.is_valid();
  persisted.buttons.open_button.Load();
  persisted.buttons.delete_button.Load();
  persisted.buttons.open_button->MaterializeEnabled(has_selection);
  persisted.buttons.delete_button->MaterializeEnabled(has_selection);
}
}  // namespace apptraverse
