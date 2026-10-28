#include "window_catalog_graph_web.h"

#include <string>

#include <emscripten.h>

#include "window_catalog_graph_builder.h"
#include "window_catalog_model.h"

#include "apptraverse/model_object_proxy.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/ui/ui_localization_string.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/ui/ui_push_button_presenter.h"

namespace apptraverse::example::surfaces {
namespace {

std::string LocalizedUtf8(ui::UiLocalizationString& localized, int language) {
  return localized.Resolve(language);
}

int LanguageIndex(ui::UiWindowPresenter const& window_presenter) {
  window_presenter.locale.Load();
  return window_presenter.locale->language_index();
}

}  // namespace

class WebCatalogWindowPresenter : public ui::UiDesktopWindowPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::WebCatalogWindowPresenter",
      WebCatalogWindowPresenter, ui::UiDesktopWindowPresenter, 0)

 protected:
  WebCatalogWindowPresenter() = default;

 public:
  explicit WebCatalogWindowPresenter(ae::ObjProp prop)
      : UiDesktopWindowPresenter{prop} {}

  void OnLoad() override {
    title.Load();
    int const language = LanguageIndex(*this);
    std::string const title_text = LocalizedUtf8(*title, language);
    EM_ASM(
        {
          var el = document.getElementById('catalog-title');
          if (el) {
            el.textContent = UTF8ToString($0);
          }
        },
        title_text.c_str());
    presentation_loaded = true;
  }
};

class WebCatalogListContainerPresenter : public ui::UiListContainerPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::WebCatalogListContainerPresenter",
      WebCatalogListContainerPresenter, ui::UiListContainerPresenter, 0)

 protected:
  WebCatalogListContainerPresenter() = default;

 public:
  explicit WebCatalogListContainerPresenter(ae::ObjProp prop)
      : UiListContainerPresenter{prop} {}

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

  void OnLoad() override { RenderList(); }

  void OnModelChanged() override { RenderList(); }

 private:
  void RenderList() {
    container.Load();
    window_presenter.Load();
    int const language = LanguageIndex(*window_presenter);
    std::string rows_html;
    for (auto const& row : container->rows) {
      row.Load();
      auto* holder = static_cast<WindowHolder*>(row.operator->());
      holder->primary_text.Load();
      std::string const label = LocalizedUtf8(*holder->primary_text, language);
      rows_html += "<li>";
      rows_html += label;
      rows_html += "</li>";
    }
    EM_ASM(
        {
          var list = document.getElementById('catalog-list');
          if (list) {
            list.innerHTML = UTF8ToString($0);
          }
        },
        rows_html.c_str());
  }
};

class WebCatalogPushButtonPresenter : public ui::UiPushButtonPresenter {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::surfaces::WebCatalogPushButtonPresenter",
      WebCatalogPushButtonPresenter, ui::UiPushButtonPresenter, 0)

 protected:
  WebCatalogPushButtonPresenter() = default;

 public:
  explicit WebCatalogPushButtonPresenter(ae::ObjProp prop)
      : UiPushButtonPresenter{prop} {}

  void OnLoad() override {
    button.Load();
    window_presenter.Load();
    int const language = LanguageIndex(*window_presenter);
    button->label.Load();
    std::string const label = LocalizedUtf8(*button->label, language);
    bool const enabled = button->enabled();
    EM_ASM(
        {
          var host = document.getElementById('catalog-buttons');
          if (!host) {
            return;
          }
          var button = document.createElement('button');
          button.type = 'button';
          button.className = 'catalog-button';
          button.dataset.controlId = String($0);
          button.textContent = UTF8ToString($1);
          button.disabled = $2 ? false : true;
          button.style.marginRight = '8px';
          host.appendChild(button);
        },
        static_cast<int>(control_id), label.c_str(), enabled ? 1 : 0);
  }
};

APPTRAVERSE_REGISTER(WebCatalogWindowPresenter);
APPTRAVERSE_REGISTER(WebCatalogListContainerPresenter);
APPTRAVERSE_REGISTER(WebCatalogPushButtonPresenter);

}  // namespace apptraverse::example::surfaces

namespace apptraverse {

WindowCatalog::ptr BuildWebWindowCatalogGraph(ae::Domain& domain) {
  ui::EnsureUiPresenterRegistration();

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
  factory.create_button = [](ae::Domain& build_domain) {
    return example::surfaces::WebCatalogPushButtonPresenter::ptr::Create(
        ae::CreateWith{build_domain});
  };

  return BuildWindowCatalogGraph(domain, factory);
}

void EnsureWebWindowCatalogGraphRegistration() {
  EnsureWindowCatalogModelRegistration();
  ui::EnsureUiPresenterRegistration();
  (void)&example::surfaces::g_apptraverse_registrar_WebCatalogWindowPresenter;
  (void)&example::surfaces::
      g_apptraverse_registrar_WebCatalogListContainerPresenter;
  (void)&example::surfaces::g_apptraverse_registrar_WebCatalogPushButtonPresenter;
}

}  // namespace apptraverse
