#include "minimal_app_web_app.h"

#include <memory>

#include <emscripten.h>
#include <emscripten/em_js.h>

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_application_host.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_app_web_graph.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {
namespace {

constexpr char kIdbfsMountPath[] = "/persistent";
constexpr char kProfilePath[] = "/persistent/minimal_app_web_s1_dual_window_state";

std::unique_ptr<web::WebApplicationHost> g_host;
std::unique_ptr<DirectoryDomainStorage> g_durable_storage;
ApplicationModelRuntime g_runtime;
ui::web::WebPresentationHost g_presentation_host;

EM_JS(emscripten::EM_VAL, ApptraverseGetAppMountElement, (), {
  return Emval.toHandle(document.getElementById('app-mount'));
});

void SetSaveStatus(char const* text) {
  EM_ASM({ document.getElementById('save-status').textContent = UTF8ToString($0); },
         text);
}

void SetShutdownStatus(char const* text) {
  EM_ASM({
    document.getElementById('shutdown-status').textContent = UTF8ToString($0);
  }, text);
}

void SetUiInputBlocked(bool blocked) {
  EM_ASM({
    var mount = document.getElementById('app-mount');
    var save = document.getElementById('save-button');
    var stop = document.getElementById('stop-button');
    if ($0) {
      mount.inert = true;
      save.disabled = true;
      stop.disabled = true;
    } else {
      mount.inert = false;
      save.disabled = false;
      stop.disabled = false;
    }
  }, blocked ? 1 : 0);
}

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

int PersistMinimalApplicationFromDomain(ae::Domain& domain) {
  auto held = domain.Find(MinimalApplicationRootId());
  if (!held) {
    WriteFatalStderr("fatal: MinimalApplication missing from model domain\n");
    std::abort();
  }
  auto& application = static_cast<MinimalApplication&>(*held);
  return PersistMinimalApplication(application, *g_durable_storage);
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseMinimalAppRequestSave() {
  if (g_host) {
    g_host->RequestSave();
  }
}

EMSCRIPTEN_KEEPALIVE void ApptraverseMinimalAppRequestShutdown() {
  if (g_host) {
    g_host->RequestShutdown();
  }
}

}  // extern "C"

void StartWebMinimalApplication() {
  EnsureObjectRegistration();
  EnsureMinimalApplicationRegistration();
  ui::web::EnsureWebUiPresenterRegistration();
  EnsureBrowserEnvironment();

  web::WebApplicationHostCallbacks callbacks;
  callbacks.refresh_presentation_host = [](ui::web::WebPresentationHost& host) {
    host.mount = ApptraverseGetAppMountElement();
  };
  callbacks.run_model = [](ApplicationPublishedCallback on_published) -> int {
    if (!g_durable_storage) {
      g_durable_storage =
          std::make_unique<DirectoryDomainStorage>(kProfilePath);
    }
    bool const bootstrapped = BootstrapMinimalApplicationProfileIfAbsent(
        g_durable_storage->root(), [](ae::Domain& domain) {
          return BuildWebMinimalApplicationGraph(domain);
        });
    if (bootstrapped) {
      EM_ASM({ console.log('MINIMAL_APP_DEV_BOOTSTRAP'); });
    } else {
      EM_ASM({ console.log('MINIMAL_APP_LOADED_FROM_PROFILE'); });
    }

    ApplicationModelSessionHooks<MinimalApplication> hooks;
    hooks.prepare = [](MinimalApplication& application) {
      PrepareMinimalApplicationForRun(application);
    };
    hooks.persist = [](MinimalApplication& application) -> int {
      return PersistMinimalApplication(application, *g_durable_storage);
    };

    return RunApplicationModelSession(*g_durable_storage, MinimalApplicationRootId(),
                                      g_runtime, std::move(on_published), hooks);
  };
  callbacks.request_stop = [] { g_runtime.RequestStop(); };
  callbacks.after_initial = [](ae::Obj&) { HideLoadingAndSignalRenderOk(); };
  callbacks.persist_model = [](ae::Domain& domain) -> int {
    return PersistMinimalApplicationFromDomain(domain);
  };
  callbacks.on_save_started = [] { SetSaveStatus("Saving…"); };
  callbacks.on_save_succeeded = [] {
    SetSaveStatus("Saved");
    EM_ASM({ console.log('MINIMAL_APP_IDBFS_SAVED'); });
  };
  callbacks.on_save_failed = [] {
    SetSaveStatus("Save failed");
    EM_ASM({ console.error('MINIMAL_APP_SAVE_FAILED'); });
  };
  callbacks.set_ui_input_blocked = SetUiInputBlocked;
  callbacks.on_shutdown_stopping = [] {
    SetShutdownStatus("Stopping…");
    EM_ASM({ console.log('MINIMAL_APP_SHUTDOWN_STOPPING'); });
  };
  callbacks.on_shutdown_succeeded = [] {
    SetShutdownStatus("Stopped");
    SetSaveStatus("");
    EM_ASM({ console.log('MINIMAL_APP_SHUTDOWN_STOPPED'); });
  };
  callbacks.on_shutdown_failed = [] {
    SetShutdownStatus("Stop failed");
    EM_ASM({ console.error('MINIMAL_APP_SHUTDOWN_FAILED'); });
  };

  g_host = std::make_unique<web::WebApplicationHost>(
      g_runtime, g_presentation_host, std::move(callbacks));
  g_host->StartWithPersistentStorage(kIdbfsMountPath);
}

}  // namespace apptraverse::example::minimal
