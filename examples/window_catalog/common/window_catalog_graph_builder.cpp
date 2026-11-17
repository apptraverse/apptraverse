#include "window_catalog_graph_builder.h"

#include <cstdlib>

#include "window_catalog_actions.h"
#include "window_catalog_chrome_layout.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"
#include "window_catalog_prefab_sizes.h"

#include "apptraverse/runtime_node.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presented_window.h"

namespace apptraverse {
namespace {

constexpr std::int32_t kMainClientWidth = 640;
constexpr std::int32_t kMainClientHeight = 480;
constexpr std::uint32_t kListControlId = 5001;
constexpr std::uint32_t kAddControlId = 5002;
constexpr std::uint32_t kOpenControlId = 5003;
constexpr std::uint32_t kDeleteControlId = 5004;
constexpr std::uint32_t kLanguageControlId = 5005;

void ValidateDistilledLayout(ui::UiLayoutNode const& root) {
  std::string error;
  if (!ui::ValidateUiConstraintLayout(root, &error)) {
    WriteFatalStderr("fatal: invalid distilled layout: ");
    WriteFatalStderr(error.c_str());
    WriteFatalStderr("\n");
    std::abort();
  }
}

ui::UiPushButtonPresenter::ptr MakeButtonPresenter(
    ae::Domain& domain, WindowCatalog& catalog, ui::UiPushButton::ptr button,
    WindowCatalogPresenterFactory const& factory,
    WindowCatalogChromeRect const& rect, std::uint32_t control_id,
    ae::ObjId presenter_id) {
  ui::UiPushButtonPresenter::ptr presenter =
      factory.create_button(domain, presenter_id);
  presenter->button = button;
  presenter->window_presenter =
      static_cast<ui::UiPresentedWindow&>(*catalog.persisted.main_window)
          .presenter;
  presenter->layout_x = rect.x;
  presenter->layout_y = rect.y;
  presenter->layout_width = rect.width;
  presenter->layout_height = rect.height;
  presenter->control_id = control_id;
  return presenter;
}

}  // namespace

ui::UiLayoutNode DefaultWindowCatalogMainLayout() {
  ui::UiLayoutNode root;
  root.flow = ui::UiLayoutFlow::kAdaptive;
  root.padding = 12;
  root.gap = 12;
  ui::UiLayoutNode list;
  list.control_id = kListControlId;
  list.min_width = 120;
  list.min_height = 80;
  list.preferred_width = 300;
  list.preferred_height = 240;
  list.grow = 1;
  ui::UiLayoutNode actions;
  actions.flow = ui::UiLayoutFlow::kVertical;
  actions.gap = 8;
  actions.min_width = 120;
  for (auto id : {kAddControlId, kOpenControlId, kDeleteControlId,
                  kLanguageControlId}) {
    ui::UiLayoutNode button;
    button.control_id = id;
    button.min_width = 100;
    button.min_height = 32;
    actions.children.push_back(button);
  }
  root.children = {list, actions};
  return root;
}

ui::UiLayoutNode DefaultWindowCatalogChildLayout() {
  ui::UiLayoutNode root;
  root.flow = ui::UiLayoutFlow::kVertical;
  root.padding = 12;
  ui::UiLayoutNode close;
  close.control_id = 5006;
  close.min_width = 100;
  close.min_height = 32;
  root.children.push_back(close);
  return root;
}

WindowCatalog::ptr BuildWindowCatalogGraph(
    ae::Domain& domain, WindowCatalogPresenterFactory const& factory,
    WindowCatalogGraphOptions options) {
  using example::surfaces::CatalogAddButton;
  using example::surfaces::CatalogDeleteButton;
  using example::surfaces::CatalogLanguageButton;
  using example::surfaces::CatalogOpenButton;
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  // One supply identity space across independently built platform binaries.
  // Child prefab domains inherit this allocator; runtime clones resume normal
  // IDs after the scope ends. The range is disjoint from fixed IDs and from
  // ObjId::GenerateUnique's runtime range (10000 and above).
  ae::Domain::ObjectIdSequence supply_ids{domain, ae::ObjId{1000},
                                         ae::ObjId{9999}};

  auto catalog = WindowCatalog::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Catalog)));

  catalog->persisted.device_state = ui::UiDeviceState::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::DeviceState)));
  InitializeRuntimeNode(*catalog->persisted.device_state, *catalog);

  catalog->persisted.next_window_number = 1;
  catalog->persisted.main_client_viewport = {kMainClientWidth, kMainClientHeight, 160};

  auto list = CatalogListContainer::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::CatalogList)));
  list->selection_index = -1;
  list->catalog = catalog;
  catalog->persisted.list = list;
  InitializeRuntimeNode(*list, *catalog);

  InstallWindowCatalogLocalization(*catalog, domain);
  auto main_title = CreateMainWindowTitle(*catalog, domain);
  auto main_window = CatalogWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindow)));
  auto main_definition = ui::UiLayoutDefinition::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainLayoutDefinition)));
  main_definition->root = options.main_layout.value_or(
      DefaultWindowCatalogMainLayout());
  auto child_definition = ui::UiLayoutDefinition::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::ChildLayoutDefinition)));
  child_definition->root = DefaultWindowCatalogChildLayout();
  catalog->persisted.child_layout_definition = child_definition;
  ValidateDistilledLayout(main_definition->root);
  ValidateDistilledLayout(child_definition->root);
  // The prefab's child Domain retains this parent-owned supply reference during
  // historical capture. Make its complete backing layer available before the
  // child graph loads that retained identity; ownership stays in the parent.
  SaveDistilledRoot(*child_definition);
  main_window->layout = ui::UiLayoutState::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainLayoutState)));
  main_window->layout->definition = main_definition;
  InitializeRuntimeNode(*main_window->layout, *catalog);
  main_window->BootstrapFrame(0, 0, kMainClientWidth, kMainClientHeight);
  ui::UiDesktopWindowPresenter::ptr main_presenter = factory.create_window(
      domain, ae::ObjId{ToObjId(ObjId::MainWindowPresenter)}, catalog);
  ui::ComposePresentedWindow(main_window, main_presenter, main_title,
                             catalog->persisted.locale);
  InitializeRuntimeNode(*main_window, *catalog);
  catalog->persisted.main_window = main_window;

  // Bootstrap geometry for adapters not yet migrated to local constraints.
  // Windows uses the distilled layout tree and native measurements instead;
  // native Windows resizing never rewrites these model fields.
  WindowCatalogMainChromeLayout const chrome =
      ComputeWindowCatalogMainChromeLayout(kMainClientWidth, kMainClientHeight,
                                          160);

  ui::UiListContainerPresenter::ptr list_presenter = factory.create_list(
      domain, ae::ObjId{ToObjId(ObjId::CatalogListPresenter)}, catalog);
  list_presenter->container = list;
  list_presenter->window_presenter = main_presenter;
  list_presenter->layout_x = chrome.list.x;
  list_presenter->layout_y = chrome.list.y;
  list_presenter->layout_width = chrome.list.width;
  list_presenter->layout_height = chrome.list.height;
  list_presenter->control_id = kListControlId;
  list_presenter->notify_list_selection_change = true;

  auto add_button = CatalogAddButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::AddButton)));
  add_button->BootstrapSetEnabled(true);
  add_button->catalog = catalog;
  add_button->label = catalog->persisted.texts.label_add;

  auto open_button = CatalogOpenButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::OpenButton)));
  open_button->BootstrapSetEnabled(true);
  open_button->catalog = catalog;
  open_button->label = catalog->persisted.texts.label_open;

  auto delete_button = CatalogDeleteButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::DeleteButton)));
  delete_button->BootstrapSetEnabled(true);
  delete_button->catalog = catalog;
  delete_button->label = catalog->persisted.texts.label_delete;

  auto language_button = CatalogLanguageButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::LanguageButton)));
  language_button->BootstrapSetEnabled(true);
  language_button->catalog = catalog;
  language_button->label = catalog->persisted.texts.label_language;

  catalog->persisted.buttons.add_button = add_button;
  catalog->persisted.buttons.open_button = open_button;
  catalog->persisted.buttons.delete_button = delete_button;
  catalog->persisted.buttons.language_button = language_button;
  catalog->persisted.presenters.list_presenter = list_presenter;
  catalog->persisted.presenters.add_button_presenter =
      MakeButtonPresenter(domain, *catalog, add_button, factory, chrome.add,
                          kAddControlId, ae::ObjId{ToObjId(ObjId::AddButtonPresenter)});
  catalog->persisted.presenters.open_button_presenter = MakeButtonPresenter(
      domain, *catalog, open_button, factory, chrome.open, kOpenControlId,
      ae::ObjId{ToObjId(ObjId::OpenButtonPresenter)});
  catalog->persisted.presenters.delete_button_presenter = MakeButtonPresenter(
      domain, *catalog, delete_button, factory, chrome.delete_button,
      kDeleteControlId, ae::ObjId{ToObjId(ObjId::DeleteButtonPresenter)});
  catalog->persisted.presenters.language_button_presenter = MakeButtonPresenter(
      domain, *catalog, language_button, factory, chrome.language,
      kLanguageControlId, ae::ObjId{ToObjId(ObjId::LanguageButtonPresenter)});

  catalog->RefreshChromeButtonEnabledState();

  constexpr std::int32_t kPrefabClientWidth = kWindowCatalogPrefabClientWidth;
  constexpr std::int32_t kPrefabClientHeight = kWindowCatalogPrefabClientHeight;
  ae::ObjId const window_prefab_id{ToObjId(ObjId::WindowPrefab)};
  ae::ObjId const window_prefab_presenter_id{
      ToObjId(ObjId::WindowPrefabPresenter)};
  ae::ObjFlags const lazy_prefab_flags =
      ae::ObjFlags::kUnloadedByDefault | ae::ObjFlags::kUnloaded;

  ae::Domain prefab_domain{domain.domain_storage()};
  prefab_domain.set_parent_domain(&domain);
  prefab_domain.SetObjectScope(window_prefab_id);
  auto window_prefab = CatalogWindow::ptr::Create(
      ae::CreateWith{prefab_domain}.with_id(window_prefab_id));
  window_prefab->layout = ui::UiLayoutState::ptr::Create(
      ae::CreateWith{prefab_domain}.with_id(ToObjId(ObjId::WindowPrefabLayoutState)));
  window_prefab->layout->definition = child_definition;
  InitializeRuntimeNode(*window_prefab->layout, *catalog);
  ae::Ptr<CatalogWindow> window_prefab_body = window_prefab.Load();
  window_prefab_body->BootstrapFrame(0, 0, kPrefabClientWidth, kPrefabClientHeight);
  ui::UiDesktopWindowPresenter::ptr window_prefab_presenter =
      factory.create_window(prefab_domain, ae::ObjId{window_prefab_presenter_id},
                            catalog);
  ui::ComposePresentedWindow(window_prefab, window_prefab_presenter,
                             catalog->persisted.texts.window_title_prefix,
                             catalog->persisted.locale);
  InitializeRuntimeNode(*window_prefab_body, *catalog);

  if (options.mobile_close_in_prefab) {
    AppendMobileCloseButtonToWindowPrefab(prefab_domain, *catalog, window_prefab,
                                          window_prefab_presenter, factory,
                                          kPrefabClientHeight);
  }

  ae::DomainGraph distill_save{&prefab_domain};
  ae::Ptr<ui::UiDesktopWindowPresenter> window_prefab_presenter_body =
      window_prefab_presenter.Load();
  if (!distill_save.SaveRoot(window_prefab_body, window_prefab_id).IsOk() ||
      !distill_save
           .SaveRoot(window_prefab_presenter_body, window_prefab_presenter_id)
           .IsOk()) {
    std::abort();
  }
  catalog->persisted.window_prefab = CatalogWindow::ptr::Declare(
      ae::CreateWith{domain}
          .with_id(window_prefab_id)
          .with_flags(lazy_prefab_flags));
  InitializeRuntimeNode(*add_button, *catalog);
  InitializeRuntimeNode(*open_button, *catalog);
  InitializeRuntimeNode(*delete_button, *catalog);
  InitializeRuntimeNode(*language_button, *catalog);

  list->catalog = catalog;
  catalog->persisted.list = list;
  catalog->persisted.window_order.push_back(CatalogWindow::ptr::Declare(
      ae::CreateWith{domain}.with_id(main_window.id()).with_flags(lazy_prefab_flags)));

  catalog->persisted.main_client_viewport.width = kMainClientWidth;
  catalog->persisted.main_client_viewport.height = kMainClientHeight;
  catalog->persisted.main_client_viewport.density_dpi = 160;

  InitializeRuntimeNode(*catalog);
  return catalog;
}

}  // namespace apptraverse
