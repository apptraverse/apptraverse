#include "window_catalog_graph_web.h"

#include <emscripten.h>
#include <emscripten/em_js.h>

#include "window_catalog_graph_builder.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/web/web_ui_list_presenter.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::surfaces {
namespace {

// Tags the presenter's DOM window element with the model window ObjId so the
// shell pager can map ring ids to pages.
EM_JS(void, ApptraverseCatalogTagWindowElement,
      (emscripten::EM_VAL client_element, unsigned window_id, int is_main), {
        var client = Emval.toValue(client_element);
        var win = client ? client.parentNode : null;
        if (!win) {
          return;
        }
        win.dataset.windowId = String(window_id);
        win.classList.add('catalog-page');
        if (is_main) {
          win.classList.add('catalog-page-main');
        }
      });

}  // namespace

// Desktop window presenter for catalog pages (main + children). Pagination is
// shell-driven (see window_catalog_shell.html); this only exposes the window id.
class WebCatalogWindowPresenter : public ui::web::WebDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::WebCatalogWindowPresenter",
      WebCatalogWindowPresenter, ui::web::WebDesktopWindowPresenter, 0)

 protected:
  WebCatalogWindowPresenter() = default;

 public:
  explicit WebCatalogWindowPresenter(ae::ObjProp prop)
      : WebDesktopWindowPresenter{prop} {}

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& dnv) {
    dnv(base_);
  }

  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& dnv) const {
    dnv(base_);
  }

  void OnLoad() override {
    WebDesktopWindowPresenter::OnLoad();
    ae::ObjId const window_id = desktop_window().id();
    bool const is_main =
        window_id.id() ==
        window_catalog_demo::ToObjId(window_catalog_demo::ObjId::MainWindow);
    ApptraverseCatalogTagWindowElement(ClientMountHandle(), window_id.id(),
                                       is_main ? 1 : 0);
  }
};

class WebCatalogListContainerPresenter
    : public ui::web::WebListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::WebCatalogListContainerPresenter",
      WebCatalogListContainerPresenter, ui::web::WebListContainerPresenter, 0)

 protected:
  WebCatalogListContainerPresenter() = default;

 public:
  explicit WebCatalogListContainerPresenter(ae::ObjProp prop)
      : WebListContainerPresenter{prop} {}

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

  void SubmitListRowSelectionChangedFromUi(std::size_t row_index) override {
    model_proxy->Invoke(catalog->obj_id, &WindowCatalog::SubmitSelectionFromUi,
                        row_index);
  }

  void SubmitListRowActivatedFromUi(std::size_t row_index) override {
    model_proxy->Invoke(catalog->obj_id, &WindowCatalog::OpenRow, row_index);
  }
};

APPTRAVERSE_REGISTER(WebCatalogWindowPresenter);
APPTRAVERSE_REGISTER(WebCatalogListContainerPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

WindowCatalog::ptr BuildWebWindowCatalogGraph(ae::Domain& domain) {
  ui::EnsureUiPresenterRegistration();
  ui::web::EnsureWebUiPresenterRegistration();

  WindowCatalogPresenterFactory factory;
  factory.create_window = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return example::surfaces::WebCatalogWindowPresenter::ptr::Create(
        ae::CreateWith{build_domain}.with_id(presenter_id));
  };
  factory.create_list =
      [](ae::Domain& build_domain, ae::ObjId presenter_id,
         WindowCatalog::ptr const& catalog) {
        example::surfaces::WebCatalogListContainerPresenter::ptr presenter =
            example::surfaces::WebCatalogListContainerPresenter::ptr::Create(
                ae::CreateWith{build_domain}.with_id(presenter_id));
        presenter->catalog = catalog;
        return presenter;
      };
  factory.create_button = [](ae::Domain& build_domain, ae::ObjId presenter_id) {
    return ui::web::WebPushButtonPresenter::ptr::Create(
        presenter_id.is_valid()
            ? ae::CreateWith{build_domain}.with_id(presenter_id)
            : ae::CreateWith{build_domain});
  };

  // Same mobile pager model as Android: main page + one page per open child
  // window, Close button in the window prefab graph.
  WindowCatalogGraphOptions options;
  options.mobile_pager = true;
  options.mobile_close_in_prefab = true;
  return BuildWindowCatalogGraph(domain, factory, options);
}

void EnsureWebWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::web::EnsureWebUiPresenterRegistration();
  (void)&example::surfaces::g_apptraverse_registrar_WebCatalogWindowPresenter;
  (void)&example::surfaces::
      g_apptraverse_registrar_WebCatalogListContainerPresenter;
}

}  // namespace apptraverse
