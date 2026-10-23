#include "window_catalog_graph.h"

#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/ui/ui_push_button.h"

namespace apptraverse {
namespace example {
namespace surfaces {

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

class CatalogAddButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogAddButton",
                           CatalogAddButton, ui::UiPushButton, 0)
 public:
  explicit CatalogAddButton(ae::ObjProp prop) : UiPushButton{prop} {}
  WindowCatalog::ptr catalog;
  void HandlePushButtonClicked() override;
};

class CatalogOpenButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogOpenButton",
                           CatalogOpenButton, ui::UiPushButton, 0)
 public:
  explicit CatalogOpenButton(ae::ObjProp prop) : UiPushButton{prop} {}
  WindowCatalog::ptr catalog;
  void HandlePushButtonClicked() override;
};

class CatalogDeleteButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDeleteButton",
      CatalogDeleteButton, ui::UiPushButton, 0)
 public:
  explicit CatalogDeleteButton(ae::ObjProp prop) : UiPushButton{prop} {}
  WindowCatalog::ptr catalog;
  void HandlePushButtonClicked() override;
};

class CatalogLanguageButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogLanguageButton",
      CatalogLanguageButton, ui::UiPushButton, 0)
 public:
  explicit CatalogLanguageButton(ae::ObjProp prop) : UiPushButton{prop} {}
  WindowCatalog::ptr catalog;
  void HandlePushButtonClicked() override;
};

class CatalogListContainerPresenter : public ui::windows::WinListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogListContainerPresenter",
      CatalogListContainerPresenter, ui::windows::WinListContainerPresenter, 0)
 public:
  explicit CatalogListContainerPresenter(ae::ObjProp prop)
      : WinListContainerPresenter{prop} {}
  WindowCatalog::ptr catalog;
  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};

void CatalogAddButton::HandlePushButtonClicked() {
  if (catalog) {
    catalog->AddWindow();
  }
}

void CatalogOpenButton::HandlePushButtonClicked() {
  if (catalog) {
    catalog->OpenSelected();
  }
}

void CatalogDeleteButton::HandlePushButtonClicked() {
  if (catalog) {
    catalog->DeleteSelected();
  }
}

void CatalogLanguageButton::HandlePushButtonClicked() {
  if (!catalog || !catalog->locale) {
    return;
  }
  int const next = 1 - catalog->locale->language_index();
  catalog->MaterializeCatalogStrings(next);
  catalog->locale->SubmitLanguageIndexFromUi(next);
}

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
    ae::Domain& domain, WindowCatalog& catalog,
    ui::UiPushButton::ptr button, std::int32_t x, std::uint32_t control_id) {
  auto presenter = ui::windows::WinPushButtonPresenter::ptr::Create(
      ae::CreateWith{domain});
  presenter->button = button;
  presenter->window_presenter = catalog.main_window_presenter;
  presenter->layout_x = x;
  presenter->layout_y = kButtonY;
  presenter->layout_width = kButtonW;
  presenter->layout_height = kButtonH;
  presenter->control_id = control_id;
  return presenter;
}

}  // namespace surfaces
}  // namespace example

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  using namespace example::surfaces;
  using window_catalog_demo::ObjId;
  using window_catalog_demo::ToObjId;

  auto catalog = WindowCatalog::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::Catalog)));
  InitializeRuntimeNode(*catalog);

  auto list = CatalogListContainer::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::CatalogList)));
  InitializeRuntimeNode(*list, *catalog);
  list->catalog_id = catalog->obj_id;
  catalog->list = list;

  auto main_window = ui::UiDesktopWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindow)));
  InitializeRuntimeNode(*main_window, *catalog);
  main_window->SubmitFrameFromNative(80, 80, kMainClientWidth,
                                       kMainClientHeight);
  catalog->main_window = main_window;

  auto main_presenter = ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindowPresenter)));
  main_presenter->window = main_window;
  catalog->main_window_presenter = main_presenter;

  InstallWindowCatalogLocalization(*catalog, domain);

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
  InitializeRuntimeNode(*add_button, *catalog);
  add_button->catalog = catalog;
  add_button->label = catalog->label_add;

  auto open_button = CatalogOpenButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::OpenButton)));
  InitializeRuntimeNode(*open_button, *catalog);
  open_button->catalog = catalog;
  open_button->label = catalog->label_open;

  auto delete_button = CatalogDeleteButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::DeleteButton)));
  InitializeRuntimeNode(*delete_button, *catalog);
  delete_button->catalog = catalog;
  delete_button->label = catalog->label_delete;

  auto language_button = CatalogLanguageButton::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::LanguageButton)));
  InitializeRuntimeNode(*language_button, *catalog);
  language_button->catalog = catalog;
  language_button->label = catalog->label_language;

  catalog->add_button = add_button;
  catalog->open_button = open_button;
  catalog->delete_button = delete_button;
  catalog->language_button = language_button;
  catalog->list_presenter = list_presenter;
  catalog->add_button_presenter =
      MakeButtonPresenter(domain, *catalog, add_button, 12, kAddControlId);
  catalog->open_button_presenter = MakeButtonPresenter(
      domain, *catalog, open_button, 12 + kButtonW + 8, kOpenControlId);
  catalog->delete_button_presenter = MakeButtonPresenter(
      domain, *catalog, delete_button, 12 + 2 * (kButtonW + 8),
      kDeleteControlId);
  catalog->language_button_presenter = MakeButtonPresenter(
      domain, *catalog, language_button, 12 + 3 * (kButtonW + 8),
      kLanguageControlId);

  main_presenter->locale = catalog->locale;

  return catalog;
}

namespace example {
namespace surfaces {

void EnsureWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  (void)&g_apptraverse_registrar_CatalogAddButton;
  (void)&g_apptraverse_registrar_CatalogOpenButton;
  (void)&g_apptraverse_registrar_CatalogDeleteButton;
  (void)&g_apptraverse_registrar_CatalogLanguageButton;
  (void)&g_apptraverse_registrar_CatalogListContainerPresenter;
}

APPTRAVERSE_REGISTER(CatalogAddButton);
APPTRAVERSE_REGISTER(CatalogOpenButton);
APPTRAVERSE_REGISTER(CatalogDeleteButton);
APPTRAVERSE_REGISTER(CatalogLanguageButton);
APPTRAVERSE_REGISTER(CatalogListContainerPresenter);

}  // namespace surfaces
}  // namespace example
}  // namespace apptraverse
