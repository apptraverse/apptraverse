#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/ui/ui_desktop_window.h"

#include <emscripten/em_js.h>
#include <emscripten/threading.h>

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

EM_JS(emscripten::EM_VAL, ApptraverseDesktopClientElement, (emscripten::EM_VAL window_el), {
  return Emval.toHandle(Emval.toValue(window_el).firstChild);
});

EM_JS(void, ApptraverseMountDesktopWindow, (emscripten::EM_VAL mount,
                                            emscripten::EM_VAL window_el), {
  Emval.toValue(mount).appendChild(Emval.toValue(window_el));
});

EM_JS(void, ApptraverseSetDesktopWindowTitleDataset,
      (emscripten::EM_VAL window_el, const char* title_utf8), {
  Emval.toValue(window_el).dataset.apptraverseWindowTitle =
      UTF8ToString(title_utf8);
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

struct WebDesktopWindowDomRefs {
  emscripten::EM_VAL window_element{nullptr};
  emscripten::EM_VAL client_element{nullptr};
};

static void ReleaseDomRefs(WebDesktopWindowDomRefs& refs) {
  RemoveDomNode(refs.window_element);
  ReleaseOwnedEmvalHandle(refs.client_element);
  refs.client_element = nullptr;
  ReleaseOwnedEmvalHandle(refs.window_element);
  refs.window_element = nullptr;
}

void WebDesktopWindowPresenter::OnLoad() {
  if (!emscripten_is_main_browser_thread()) {
    WriteFatalStderr(
        "fatal: WebDesktopWindowPresenter::OnLoad off main thread\n");
    std::abort();
  }
  auto* host = static_cast<WebPresentationHost*>(presentation_host);
  if (host == nullptr || host->mount == nullptr) {
    WriteFatalStderr(
        "fatal: WebDesktopWindowPresenter::OnLoad missing presentation host "
        "mount\n");
    std::abort();
  }
  if (!desktop_window().is_valid()) {
    WriteFatalStderr(
        "fatal: WebDesktopWindowPresenter::OnLoad missing desktop window\n");
    std::abort();
  }

  dom_refs_ = new WebDesktopWindowDomRefs();
  dom_refs_->window_element = ApptraverseCreateDesktopWindowElement();
  dom_refs_->client_element =
      ApptraverseDesktopClientElement(dom_refs_->window_element);
  ApptraverseMountDesktopWindow(host->mount, dom_refs_->window_element);
  SyncGeometryFromMirror();
  SyncTitleFromMirror();
}

void WebDesktopWindowPresenter::SyncTitleFromMirror() {
  std::string title_text;
  if (title && title->strings.size() > 0) {
    int language_index = 0;
    if (locale) {
      language_index = locale->language_index();
    }
    title_text = title->Resolve(language_index);
  }
  ApptraverseSetDesktopWindowTitleDataset(dom_refs_->window_element,
                                          title_text.c_str());
}

void WebDesktopWindowPresenter::SyncGeometryFromMirror() {
  auto const desktop = desktop_window();
  ApptraverseSetDesktopWindowGeometry(dom_refs_->window_element, desktop->x(),
                                      desktop->y(), desktop->client_width(),
                                      desktop->client_height());
}

void WebDesktopWindowPresenter::OnModelChanged() {
  SyncGeometryFromMirror();
  SyncTitleFromMirror();
}

void WebDesktopWindowPresenter::OnUnload() {
  if (dom_refs_ == nullptr) {
    return;
  }
  ReleaseDomRefs(*dom_refs_);
  delete dom_refs_;
  dom_refs_ = nullptr;
}

void* WebDesktopWindowPresenter::NativeChildParentHandle() const {
  if (dom_refs_ == nullptr || dom_refs_->client_element == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<void*>(dom_refs_->client_element);
}

emscripten::EM_VAL WebDesktopWindowPresenter::ClientMountHandle() const {
  if (dom_refs_ == nullptr) {
    return nullptr;
  }
  return dom_refs_->client_element;
}

}  // namespace apptraverse::ui::web
