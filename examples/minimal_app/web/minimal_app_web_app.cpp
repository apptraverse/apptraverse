#include "apptraverse/platform/web/web_js_handle.h"
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
ae::RamDomainStorage g_user_storage, g_local_storage;
std::filesystem::path g_profile_path{kProfilePath};
ApplicationModelRuntime g_runtime;
ui::web::WebPresentationHost g_presentation_host;
int g_viewport_width = 0;
int g_viewport_height = 0;

APPTRAVERSE_EM_JS_HANDLE(ApptraverseGetAppMountElement, (), (), {
  return Emval.toHandle(document.getElementById('app-mount'));
});
EM_JS(void, ApptraverseShowMinimalPage, (emscripten::EM_VAL client, int visible, int width, int height, int inset), {
  var mount = Emval.toValue(Number(client)), win = mount.parentElement;
  win.style.display = visible ? "" : "none";
  if (width > 0 && height > 0) {
    win.style.left = inset + 'px'; win.style.top = inset + 'px';
    win.style.width = Math.max(1, width - 2 * inset) + 'px';
    win.style.height = Math.max(1, height - 2 * inset) + 'px';
    mount.style.width = win.style.width; mount.style.height = win.style.height;
  }
});

void ApplyPagePresentation(ae::Obj& root) {
  auto& application = static_cast<MinimalApplication&>(root);
  auto show = [&](ui::UiDesktopWindow& window) {
    auto& presenter = static_cast<ui::web::WebDesktopWindowPresenter&>(*window.presenter);
    bool const single_page = g_viewport_width > 0 && g_viewport_width < 600;
    presenter.OnModelChanged();
    ApptraverseShowMinimalPage(presenter.ClientMountHandle(),
        !single_page || window.obj_id == application.ActivePageId(),
        single_page ? g_viewport_width : 0, single_page ? g_viewport_height : 0, 0);
    presenter.RefreshLocalPresentation();
  };
  show(*application.main_window);
  if (application.auxiliary_presented_open) show(*application.auxiliary.window);
}

void SetSaveStatus(char const* text) {
  EM_ASM({ document.getElementById('save-status').textContent = UTF8ToString(Number($0)); },
         text);
}

void SetShutdownStatus(char const* text) {
  EM_ASM({
    document.getElementById('shutdown-status').textContent = UTF8ToString(Number($0));
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

void HideLoading() {
  EM_ASM({
    var loading = document.getElementById('loading');
    loading.textContent = "";
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
    EM_ASM({ console.log('MINIMAL_APP_SHUTDOWN_STOPPED'); });
    SetSaveStatus("");
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
  return SaveUserStorageFile(g_user_storage, g_profile_path) &&
      SaveUserStorageFile(g_local_storage, g_profile_path / "local") ? 0 : 1;
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseMinimalAppViewportChanged(int width, int height) {
  g_viewport_width = width; g_viewport_height = height;
  if (!g_host || !g_host->ui_session() ||
      g_host->lifecycle() != web::WebApplicationHostLifecycle::kRunning) return;
  ApplyPagePresentation(g_host->ui_session()->root());
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

  auto const environment = ui::web::ReadWebDeviceEnvironment();
  web::WebApplicationHostCallbacks callbacks;
  callbacks.refresh_presentation_host = [](ui::web::WebPresentationHost& host) {
    host.mount = ApptraverseGetAppMountElement();
  };
  callbacks.run_model = [environment](ApplicationPublishedCallback on_published) -> int {
#if defined(APPTRAVERSE_DEBUG_FRESH_START)
    g_profile_path /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
    EM_ASM({ console.log('MINIMAL_APP_DEV_BOOTSTRAP'); });
#else
    if (!LoadUserStorageFile(g_user_storage, g_profile_path) ||
        !LoadUserStorageFile(g_local_storage, g_profile_path / "local")) return 1;
    if (g_user_storage.state.empty()) EM_ASM({ console.log('MINIMAL_APP_LOADED_FROM_EMBEDDED_BACKING'); });
    else EM_ASM({ console.log('MINIMAL_APP_LOADED_FROM_PROFILE'); });
#endif

    ApplicationModelSessionHooks<MinimalApplication> hooks;
    hooks.prepare = [environment](MinimalApplication& application) {
      PrepareMinimalApplicationForRun(application);
      application.main_window->device_state->ObserveEnvironment(environment, ui::UiEnvironmentReason::kStartup);
    };
    hooks.persist = [](MinimalApplication& application) -> int {
      return PersistMinimalApplicationFromDomain(*application.domain);
    };
    int const session_result = RunEmbeddedApplicationModelSession(
        g_user_storage, g_local_storage, embedded_distilled::minimal::kEmbeddedGraph,
        g_runtime, std::move(on_published), hooks);
    return session_result;
  };
  callbacks.request_stop = [] { g_runtime.RequestStop(); };
  callbacks.capture_model_files = [](ae::Domain& domain, std::vector<web::WebPersistentFileSnapshot>& files) {
    auto application = domain.Find(MinimalApplicationRootId()).as<MinimalApplication>();
    files = {{(g_profile_path / "user.bin").generic_string(), {}},
             {(g_profile_path / "local" / "user.bin").generic_string(), {}}};
    return SaveMinimalApplicationEmbeddedUserState(*application) == 0 &&
           SerializeUserStorage(g_user_storage, files[0].bytes) && SerializeUserStorage(g_local_storage, files[1].bytes);
  };
  callbacks.after_initial = [](ae::Obj& root) {
    ApplyPagePresentation(root);
    ApptraverseMinimalAppViewportChanged(g_viewport_width, g_viewport_height);
    HideLoading();
    EM_ASM({ console.log('MINIMAL_APP_INITIAL_RENDER_OK'); });
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
