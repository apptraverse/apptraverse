#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include <emscripten/em_js.h>
#include <emscripten/threading.h>

#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/ui/ui_desktop_window.h"

namespace apptraverse::ui::web {

EM_JS(emscripten::EM_VAL, ApptraverseCreateDesktopWindowElement, (), {
  var windowEl = document.createElement('div');
  windowEl.className = 'apptraverse-desktop-window';
  windowEl.dataset.apptraverseDesktopWindow = '1';
  var clientEl = document.createElement('div');
  clientEl.className = 'apptraverse-desktop-client';
  windowEl.appendChild(clientEl);
  return Emval.toHandle(windowEl);
});

EM_JS(void, ApptraverseMountDesktopWindow, (emscripten::EM_VAL mount,
                                            emscripten::EM_VAL window_el), {
  Emval.toValue(mount).appendChild(Emval.toValue(window_el));
});

EM_JS(void, ApptraverseSetDesktopWindowGeometry, (emscripten::EM_VAL window_el,
                                                  int x, int y, int w, int h), {
  var el = Emval.toValue(window_el);
  var style = el.style;
  style.left = x + 'px';
  style.top = y + 'px';
  style.width = w + 'px';
  style.height = h + 'px';
  var client = el.firstChild;
  if (client) {
    client.style.width = w + 'px';
    client.style.height = h + 'px';
  }
});

EM_JS(void, ApptraverseRemoveDomNode, (emscripten::EM_VAL node), {
  var el = Emval.toValue(node);
  if (el && el.remove) {
    el.remove();
  }
});

struct WebDesktopWindowDomRefs {
  emscripten::EM_VAL window_element{nullptr};
};

WebDesktopWindowPresenter::~WebDesktopWindowPresenter() {
  if (dom_refs_ != nullptr && emscripten_is_main_browser_thread()) {
    OnUnload();
  }
  delete dom_refs_;
  dom_refs_ = nullptr;
}

void WebDesktopWindowPresenter::OnLoad() {
  if (!emscripten_is_main_browser_thread()) {
    return;
  }
  auto* host = static_cast<WebPresentationHost*>(presentation_host);
  if (host == nullptr || host->mount == nullptr) {
    return;
  }
  auto const desktop = desktop_window();
  if (!desktop) {
    return;
  }

  if (dom_refs_ == nullptr) {
    dom_refs_ = new WebDesktopWindowDomRefs();
  }

  dom_refs_->window_element = ApptraverseCreateDesktopWindowElement();
  ApptraverseMountDesktopWindow(host->mount, dom_refs_->window_element);
  SyncGeometryFromMirror();
}

void WebDesktopWindowPresenter::SyncGeometryFromMirror() {
  if (dom_refs_ == nullptr || dom_refs_->window_element == nullptr) {
    return;
  }
  auto const desktop = desktop_window();
  if (!desktop) {
    return;
  }
  ApptraverseSetDesktopWindowGeometry(dom_refs_->window_element, desktop->x(),
                                      desktop->y(), desktop->client_width(),
                                      desktop->client_height());
}

void WebDesktopWindowPresenter::OnModelChanged() { SyncGeometryFromMirror(); }

void WebDesktopWindowPresenter::OnUnload() {
  if (dom_refs_ == nullptr || dom_refs_->window_element == nullptr) {
    return;
  }
  ApptraverseRemoveDomNode(dom_refs_->window_element);
  dom_refs_->window_element = nullptr;
}

}  // namespace apptraverse::ui::web
