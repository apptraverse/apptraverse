#include "window_catalog_graph_android.h"

#include "window_catalog_actions.h"
#include "window_catalog_graph_builder.h"
#include "window_catalog_ids.h"
#include "window_catalog_list_presenter_mixin.h"
#include "window_catalog_model.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_list_presenter.h"
#include "apptraverse/platform/android/android_ui_push_button_presenter.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/platform/android/android_ui_window_presenter.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::surfaces {

// Main catalog window: chrome layout is computed by the model in main client
// viewport space (SetWindowCatalogMainClientViewportEvent), which the native
// runtime reports in mount pixels, so no model->presentation rescale applies.
// Child windows keep AndroidDesktopWindowPresenter::ResolveChildLayout scaling.
class AndroidCatalogMainWindowPresenter
    : public ui::android::AndroidDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::AndroidCatalogMainWindowPresenter",
      AndroidCatalogMainWindowPresenter,
      ui::android::AndroidDesktopWindowPresenter, 0)

 protected:
  AndroidCatalogMainWindowPresenter() = default;

 public:
  explicit AndroidCatalogMainWindowPresenter(ae::ObjProp prop)
      : AndroidDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  ui::UiControlLayout ResolveChildLayout(
      ui::UiControlLayout requested) const override {
    return requested;
  }
};

APPTRAVERSE_REGISTER(AndroidCatalogMainWindowPresenter);

class CatalogDesktopWindowPresenter
    : public WindowCatalogDesktopPresenterMixin<
          ui::android::AndroidDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::CatalogDesktopWindowPresenter",
      CatalogDesktopWindowPresenter,
      ui::android::AndroidDesktopWindowPresenter, 0)

 protected:
  CatalogDesktopWindowPresenter() = default;

 public:
  explicit CatalogDesktopWindowPresenter(ae::ObjProp prop)
      : WindowCatalogDesktopPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }
};

class AndroidCatalogListContainerPresenter
    : public WindowCatalogListPresenterMixin<
          ui::android::AndroidListContainerPresenter> {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::AndroidCatalogListContainerPresenter",
      AndroidCatalogListContainerPresenter,
      ui::android::AndroidListContainerPresenter, 0)

 protected:
  AndroidCatalogListContainerPresenter() = default;

 public:
  explicit AndroidCatalogListContainerPresenter(ae::ObjProp prop)
      : WindowCatalogListPresenterMixin{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_, catalog);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_, catalog);
  }

  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override;
  void SubmitListRowActivatedFromUi(std::size_t row_index) override;
};

void AndroidCatalogListContainerPresenter::SubmitListRowSelectionChangedFromUi(
    std::size_t row_index) {
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionFromUi,
                      row_index);
}

void AndroidCatalogListContainerPresenter::SubmitListRowActivatedFromUi(
    std::size_t row_index) {
  model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenRow, row_index);
}

APPTRAVERSE_REGISTER(CatalogDesktopWindowPresenter);
APPTRAVERSE_REGISTER(AndroidCatalogListContainerPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

WindowCatalog::ptr BuildWindowCatalogGraph(ae::Domain& domain) {
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();

  WindowCatalogPresenterFactory factory;
  factory.create_window =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& catalog)
          -> ui::UiDesktopWindowPresenter::ptr {
        if (presenter_id.id() ==
            window_catalog_demo::ToObjId(
                window_catalog_demo::ObjId::MainWindowPresenter)) {
          return example::surfaces::AndroidCatalogMainWindowPresenter::ptr::
              Create(ae::CreateWith{build_domain}.with_id(presenter_id));
        }
        example::surfaces::CatalogDesktopWindowPresenter::ptr presenter =
            example::surfaces::CatalogDesktopWindowPresenter::ptr::Create(
                ae::CreateWith{build_domain}.with_id(presenter_id));
        presenter->catalog = catalog;
        return presenter;
      };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& catalog) {
        example::surfaces::AndroidCatalogListContainerPresenter::ptr presenter =
            example::surfaces::AndroidCatalogListContainerPresenter::ptr::Create(
                ae::CreateWith{build_domain}.with_id(presenter_id));
        presenter->catalog = catalog;
        return presenter;
      };
  factory.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::android::AndroidPushButtonPresenter::ptr::Create(
        presenter_id.is_valid()
            ? ae::CreateWith{build_domain}.with_id(presenter_id)
            : ae::CreateWith{build_domain});
  };

  WindowCatalogGraphOptions options;
  options.mobile_pager = true;
  options.mobile_close_in_prefab = true;
  return BuildWindowCatalogGraph(domain, factory, options);
}

void EnsureWindowCatalogAndroidGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
  (void)&example::surfaces::
      g_apptraverse_registrar_AndroidCatalogListContainerPresenter;
  (void)&example::surfaces::
      g_apptraverse_registrar_AndroidCatalogMainWindowPresenter;
}

}  // namespace apptraverse
