#include "window_catalog_web_app.h"

#include <string>
#include <utility>
#include <vector>

#include <emscripten.h>
#include <emscripten/em_js.h>
#include <emscripten/threading.h>

#include "embedded_distilled_window_catalog_web_supply.h"
#include "window_catalog_graph_web.h"
#include "window_catalog_ids.h"
#include "window_catalog_model.h"
#include "window_catalog_prepare.h"
#include "window_catalog_web_persistence.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

EM_JS(emscripten::EM_VAL, ApptraverseCatalogGetMountElement, (), {
  return Emval.toHandle(document.getElementById('catalog-mount'));
});

void SetStatus(char const* text) {
  EM_ASM({ AtCatalog.setStatus(UTF8ToString($0)); }, text);
}

void SetUiInputBlocked(bool blocked) {
  EM_ASM({ AtCatalog.setInputBlocked($0 ? true : false); }, blocked ? 1 : 0);
}

void EnsureBrowserEnvironment() {
  if (!emscripten_is_main_browser_thread()) {
    EM_ASM({
      throw new Error('WebWindowCatalogApp::Start must run on the browser main thread');
    });
  }
  if (!EM_ASM_INT({ return crossOriginIsolated ? 1 : 0; })) {
    EM_ASM({
      document.getElementById('status').textContent =
          'FAIL: crossOriginIsolated is false (need COOP/COEP headers)';
      throw new Error('crossOriginIsolated is false');
    });
  }
  if (!EM_ASM_INT({
        return (typeof SharedArrayBuffer !== 'undefined') ? 1 : 0;
      })) {
    EM_ASM({
      document.getElementById('status').textContent =
          'FAIL: SharedArrayBuffer unavailable';
      throw new Error('SharedArrayBuffer unavailable');
    });
  }
}

bool UserProfilePresent(std::filesystem::path const& state_dir) {
  return std::filesystem::exists(state_dir) &&
         !std::filesystem::is_empty(state_dir);
}

bool g_saving = false;
int g_last_viewport_w = 0;
int g_last_viewport_h = 0;

void FinishHostTeardownTrampoline(void* arg) {
  WebWindowCatalogApp::Instance().FinishTeardown(arg != nullptr);
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogRequestSave() {
  WebWindowCatalogApp::Instance().RequestSave();
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogRequestShutdown() {
  WebWindowCatalogApp::Instance().RequestShutdown();
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogMobileSwipe(int delta) {
  WebWindowCatalogApp::Instance().MobileSwipe(delta);
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogMobilePageShown(
    unsigned window_obj_id) {
  WebWindowCatalogApp::Instance().MobilePageShown(window_obj_id);
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogViewportChanged(int width,
                                                                  int height) {
  WebWindowCatalogApp::Instance().ViewportChanged(width, height);
}

}  // extern "C"

WebWindowCatalogApp& WebWindowCatalogApp::Instance() {
  static WebWindowCatalogApp app;
  return app;
}

void WebWindowCatalogApp::Start(std::filesystem::path state_dir) {
  EnsureObjectRegistration();
  EnsureWebWindowCatalogGraphRegistration();
  EnsureBrowserEnvironment();
  SetStatus("Loading…");

  state_dir_ = std::move(state_dir);

  web::WebApplicationHostCallbacks callbacks;
  callbacks.refresh_presentation_host = [](ui::web::WebPresentationHost& host) {
    host.mount = ApptraverseCatalogGetMountElement();
  };
  callbacks.run_model = [this](ApplicationPublishedCallback on_published) -> int {
    if (!user_storage_) {
      user_storage_ = std::make_unique<DirectoryDomainStorage>(state_dir_);
    }
    bool const user_profile_present = UserProfilePresent(state_dir_);
    if (user_profile_present) {
      EM_ASM({ console.log('WINDOW_CATALOG_LOADED_FROM_PROFILE'); });
    } else {
      EM_ASM({ console.log('WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING'); });
    }

    ApplicationModelSessionHooks<WindowCatalog> hooks;
    hooks.prepare = PrepareWindowCatalogForRun;
    hooks.before_incremental = [](WindowCatalog& catalog) {
      catalog.pending_focus_window = {};
      catalog.pending_mobile_active_window = {};
    };
    hooks.persist = [this](WindowCatalog& catalog) -> int {
      return PersistWindowCatalogEmbeddedUserOverlay(catalog, *user_storage_);
    };

    int const session_result = RunEmbeddedApplicationModelSession(
        *user_storage_, embedded_distilled::window_catalog_web::kEmbeddedGraph,
        runtime_, std::move(on_published), hooks);
    user_storage_.reset();
    return session_result;
  };
  callbacks.request_stop = [this] { runtime_.RequestStop(); };
  callbacks.after_initial = [this](ae::Obj& root) {
    EM_ASM({
      AtCatalog.setStatus('');
      console.log('WINDOW_CATALOG_INITIAL_RENDER_OK');
    });
    PublishMobilePages(root);
    ViewportChanged(g_last_viewport_w, g_last_viewport_h);
  };
  callbacks.after_incremental = [this](ae::Obj& root) { PublishMobilePages(root); };
  callbacks.persist_model = [this](ae::Domain& domain) -> int {
    return PersistWindowCatalogEmbeddedUserOverlayFromDomain(domain,
                                                             *user_storage_);
  };
  callbacks.on_save_started = [] {
    g_saving = true;
    SetStatus("Saving…");
  };
  callbacks.on_save_succeeded = [this] {
    g_saving = false;
    SetStatus("Saved");
    EM_ASM({ console.log('WINDOW_CATALOG_IDBFS_SAVED'); });
    ViewportChanged(g_last_viewport_w, g_last_viewport_h);
  };
  callbacks.on_save_failed = [this] {
    g_saving = false;
    SetStatus("Save failed");
    EM_ASM({ console.error('WINDOW_CATALOG_SAVE_FAILED'); });
    ViewportChanged(g_last_viewport_w, g_last_viewport_h);
  };
  callbacks.set_ui_input_blocked = SetUiInputBlocked;
  callbacks.on_shutdown_stopping = [] {
    SetStatus("Stopping…");
    EM_ASM({ console.log('WINDOW_CATALOG_SHUTDOWN_STOPPING'); });
  };
  callbacks.on_shutdown_succeeded = [] {
    emscripten_async_call(FinishHostTeardownTrampoline,
                          reinterpret_cast<void*>(1), 0);
  };
  callbacks.on_shutdown_failed = [] {
    emscripten_async_call(FinishHostTeardownTrampoline, nullptr, 0);
  };

  host_ = std::make_unique<web::WebApplicationHost>(
      runtime_, presentation_host_, std::move(callbacks));
  host_->StartWithPersistentStorage("/persistent");
}

void WebWindowCatalogApp::FinishTeardown(bool shutdown_succeeded) {
  if (presentation_host_.mount != nullptr) {
    ui::web::ReleaseOwnedEmvalHandle(presentation_host_.mount);
    presentation_host_.mount = nullptr;
  }
  host_.reset();
  EM_ASM({ AtCatalog.shutdownDone($0 ? true : false); },
         shutdown_succeeded ? 1 : 0);
}

void WebWindowCatalogApp::RequestSave() {
  if (host_) {
    host_->RequestSave();
  }
}

void WebWindowCatalogApp::RequestShutdown() {
  if (host_) {
    host_->RequestShutdown();
  }
}

void WebWindowCatalogApp::MobileSwipe(int delta) {
  if (!host_ || g_saving || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) {
    return;
  }
  host_->model_proxy().Invoke(
      CatalogRootId(), &WindowCatalog::SubmitMobileSwipeFromUi, delta);
}

void WebWindowCatalogApp::MobilePageShown(unsigned window_obj_id) {
  if (!host_ || g_saving || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) {
    return;
  }
  host_->model_proxy().Invoke(CatalogRootId(),
                              &WindowCatalog::SubmitMobileActiveFromUi,
                              ae::ObjId{window_obj_id});
}

void WebWindowCatalogApp::ViewportChanged(int width, int height) {
  g_last_viewport_w = width;
  g_last_viewport_h = height;
  if (width <= 0 || height <= 0) {
    return;
  }
  if (!host_ || g_saving || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) {
    return;
  }
  WindowCatalog& mirror =
      static_cast<WindowCatalog&>(host_->ui_session()->root());
  if (mirror.persisted.main_client_viewport.width == width &&
      mirror.persisted.main_client_viewport.height == height) {
    return;
  }
  host_->model_proxy().Invoke(
      CatalogRootId(), &WindowCatalog::SubmitMainClientViewportFromUi,
      static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
      static_cast<std::int32_t>(160));
}

void WebWindowCatalogApp::PublishMobilePages(ae::Obj& application_root) {
  auto& catalog = static_cast<WindowCatalog&>(application_root);
  if (!catalog.persisted.mobile_pager) {
    return;
  }
  std::vector<CatalogWindow::ptr> const ring = catalog.BuildMobileRing();
  std::string ids;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    if (i != 0) {
      ids += ',';
    }
    ids += std::to_string(ring[i].id().id());
  }
  CatalogWindow::ptr const active = catalog.ResolveMobileActiveWindow();
  EM_ASM({ AtCatalog.setPages(UTF8ToString($0), $1); }, ids.c_str(),
         active.id().id());
}

}  // namespace apptraverse
