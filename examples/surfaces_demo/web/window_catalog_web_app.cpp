#include "window_catalog_web_app.h"

#include <memory>

#include <emscripten.h>
#include <emscripten/em_js.h>

#include "window_catalog_graph_web.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/web/web_application_host.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse::example::surfaces {
namespace {

constexpr char kIdbfsMountPath[] = "/persistent";
constexpr char kProfilePath[] =
    "/persistent/window_catalog_web_five_backend_state";

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

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

void SetUiInputBlocked(bool blocked) {
  EM_ASM({
    var mount = document.getElementById('app-mount');
    var save = document.getElementById('save-button');
    if ($0) {
      mount.inert = true;
      if (save) {
        save.disabled = true;
      }
    } else {
      mount.inert = false;
      if (save) {
        save.disabled = false;
      }
    }
  }, blocked ? 1 : 0);
}

void HideLoadingAndSignalRenderOk() {
  EM_ASM({
    var loading = document.getElementById('loading');
    if (loading) {
      loading.textContent = "";
    }
    console.log('WINDOW_CATALOG_INITIAL_RENDER_OK');
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

#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
void BootstrapDistilledProfileIfAbsent() {
  std::error_code profile_error;
  bool const profile_exists =
      std::filesystem::exists(g_durable_storage->root(), profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile\n");
    std::abort();
  }
  bool const profile_absent =
      !profile_exists ||
      std::filesystem::is_empty(g_durable_storage->root(), profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect catalog profile contents\n");
    std::abort();
  }
  if (!profile_absent) {
    return;
  }
  ae::Domain bootstrap_domain{*g_durable_storage};
  auto bootstrap_catalog = BuildWebWindowCatalogGraph(bootstrap_domain);
  FinalizeDistilledGraph(*bootstrap_catalog);
  StabilizeDistilledPersistentSnapshot(*bootstrap_catalog);
  SaveDistilledRoot(*bootstrap_catalog);
}
#endif

int PersistWindowCatalogFromDomain(ae::Domain& domain) {
  auto held = domain.Find(CatalogRootId());
  if (!held) {
    WriteFatalStderr("fatal: WindowCatalog missing from model domain\n");
    std::abort();
  }
  auto& catalog = static_cast<WindowCatalog&>(*held);
  g_durable_storage->BeginSaveSession();
  SaveWindowCatalogPersistentSnapshot(catalog);
  return g_durable_storage->SaveSessionHadFailure() ? 1 : 0;
}

void HandleWebDesktopWindowUserClose(void* opaque, ae::Obj* desktop_window) {
  if (desktop_window == nullptr || opaque == nullptr) {
    return;
  }
  auto* host = static_cast<web::WebApplicationHost*>(opaque);
  if (host->ui_session() == nullptr) {
    return;
  }
  auto& catalog = static_cast<WindowCatalog&>(host->ui_session()->root());
  if (catalog.main_window.id() == desktop_window->obj_id) {
    g_runtime.RequestStop();
    return;
  }
  host->model_proxy().Invoke(catalog.obj_id,
                             &WindowCatalog::UserCloseWindow,
                             desktop_window->obj_id);
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogRequestSave() {
  if (g_host) {
    g_host->RequestSave();
  }
}

}  // extern "C"

void StartWebWindowCatalogApplication() {
  EnsureObjectRegistration();
  EnsureWebWindowCatalogGraphRegistration();
  ui::web::EnsureWebUiPresenterRegistration();
  EnsureBrowserEnvironment();

  g_presentation_host.on_desktop_window_user_close = HandleWebDesktopWindowUserClose;

  web::WebApplicationHostCallbacks callbacks;
  callbacks.refresh_presentation_host = [](ui::web::WebPresentationHost& host) {
    host.mount = ApptraverseGetAppMountElement();
    host.on_desktop_window_user_close =
        g_presentation_host.on_desktop_window_user_close;
    host.opaque = g_presentation_host.opaque;
  };
  callbacks.run_model = [](ApplicationPublishedCallback on_published) {
    if (!g_durable_storage) {
      g_durable_storage =
          std::make_unique<DirectoryDomainStorage>(kProfilePath);
    }
    bool const bootstrapped = [&] {
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
      std::error_code profile_error;
      bool const profile_exists =
          std::filesystem::exists(g_durable_storage->root(), profile_error);
      if (profile_error) {
        WriteFatalStderr("fatal: cannot inspect catalog profile\n");
        std::abort();
      }
      bool const profile_absent =
          !profile_exists ||
          std::filesystem::is_empty(g_durable_storage->root(), profile_error);
      if (profile_absent) {
        BootstrapDistilledProfileIfAbsent();
        return true;
      }
#endif
      return false;
    }();
    if (bootstrapped) {
      EM_ASM({ console.log('WINDOW_CATALOG_DEV_BOOTSTRAP'); });
    } else {
      EM_ASM({ console.log('WINDOW_CATALOG_LOADED_FROM_PROFILE'); });
    }

    ApplicationModelSessionHooks<WindowCatalog> hooks;
    hooks.prepare = PrepareWindowCatalogForRun;
    hooks.before_incremental = [](WindowCatalog& catalog) {
      catalog.pending_focus_window = {};
    };
    hooks.persist = [](WindowCatalog& catalog) -> int {
      g_durable_storage->BeginSaveSession();
      SaveWindowCatalogPersistentSnapshot(catalog);
      return g_durable_storage->SaveSessionHadFailure() ? 1 : 0;
    };

    ae::Domain domain{*g_durable_storage};
    ae::DomainGraph load_graph{&domain};
    ae::Ptr<ae::Obj> root_obj;
    if (!load_graph.LoadRoot(CatalogRootId(), root_obj).IsOk()) {
      WriteFatalStderr("fatal: WindowCatalog LoadRoot failed\n");
      std::abort();
    }
    ae::Ptr<WindowCatalog> catalog_obj{root_obj};
    hooks.prepare(*catalog_obj);
    WindowCatalog* catalog_ptr = catalog_obj.get();
    BeforeIncrementalPublicationCallback before_inc;
    if (hooks.before_incremental) {
      before_inc = [catalog_ptr, &hooks]() {
        hooks.before_incremental(*catalog_ptr);
      };
    }
    (void)g_runtime.Run(domain, *catalog_obj, std::move(on_published),
                        before_inc);
  };
  callbacks.request_stop = [] { g_runtime.RequestStop(); };
  callbacks.after_initial = [](ae::Obj&) { HideLoadingAndSignalRenderOk(); };
  callbacks.persist_model = [](ae::Domain& domain) -> int {
    return PersistWindowCatalogFromDomain(domain);
  };
  callbacks.on_save_started = [] { SetSaveStatus("Saving…"); };
  callbacks.on_save_succeeded = [] {
    SetSaveStatus("Saved");
    EM_ASM({ console.log('WINDOW_CATALOG_IDBFS_SAVED'); });
  };
  callbacks.on_save_failed = [] {
    SetSaveStatus("Save failed");
    EM_ASM({ console.error('WINDOW_CATALOG_SAVE_FAILED'); });
  };
  callbacks.set_ui_input_blocked = SetUiInputBlocked;

  g_host = std::make_unique<web::WebApplicationHost>(
      g_runtime, g_presentation_host, std::move(callbacks));
  g_presentation_host.opaque = g_host.get();

  g_host->StartWithPersistentStorage(kIdbfsMountPath);
}

}  // namespace apptraverse::example::surfaces
