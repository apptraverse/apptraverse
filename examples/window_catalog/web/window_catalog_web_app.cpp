#include "apptraverse/platform/web/web_js_handle.h"
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
#include "window_catalog_menu.h"
#include "window_catalog_prepare.h"
#include "window_catalog_web_persistence.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/client_local_state.h"
#include "apptraverse/runtime_node.h"

namespace apptraverse {
namespace {

ae::ObjId CatalogRootId() {
  return ae::ObjId{
      window_catalog_demo::ToObjId(window_catalog_demo::ObjId::Catalog)};
}

APPTRAVERSE_EM_JS_HANDLE(ApptraverseCatalogGetMountElement, (), (), {
  return Emval.toHandle(document.getElementById('catalog-mount'));
});

void SetStatus(char const* text) {
  EM_ASM({ AtCatalog.setStatus(UTF8ToString(Number($0))); }, text);
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

int g_last_viewport_w = 0;
int g_last_viewport_h = 0;

void FinishHostTeardownTrampoline(void* arg) {
  WebWindowCatalogApp::Instance().FinishTeardown(arg != nullptr);
}

}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogMenuCommand(unsigned group, unsigned item) {
  WebWindowCatalogApp::Instance().ExecuteMenuCommand(group, item);
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogLayoutCommand(unsigned item) {
  WebWindowCatalogApp::Instance().ExecuteLayoutCommand(item);
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogFullscreen(
    unsigned window_id, int fullscreen) {
  WebWindowCatalogApp::Instance().RequestFullscreen(window_id, fullscreen != 0);
}

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogRequestShutdown() {
  WebWindowCatalogApp::Instance().RequestShutdown();
}

EMSCRIPTEN_KEEPALIVE int ApptraverseWindowCatalogPageHidden() {
  return WebWindowCatalogApp::Instance().PersistForPageLifecycle() ? 1 : 0;
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

EMSCRIPTEN_KEEPALIVE void ApptraverseWindowCatalogChildViewportChanged(
    unsigned window_id, int width, int height) {
  WebWindowCatalogApp::Instance().ChildViewportChanged(window_id, width, height);
}

}  // extern "C"

WebWindowCatalogApp& WebWindowCatalogApp::Instance() {
  static WebWindowCatalogApp app;
  return app;
}

void WebWindowCatalogApp::Start(std::filesystem::path state_dir) {
  EnsureWebWindowCatalogGraphRegistration();
  EnsureBrowserEnvironment();
  SetStatus("");

  state_dir_ = std::move(state_dir);

  auto const environment = ui::web::ReadWebDeviceEnvironment();
  web::WebApplicationHostCallbacks callbacks;
  callbacks.refresh_presentation_host = [](ui::web::WebPresentationHost& host) {
    host.mount = ApptraverseCatalogGetMountElement();
  };
  callbacks.run_model = [this, environment](ApplicationPublishedCallback on_published) -> int {
#if defined(APPTRAVERSE_WEB_CATALOG_FRESH_USER)
    state_dir_ /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
    state_dir_ /= "user";
#endif
#if !defined(APPTRAVERSE_WEB_CATALOG_FRESH_USER)
    if (!LoadUserStorageFile(user_storage_, state_dir_) ||
        !LoadUserStorageFile(local_storage_, state_dir_ / "local")) return 1;
    if (local_storage_.state.empty()) {
      // Optional initial LOCAL seed is a whole native object partition.
      // Existing LOCAL bypasses seed reads and wins in full.
      auto seed_dir = std::filesystem::path{"/persistent/local-seed"};
      ae::RamDomainStorage seed;
      if (!LoadUserStorageFile(seed, seed_dir)) return 1;
      SeedClientLocalStorage(local_storage_, seed);
    }
#ifndef NDEBUG
    if (!user_storage_.state.empty()) {
      EM_ASM({ console.log('WINDOW_CATALOG_LOADED_FROM_PROFILE'); });
    } else {
      EM_ASM({ console.log('WINDOW_CATALOG_LOADED_FROM_EMBEDDED_BACKING'); });
    }
#endif
#else
    EM_ASM({ console.log('WINDOW_CATALOG_DEBUG_FRESH_USER'); });
#endif

    ApplicationModelSessionHooks<WindowCatalog> hooks;
    hooks.prepare = [environment](WindowCatalog& catalog) {
      catalog.persisted.device_state.Load();
      catalog.persisted.device_state->ObserveEnvironment(environment, ui::UiEnvironmentReason::kStartup);
      PrepareWindowCatalogForRun(catalog);
      catalog.EnableLocalWindowLifecycle();
      PrepareCatalogMenuForRun(catalog);
    };
    hooks.before_incremental = [](WindowCatalog& catalog) {
      catalog.pending_focus_window = {};
    };
    hooks.persist = [this](WindowCatalog& catalog) -> int {
      SaveWindowCatalogPersistentSnapshot(catalog);
      return SaveUserStorageFile(user_storage_, state_dir_) &&
             SaveUserStorageFile(local_storage_, state_dir_ / "local") ? 0 : 1;
    };

    int const session_result = RunEmbeddedApplicationModelSession(
        user_storage_, local_storage_, embedded_distilled::window_catalog_web::kEmbeddedGraph,
        runtime_, std::move(on_published), hooks);
    return session_result;
  };
  callbacks.request_stop = [this] { runtime_.RequestStop(); };
  callbacks.capture_model_files = [this](ae::Domain& domain,
      std::vector<web::WebPersistentFileSnapshot>& files) {
    auto catalog = domain.Find(CatalogRootId()).as<WindowCatalog>();
    SaveWindowCatalogPersistentSnapshot(*catalog);
    files = {{(state_dir_ / "user.bin").generic_string(), {}},
             {(state_dir_ / "local" / "user.bin").generic_string(), {}}};
    return SerializeUserStorage(user_storage_, files[0].bytes) &&
           SerializeUserStorage(local_storage_, files[1].bytes);
  };
  callbacks.after_initial = [this](ae::Obj& root) {
    SetStatus("");
#ifndef NDEBUG
    EM_ASM({ console.log('WINDOW_CATALOG_INITIAL_RENDER_OK'); });
#endif
    PublishMobilePages(root);
    SetUiInputBlocked(false);
    ViewportChanged(g_last_viewport_w, g_last_viewport_h);
  };
  callbacks.after_incremental = [this](ae::Obj& root) { PublishMobilePages(root); };
  callbacks.set_ui_input_blocked = SetUiInputBlocked;
  callbacks.on_shutdown_stopping = [] {
    EM_ASM({ AtCatalog.setStatusKey(4); });
#ifndef NDEBUG
    EM_ASM({ console.log('WINDOW_CATALOG_SHUTDOWN_STOPPING'); });
#endif
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

void WebWindowCatalogApp::ExecuteMenuCommand(unsigned group, unsigned item) {
  if (!host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) return;
  auto& catalog = static_cast<WindowCatalog&>(host_->ui_session()->root());
  host_->model_proxy().Invoke(catalog.persisted.device_state.id(),
      &CatalogUiState::ExecuteMenuCommand, group, item);
}

void WebWindowCatalogApp::ExecuteLayoutCommand(unsigned item) {
  if (!host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) return;
  auto& catalog = static_cast<WindowCatalog&>(host_->ui_session()->root());
  host_->model_proxy().Invoke(catalog.persisted.device_state.id(),
      &CatalogUiState::ExecuteLayoutCommand, item);
}

void WebWindowCatalogApp::RequestFullscreen(unsigned window_id, bool fullscreen) {
  if (!host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) return;
  auto& catalog = static_cast<WindowCatalog&>(host_->ui_session()->root());
  host_->model_proxy().Invoke(catalog.persisted.device_state.id(),
      &CatalogUiState::RequestWindowFullscreenFromUi, ae::ObjId{window_id}, fullscreen);
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

void WebWindowCatalogApp::RequestShutdown() {
  // A tab can disappear in the same event turn as this button click. Queue the
  // complete USER transaction before stopping; the normal async shutdown still
  // acknowledges persistence and releases the model/UI while the page lives.
  if (host_ && PersistForPageLifecycle()) {
    host_->RequestShutdown();
  }
}

bool WebWindowCatalogApp::PersistForPageLifecycle() {
  if (!host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) {
    return true;
  }
  bool const saved = host_->PersistForPageLifecycle(
      (state_dir_ / "user.bin").generic_string());
  if (!saved) EM_ASM({ AtCatalog.setStatusKey(7); });
  return saved;
}

void WebWindowCatalogApp::MobileSwipe(int delta) {
  if (!host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) {
    return;
  }
  host_->model_proxy().Invoke(
      CatalogRootId(), &WindowCatalog::SubmitWindowNavigationFromUi, delta);
}

void WebWindowCatalogApp::MobilePageShown(unsigned window_obj_id) {
  if (!host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) {
    return;
  }
  host_->model_proxy().Invoke(CatalogRootId(),
                              &WindowCatalog::SubmitActiveWindowFromUi,
                              ae::ObjId{window_obj_id});
}

void WebWindowCatalogApp::ViewportChanged(int width, int height) {
  g_last_viewport_w = width;
  g_last_viewport_h = height;
  if (width <= 0 || height <= 0) {
    return;
  }
  if (!host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) {
    return;
  }
  auto& mirror = static_cast<WindowCatalog&>(host_->ui_session()->root());
  static_cast<ui::web::WebDesktopWindowPresenter*>(mirror.persisted.main_window->presenter.operator->())->RefreshLocalPresentation(true);
}

void WebWindowCatalogApp::ChildViewportChanged(unsigned window_id, int width, int height) {
  if (width <= 0 || height <= 0 || !host_ || !host_->ui_session() ||
      host_->lifecycle() != web::WebApplicationHostLifecycle::kRunning) return;
  auto& mirror = static_cast<WindowCatalog&>(host_->ui_session()->root());
  for (auto const& row : mirror.persisted.list->rows) {
    auto const& holder = static_cast<WindowHolder const&>(*row);
    if (holder.window.is_loaded() && holder.window.id().id() == window_id)
      static_cast<ui::web::WebDesktopWindowPresenter*>(holder.window->presenter.operator->())->RefreshLocalPresentation();
  }
}

void WebWindowCatalogApp::PublishMobilePages(ae::Obj& application_root) {
  auto& catalog = static_cast<WindowCatalog&>(application_root);
  auto const preferences = catalog.persisted.device_state->appearance;
  auto const& state = static_cast<CatalogUiState const&>(*catalog.persisted.device_state);
  int const language = catalog.persisted.locale->language_index();
  using emscripten::val;
  auto groups = val::array();
  for (auto const& group : state.menu->groups) {
    auto native_group = val::object();
    native_group.set("label", group.label->Resolve(language));
    auto items = val::array();
    for (auto const& item : group.items) {
      auto native_item = val::object();
      native_item.set("label", item.label->Resolve(language));
      native_item.set("selected", state.MenuItemSelected(item));
      items.call<void>("push", native_item);
    }
    native_group.set("items", items);
    groups.call<void>("push", native_group);
  }
  auto strings = val::array();
  for (auto const& text : state.menu->shell_strings)
    strings.call<void>("push", text->Resolve(language));
  val::global("AtCatalog").call<void>("setMenus", groups, strings, language);
  EM_ASM({ AtCatalog.setAppearance($0, !!$1); },
      static_cast<int>(preferences.theme), preferences.soften_dark);
  std::vector<CatalogWindow::ptr> const ring = catalog.BuildWindowNavigationRing();
  std::string ids;
  for (std::size_t i = 0; i < ring.size(); ++i) {
    if (i != 0) {
      ids += ',';
    }
    ids += std::to_string(ring[i].id().id());
  }
  CatalogWindow::ptr const active = catalog.ResolveActiveWindow();
  auto layout_items = val::array();
  for (auto const& item : state.menu->layout_items) {
    auto choice = val::object();
    choice.set("label", item.label->Resolve(language));
    choice.set("selected", state.MenuItemSelected(item));
    layout_items.call<void>("push", choice);
  }
  bool const fullscreen = state.IsWindowFullscreen(active.id());
  auto const& display = state.DisplayMenu();
  val::global("AtCatalog").call<void>("setPages", ids, active.id().id(), fullscreen,
      display.fullscreen_items[fullscreen ? 1 : 0].label->Resolve(language),
      layout_items, active.id() == catalog.persisted.main_window.id());
}

}  // namespace apptraverse
