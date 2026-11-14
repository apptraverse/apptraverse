#include "minimal_app_web_app.h"

#include <filesystem>
#include <memory>

#include <emscripten.h>
#include <emscripten/em_js.h>

#include "apptraverse/application_model_session.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/web/web_application_host.h"
#include "apptraverse/platform/web/web_presentation_host.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"

#include "embedded_distilled_minimal_app_supply.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {
namespace {

constexpr char kIdbfsMountPath[] = "/persistent";
constexpr char kProfilePath[] = "/persistent/minimal_app_web_embedded_user";

std::unique_ptr<web::WebApplicationHost> g_host;
ae::RamDomainStorage g_user_storage;
std::filesystem::path g_profile_path{kProfilePath};
ApplicationModelRuntime g_runtime;
ui::web::WebPresentationHost g_presentation_host;
int g_viewport_width = 0;
int g_viewport_height = 0;

EM_JS(emscripten::EM_VAL, ApptraverseGetAppMountElement, (), {
  return Emval.toHandle(document.getElementById('app-mount'));
});
EM_JS(void, ApptraverseShowMinimalPage, (emscripten::EM_VAL client, int visible), {
  Emval.toValue(client).parentElement.style.display = visible ? '' : 'none';
});

void ApplyPagePresentation(ae::Obj& root) {
  auto& application = static_cast<MinimalApplication&>(root);
  auto show = [&](ui::UiDesktopWindow& window) {
    auto& presenter = static_cast<ui::web::WebDesktopWindowPresenter&>(*window.presenter);
    ApptraverseShowMinimalPage(presenter.ClientMountHandle(),
        !application.viewport.single_page || window.obj_id == application.ActivePageId());
  };
  show(*application.main_window);
  if (application.auxiliary_presented_open) show(*application.auxiliary.window);
}

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

void ReleasePresentationMountHandle() {
  if (g_presentation_host.mount == nullptr) {
    return;
  }
  ui::web::ReleaseOwnedEmvalHandle(g_presentation_host.mount);
  g_presentation_host.mount = nullptr;
}

void FinishHostTeardownOnNextMainTurn(bool shutdown_succeeded) {
  ReleasePresentationMountHandle();
  g_host.reset();
  if (shutdown_succeeded) {
    SetShutdownStatus("Stopped");
    SetSaveStatus("");
    EM_ASM({ console.log('MINIMAL_APP_SHUTDOWN_STOPPED'); });
  } else {
    SetShutdownStatus("Stop failed");
    EM_ASM({ console.error('MINIMAL_APP_SHUTDOWN_FAILED'); });
  }
}

void FinishHostTeardownTrampoline(void* arg) {
  FinishHostTeardownOnNextMainTurn(arg != nullptr);
}

int PersistMinimalApplicationFromDomain(ae::Domain& domain) {
  auto held = domain.Find(MinimalApplicationRootId());
  if (!held) {
    WriteFatalStderr("fatal: MinimalApplication missing from model domain\n");
    std::abort();
  }
  auto& application = static_cast<MinimalApplication&>(*held);
  int const saved = SaveMinimalApplicationEmbeddedUserState(application);
  if (saved != 0) return saved;
  return SaveUserStorageFile(g_user_storage, g_profile_path) ? 0 : 1;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseMinimalAppViewportChanged(int width, int height) {
  g_viewport_width = width; g_viewport_height = height;
  if (!g_host || !g_host->ui_session() ||
      g_host->lifecycle() != web::WebApplicationHostLifecycle::kRunning) return;
  g_host->model_proxy().Invoke(MinimalApplicationRootId(),
      &MinimalApplication::SubmitViewport, width, height, 160, false);
}

EMSCRIPTEN_KEEPALIVE void ApptraverseMinimalAppRequestSave() {
  if (g_host) {
    g_host->RequestSave();
  }
}

EMSCRIPTEN_KEEPALIVE void ApptraverseMinimalAppPageHidden() {
  if (!g_host || !g_host->ui_session() ||
      g_host->lifecycle() != web::WebApplicationHostLifecycle::kRunning) return;
  if (!g_host->PersistForPageLifecycle((g_profile_path / "user.bin").generic_string()))
    SetSaveStatus("Save failed");
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
#if defined(APPTRAVERSE_DEBUG_FRESH_START)
    g_profile_path /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
    EM_ASM({ console.log('MINIMAL_APP_DEBUG_FRESH_USER'); });
#else
    if (!LoadUserStorageFile(g_user_storage, g_profile_path)) return 1;
    if (!g_user_storage.state.empty()) {
      EM_ASM({ console.log('MINIMAL_APP_LOADED_FROM_PROFILE'); });
    } else {
      EM_ASM({ console.log('MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING'); });
    }
#endif

    ApplicationModelSessionHooks<MinimalApplication> hooks;
    hooks.prepare = PrepareMinimalApplicationForRun;
    hooks.persist = [](MinimalApplication& application) -> int {
      return PersistMinimalApplicationFromDomain(*application.domain);
    };
    int const session_result = RunEmbeddedApplicationModelSession(
        g_user_storage, embedded_distilled::minimal::kEmbeddedGraph,
        g_runtime, std::move(on_published), hooks);
    return session_result;
  };
  callbacks.request_stop = [] { g_runtime.RequestStop(); };
  callbacks.capture_model_state = [](ae::Domain& domain, std::vector<std::uint8_t>& bytes) {
    auto application = domain.Find(MinimalApplicationRootId()).as<MinimalApplication>();
    return SaveMinimalApplicationEmbeddedUserState(*application) == 0 &&
           SerializeUserStorage(g_user_storage, bytes);
  };
  callbacks.after_initial = [](ae::Obj& root) {
    ApplyPagePresentation(root);
    ApptraverseMinimalAppViewportChanged(g_viewport_width, g_viewport_height);
    HideLoadingAndSignalRenderOk();
  };
  callbacks.after_incremental = ApplyPagePresentation;
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
    emscripten_async_call(FinishHostTeardownTrampoline,
                          reinterpret_cast<void*>(1), 0);
  };
  callbacks.on_shutdown_failed = [] {
    emscripten_async_call(FinishHostTeardownTrampoline, nullptr, 0);
  };

  g_host = std::make_unique<web::WebApplicationHost>(
      g_runtime, g_presentation_host, std::move(callbacks));
  g_host->StartWithPersistentStorage(kIdbfsMountPath);
}

}  // namespace apptraverse::example::minimal
