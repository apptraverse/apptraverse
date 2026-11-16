#include "web_surface_presenter.h"

#include <emscripten.h>

#include "web_app.h"

namespace apptraverse {
namespace {

APPTRAVERSE_REGISTER(WebSurfacePresenter);

}  // namespace

void EnsureWebSurfacePresenterRegistration() {
  (void)&g_apptraverse_registrar_WebSurfacePresenter;
}

void WebSurfacePresenter::OnLoad() {
  std::uint32_t const surface_id = surface->obj_id.id();
  std::uint32_t const number = surface->number;
  // Tab button identity is the Surface ObjId — never a model pointer.
  // `>>> 0` keeps ObjIds above 0x7fffffff unsigned in JS (EM_ASM passes i32).
  EM_ASM(
      {
        var id = ($0) >>> 0;
        var tabs = document.getElementById('tab-strip');
        var button = document.createElement('button');
        button.type = 'button';
        button.className = 'surface-tab';
        button.dataset.surfaceId = String(id);
        button.textContent = 'Surface ' + $1;
        button.onclick = function() {
          Module['_AppTraverseWebSelectSurface'](id);
        };
        tabs.appendChild(button);
      },
      surface_id, number);
  WebApp::Instance().OnSurfacePageLoaded(surface_id, number,
                                         /*newly_created=*/true);
}

void WebSurfacePresenter::OnUnload() {
  std::uint32_t const surface_id = surface->obj_id.id();
  EM_ASM(
      {
        var id = ($0) >>> 0;
        var tabs = document.getElementById('tab-strip');
        var button = tabs.querySelector('[data-surface-id="' + id + '"]');
        if (button) {
          button.remove();
        }
      },
      surface_id);
  WebApp::Instance().OnSurfacePageUnloaded(surface_id);
}

}  // namespace apptraverse
