#include "window_catalog_graph.h"

#include "window_catalog_actions.h"
#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_desktop_window.h"
#include "apptraverse/ui/ui_presented_window_composition.h"
#include "apptraverse/ui/ui_presented_window.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse::example::surfaces {

constexpr std::int32_t kMainClientWidth = 640;
constexpr std::int32_t kMainClientHeight = 480;
constexpr std::int32_t kListX = 12;
constexpr std::int32_t kListY = 12;
constexpr std::int32_t kListW = 400;
constexpr std::int32_t kListH = 380;
constexpr std::int32_t kButtonY = 400;
constexpr std::int32_t kButtonW = 90;
constexpr std::int32_t kButtonH = 28;
constexpr std::uint32_t kListControlId = 5001;
constexpr std::uint32_t kAddControlId = 5002;
constexpr std::uint32_t kOpenControlId = 5003;
constexpr std::uint32_t kDeleteControlId = 5004;
constexpr std::uint32_t kLanguageControlId = 5005;

class CatalogListContainerPresenter
    : public ui::windows::WinListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      CatalogListContainerPresenter, ui::windows::WinListContainerPresenter, 0)

 protected:
  CatalogListContainerPresenter() = default;

 public:
  explicit CatalogListContainerPresenter(ae::ObjProp prop)
      : WinListContainerPresenter{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(catalog))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  WindowCatalog::ptr catalog;

  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};

void CatalogListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  if (!catalog || !model_proxy) {
    return;
  }
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionFromUi,
                      row_index);
}

void CatalogListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  if (!catalog || !model_proxy) {
    return;
  }
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenRow, row_index);
}

ui::windows::WinPushButtonPresenter::ptr MakeButtonPresenter(
    ae::Domain& domain, WindowCatalog& catalog, ui::UiPushButton::ptr button,
    std::int32_t x, std::uint32_t control_id) {
  auto presenter = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  presenter->button = button;
  presenter->window_presenter =
      static_cast<ui::UiPresentedWindow&>(*catalog.main_window).presenter;
  presenter->layout_x = x;
  presenter->layout_y = kButtonY;
  presenter->layout_width = kButtonW;
  presenter->layout_height = kButtonH;
  presenter->control_id = control_id;
  return presenter;
}

APPTRAVERSE_REGISTER(CatalogListContainerPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  using example::surfaces::CatalogAddButton;
  using example::surfaces::CatalogDeleteButton;
  using example::surfaces::CatalogLanguageButton;
  using example::surfaces::CatalogListContainerPresenter;
  using example::surfaces::CatalogOpenButton;
  using example::surfaces::MakeButtonPresenter;
  using example::surfaces::kAddControlId;
  using example::surfaces::kButtonH;
  using example::surfaces::kButtonW;
  using example::surfaces::kButtonY;
  using example::surfaces::kDeleteControlId;
  using example::surfaces::kLanguageControlId;
  using example::surfaces::kListControlId;
  using example::surfaces::kListH;
  using example::surfaces::kListW;
  using example::surfaces::kListX;
  using example::surfaces::kListY;
  using example::surfaces::kMainClientHeight;
  using example::surfaces::kMainClientWidth;
  using example::surfaces::kOpenControlId;
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();

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
  auto main_presenter = ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindowPresenter)));
  ui::ComposePresentedWindow(main_window, main_presenter, main_title,
                             catalog->locale);
  InitializeRuntimeNode(*main_window, *catalog);
  main_window->SubmitFrameFromNative(80, 80, kMainClientWidth,
                                     kMainClientHeight);
  catalog->main_window = main_window;

  auto list_presenter = CatalogListContainerPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::CatalogListPresenter)));
  list_presenter->container = list;
  list_presenter->window_presenter = main_presenter;
  list_presenter->layout_x = kListX;
  list_presenter->layout_y = kListY;
  list_presenter->layout_width = kListW;
  list_presenter->layout_height = kListH;
  list_presenter->control_id = kListControlId;
  list_presenter->notify_list_selection_change = true;
  list_presenter->catalog = catalog;

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
      MakeButtonPresenter(domain, *catalog, add_button, 12, kAddControlId);
  catalog->presenters.open_button_presenter = MakeButtonPresenter(
      domain, *catalog, open_button, 12 + kButtonW + 8, kOpenControlId);
  catalog->presenters.delete_button_presenter = MakeButtonPresenter(
      domain, *catalog, delete_button, 12 + 2 * (kButtonW + 8),
      kDeleteControlId);
  catalog->presenters.language_button_presenter = MakeButtonPresenter(
      domain, *catalog, language_button, 12 + 3 * (kButtonW + 8),
      kLanguageControlId);

  catalog->RefreshChromeButtonEnabledState();

  constexpr std::int32_t kPrefabFrameX = 160;
  constexpr std::int32_t kPrefabFrameY = 120;
  constexpr std::int32_t kPrefabClientWidth = 480;
  constexpr std::int32_t kPrefabClientHeight = 320;
  ae::ObjId const window_prefab_id = ToObjId(ObjId::WindowPrefab);
  ae::ObjId const window_prefab_presenter_id =
      ToObjId(ObjId::WindowPrefabPresenter);
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
  auto window_prefab_presenter =
      ui::windows::WinDesktopWindowPresenter::ptr::Create(
          ae::CreateWith{domain}.with_id(window_prefab_presenter_id));
  ui::ComposePresentedWindow(window_prefab, window_prefab_presenter,
                             catalog->texts.window_title_prefix,
                             catalog->locale);
  InitializeRuntimeNode(*window_prefab_body, *catalog);

  ae::DomainGraph distill_save{&domain};
  ae::Ptr<ui::windows::WinDesktopWindowPresenter> window_prefab_presenter_body =
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
  domain.RemoveObject(window_prefab_body.get());
  domain.RemoveObject(window_prefab_presenter_body.get());

  InitializeRuntimeNode(*add_button, *catalog);
  InitializeRuntimeNode(*open_button, *catalog);
  InitializeRuntimeNode(*delete_button, *catalog);
  InitializeRuntimeNode(*language_button, *catalog);

  list->catalog = catalog;
  catalog->list = list;
  InitializeRuntimeNode(*catalog);

  catalog->desktop_z_order.push_back(catalog->main_window);

  return catalog;
}

void EnsureWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  (void)&example::surfaces::g_apptraverse_registrar_CatalogListContainerPresenter;
}

}  // namespace apptraverse
