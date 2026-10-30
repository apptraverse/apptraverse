#include "minimal_app_web_app.h"

#include <memory>

#include <emscripten.h>
#include <emscripten/em_js.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_application_host.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

#include "minimal_app_prepare.h"
#include "minimal_app_web_graph.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {
namespace {

std::unique_ptr<web::WebApplicationHost> g_host;
ae::RamDomainStorage g_model_storage;
ApplicationModelRuntime g_runtime;
ui::web::WebPresentationHost g_presentation_host;

EM_JS(emscripten::EM_VAL, ApptraverseGetAppMountElement, (), {
  return Emval.toHandle(document.getElementById('app-mount'));
});

void HideLoadingAndSignalRenderOk() {
  EM_ASM({
    var loading = document.getElementById('loading');
    if (loading) {
      loading.textContent = "";
    }
    console.log('MINIMAL_APP_INITIAL_RENDER_OK');
  });
}

void EnsureBrowserEnvironment() {
  if (!EM_ASM_INT({ return crossOriginIsolated ? 1 : 0; })) {
    EM_ASM({
      throw new Error('crossOriginIsolated is false (need COOP/COEP headers)');
    });
  }
  if (!EM_ASM_INT({ return (typeof SharedArrayBuffer !== 'undefined') ? 1 : 0; })) {
    EM_ASM({ throw new Error('SharedArrayBuffer unavailable'); });
  }
}

}  // namespace

void StartWebMinimalApplication() {
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();
  ui::web::EnsureWebUiPresenterRegistration();
  EnsureBrowserEnvironment();

  web::WebApplicationHostCallbacks callbacks;
  callbacks.refresh_presentation_host = [](ui::web::WebPresentationHost& host) {
    host.mount = ApptraverseGetAppMountElement();
  };
  callbacks.run_model = [](ApplicationPublishedCallback on_published) {
    {
      ae::Domain bootstrap_domain{g_model_storage};
      auto bootstrap = BuildWebMinimalApplicationGraph(bootstrap_domain);
      FinalizeDistilledGraph(*bootstrap);
      SaveDistilledRoot(*bootstrap);
    }

    ApplicationModelSessionHooks<MinimalApplication> hooks;
    hooks.prepare = [](MinimalApplication& application) {
      PrepareMinimalApplicationForRun(application);
    };
    hooks.persist = [](MinimalApplication& application) -> int {
      ui::SavePresentedWindow(application.main_window);
      MinimalApplication::ptr::MakeFromThis(&application).Save();
      return 0;
    };

    (void)RunApplicationModelSession(g_model_storage, MinimalApplicationRootId(),
                                     g_runtime, std::move(on_published), hooks);
  };
  callbacks.request_stop = [] { g_runtime.RequestStop(); };
  callbacks.after_initial = [](ae::Obj&) { HideLoadingAndSignalRenderOk(); };

  g_host = std::make_unique<web::WebApplicationHost>(
      g_runtime, g_presentation_host, std::move(callbacks));
  g_host->Start();
}

}  // namespace apptraverse::example::minimal
