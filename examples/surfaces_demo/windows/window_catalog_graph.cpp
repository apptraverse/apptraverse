#include "window_catalog_graph.h"

#include "window_catalog_ids.h"
#include "window_catalog_localization.h"
#include "window_catalog_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/runtime_node.h"
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

namespace {

WindowCatalog& LoadedCatalog(WindowCatalog::ptr& catalog) {
  if (!catalog.is_valid()) {
    std::abort();
  }
  if (!catalog.is_loaded()) {
    catalog.Load();
  }
  return *catalog;
}

}  // namespace

class CatalogAddButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogAddButton",
                           CatalogAddButton, ui::UiPushButton, 0)

 protected:
  CatalogAddButton() = default;

 public:
  explicit CatalogAddButton(ae::ObjProp prop) : UiPushButton{prop} {}

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

  void HandlePushButtonClicked() override;
};

class CatalogOpenButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surfaces::CatalogOpenButton",
                           CatalogOpenButton, ui::UiPushButton, 0)

 protected:
  CatalogOpenButton() = default;

 public:
  explicit CatalogOpenButton(ae::ObjProp prop) : UiPushButton{prop} {}

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

  void HandlePushButtonClicked() override;
};

class CatalogDeleteButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDeleteButton",
      CatalogDeleteButton, ui::UiPushButton, 0)

 protected:
  CatalogDeleteButton() = default;

 public:
  explicit CatalogDeleteButton(ae::ObjProp prop) : UiPushButton{prop} {}

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

  void HandlePushButtonClicked() override;
};

class CatalogLanguageButton : public ui::UiPushButton {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogLanguageButton",
      CatalogLanguageButton, ui::UiPushButton, 0)

 protected:
  CatalogLanguageButton() = default;

 public:
  explicit CatalogLanguageButton(ae::ObjProp prop) : UiPushButton{prop} {}

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

  void HandlePushButtonClicked() override;
};

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

void CatalogAddButton::HandlePushButtonClicked() {
  LoadedCatalog(catalog).AddWindow();
}

void CatalogOpenButton::HandlePushButtonClicked() {
  WindowCatalog& cat = LoadedCatalog(catalog);
  if (!cat.list) {
    return;
  }
  if (cat.list->selection_index >= 0 &&
      static_cast<std::size_t>(cat.list->selection_index) <
          cat.list->rows.size()) {
    cat.OpenListRowFromUi(
        static_cast<std::size_t>(cat.list->selection_index));
    return;
  }
  cat.OpenSelected();
}

void CatalogDeleteButton::HandlePushButtonClicked() {
  LoadedCatalog(catalog).DeleteSelected();
}

void CatalogLanguageButton::HandlePushButtonClicked() {
  WindowCatalog& cat = LoadedCatalog(catalog);
  if (!cat.locale) {
    return;
  }
  int const next = 1 - cat.locale->language_index();
  cat.MaterializeCatalogStrings(next);
  cat.locale->SubmitLanguageIndexFromUi(next);
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

APPTRAVERSE_REGISTER(CatalogAddButton);
APPTRAVERSE_REGISTER(CatalogOpenButton);
APPTRAVERSE_REGISTER(CatalogDeleteButton);
APPTRAVERSE_REGISTER(CatalogLanguageButton);
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

  auto main_window = CatalogWindow::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindow)));
  InitializeRuntimeNode(*main_window, *catalog);
  main_window->SubmitFrameFromNative(80, 80, kMainClientWidth,
                                     kMainClientHeight);
  catalog->main_window = main_window;

  auto main_presenter = ui::windows::WinDesktopWindowPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(ObjId::MainWindowPresenter)));
  main_presenter->window = main_window;
  static_cast<ui::UiPresentedWindow&>(*main_window).presenter = main_presenter;

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

  main_presenter->locale = catalog->locale;
  AttachMainWindowTitle(*catalog, domain);
  catalog->RefreshChromeButtonEnabledState();

  InitializeRuntimeNode(*add_button, *catalog);
  InitializeRuntimeNode(*open_button, *catalog);
  InitializeRuntimeNode(*delete_button, *catalog);
  InitializeRuntimeNode(*language_button, *catalog);

  list->catalog = catalog;
  catalog->list = list;
  InitializeRuntimeNode(*catalog);

  return catalog;
}

void EnsureWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  (void)&example::surfaces::g_apptraverse_registrar_CatalogAddButton;
  (void)&example::surfaces::g_apptraverse_registrar_CatalogOpenButton;
  (void)&example::surfaces::g_apptraverse_registrar_CatalogDeleteButton;
  (void)&example::surfaces::g_apptraverse_registrar_CatalogLanguageButton;
  (void)&example::surfaces::g_apptraverse_registrar_CatalogListContainerPresenter;
}

}  // namespace apptraverse
