#include "window_catalog_graph_builder.h"

#include "window_catalog_actions.h"
#include "window_catalog_chrome_layout.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/runtime_node.h"
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

ui::UiPushButtonPresenter::ptr MakeButtonPresenter(
    ae::Domain& domain, WindowCatalog& catalog, ui::UiPushButton::ptr button,
    WindowCatalogPresenterFactory const& factory,
    WindowCatalogChromeRect const& rect, std::uint32_t control_id) {
  ui::UiPushButtonPresenter::ptr presenter =
      factory.create_button(domain, ae::ObjId{});
  presenter->button = button;
  presenter->window_presenter =
      static_cast<ui::UiPresentedWindow&>(*catalog.main_window).presenter;
  presenter->layout_x = rect.x;
  presenter->layout_y = rect.y;
  presenter->layout_width = rect.width;
  presenter->layout_height = rect.height;
  presenter->control_id = control_id;
  return presenter;
}

}  // namespace

WindowCatalog::ptr BuildWindowCatalogGraph(
    ae::Domain& domain, WindowCatalogPresenterFactory const& factory,
    WindowCatalogGraphOptions options) {
  using example::surfaces::CatalogAddButton;
  using example::surfaces::CatalogDeleteButton;
  using example::surfaces::CatalogLanguageButton;
  using example::surfaces::CatalogOpenButton;
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  auto catalog = WindowCatalog::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Catalog)));

  auto list = CatalogListContainer::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::CatalogList)));
  list->catalog = catalog;
  catalog->list = list;
  InitializeRuntimeNode(*list, *catalog);

  InstallWindowCatalogLocalization(*catalog, domain);
  auto main_title = CreateMainWindowTitle(*catalog, domain);
  auto main_window = CatalogWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindow)));
  ui::UiDesktopWindowPresenter::ptr main_presenter = factory.create_window(
      domain, ae::ObjId{ToObjId(ObjId::MainWindowPresenter)});
  ui::ComposePresentedWindow(main_window, main_presenter, main_title,
                             catalog->locale);
  InitializeRuntimeNode(*main_window, *catalog);
  main_window->SubmitFrameFromNative(80, 80, kMainClientWidth,
                                     kMainClientHeight, true);
  catalog->main_window = main_window;

  // Initial chrome geometry for the default main client size; the model
  // re-applies it from the real viewport (SetWindowCatalogMainClientViewport).
  WindowCatalogMainChromeLayout const chrome =
      ComputeWindowCatalogMainChromeLayout(kMainClientWidth, kMainClientHeight);

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
  add_button->catalog = catalog;
  add_button->label = catalog->texts.label_add;

  auto open_button = CatalogOpenButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::OpenButton)));
  open_button->catalog = catalog;
  open_button->label = catalog->texts.label_open;

  auto delete_button = CatalogDeleteButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::DeleteButton)));
  delete_button->catalog = catalog;
  delete_button->label = catalog->texts.label_delete;

  auto language_button = CatalogLanguageButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::LanguageButton)));
  language_button->catalog = catalog;
  language_button->label = catalog->texts.label_language;

  catalog->buttons.add_button = add_button;
  catalog->buttons.open_button = open_button;
  catalog->buttons.delete_button = delete_button;
  catalog->buttons.language_button = language_button;
  catalog->presenters.list_presenter = list_presenter;
  catalog->presenters.add_button_presenter =
      MakeButtonPresenter(domain, *catalog, add_button, factory, chrome.add,
                          kAddControlId);
  catalog->presenters.open_button_presenter = MakeButtonPresenter(
      domain, *catalog, open_button, factory, chrome.open, kOpenControlId);
  catalog->presenters.delete_button_presenter = MakeButtonPresenter(
      domain, *catalog, delete_button, factory, chrome.delete_button,
      kDeleteControlId);
  catalog->presenters.language_button_presenter = MakeButtonPresenter(
      domain, *catalog, language_button, factory, chrome.language,
      kLanguageControlId);

  catalog->RefreshChromeButtonEnabledState();

  constexpr std::int32_t kPrefabFrameX = 160;
  constexpr std::int32_t kPrefabFrameY = 120;
  constexpr std::int32_t kPrefabClientWidth = 480;
  constexpr std::int32_t kPrefabClientHeight = 320;
  ae::ObjId const window_prefab_id{ToObjId(ObjId::WindowPrefab)};
  ae::ObjId const window_prefab_presenter_id{
      ToObjId(ObjId::WindowPrefabPresenter)};
  ae::ObjFlags const lazy_prefab_flags =
      ae::ObjFlags::kUnloadedByDefault | ae::ObjFlags::kUnloaded;

  auto window_prefab = CatalogWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(window_prefab_id));
  ae::Ptr<CatalogWindow> window_prefab_body = window_prefab.Load();
  auto prefab_frame = ui::SetUiDesktopWindowFrameEvent::ptr::Create(
      ae::CreateWith{domain});
  prefab_frame->x = kPrefabFrameX;
  prefab_frame->y = kPrefabFrameY;
  prefab_frame->client_width = kPrefabClientWidth;
  prefab_frame->client_height = kPrefabClientHeight;
  window_prefab_body->Apply(*prefab_frame);
  ui::UiDesktopWindowPresenter::ptr window_prefab_presenter =
      factory.create_window(domain, ae::ObjId{window_prefab_presenter_id});
  ui::ComposePresentedWindow(window_prefab, window_prefab_presenter,
                             catalog->texts.window_title_prefix,
                             catalog->locale);
  InitializeRuntimeNode(*window_prefab_body, *catalog);

  if (options.mobile_close_in_prefab) {
    AppendMobileCloseButtonToWindowPrefab(domain, *catalog, window_prefab,
                                          window_prefab_presenter, factory,
                                          kPrefabClientHeight);
  }

  ae::DomainGraph distill_save{&domain};
  ae::Ptr<ui::UiDesktopWindowPresenter> window_prefab_presenter_body =
      window_prefab_presenter.Load();
  if (!distill_save.SaveRoot(window_prefab_body, window_prefab_id).IsOk() ||
      !distill_save
           .SaveRoot(window_prefab_presenter_body, window_prefab_presenter_id)
           .IsOk()) {
    std::abort();
  }
  catalog->window_prefab = CatalogWindow::ptr::Declare(
      ae::CreateWith{domain}
          .with_id(window_prefab_id)
          .with_flags(lazy_prefab_flags));
  // Close button graph (mobile prefab) was saved transitively with the prefab.
  ae::Ptr<ae::Obj> close_button_body;
  ae::Ptr<ae::Obj> close_presenter_body;
  if (window_prefab_body->close_button.is_valid()) {
    close_button_body = window_prefab_body->close_button.Load();
  }
  if (window_prefab_body->close_button_presenter.is_valid()) {
    close_presenter_body = window_prefab_body->close_button_presenter.Load();
  }
  domain.RemoveObject(window_prefab_body.get());
  domain.RemoveObject(window_prefab_presenter_body.get());
  if (close_button_body) {
    domain.RemoveObject(close_button_body.get());
  }
  if (close_presenter_body) {
    domain.RemoveObject(close_presenter_body.get());
  }

  InitializeRuntimeNode(*add_button, *catalog);
  InitializeRuntimeNode(*open_button, *catalog);
  InitializeRuntimeNode(*delete_button, *catalog);
  InitializeRuntimeNode(*language_button, *catalog);

  list->catalog = catalog;
  catalog->list = list;
  InitializeRuntimeNode(*catalog);

  catalog->desktop_z_order.push_back(catalog->main_window);

  catalog->main_client_viewport_w = kMainClientWidth;
  catalog->main_client_viewport_h = kMainClientHeight;
  catalog->mobile_pager = options.mobile_pager;
  if (catalog->mobile_pager) {
    catalog->mobile_active_window = catalog->main_window;
  }

  return catalog;
}

}  // namespace apptraverse
