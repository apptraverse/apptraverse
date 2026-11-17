#include <filesystem>
#include <memory>
#include <string_view>
#include <emscripten.h>
#include <emscripten/val.h>
#include "apptraverse/application_model_session.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/platform/web/web_application_host.h"
#include "apptraverse/platform/web/web_ui_window_presenter.h"
#include "apptraverse/platform/web/web_ui_push_button_presenter.h"
#include "apptraverse/platform/web/web_ui_dom_em.h"
#include "apptraverse/single_file_user_storage.h"
#include "surface_ui_session.h"
#include "surface_ui_native_presenter.h"
#if !defined(APPTRAVERSE_SURFACE_UI_EXPORTER)
#include "embedded_surface_ui_supply.h"
#endif
namespace demo = apptraverse::example::surface_ui;
namespace {
class SurfaceNativeWindow : public demo::SurfacePresenterMixin<apptraverse::ui::web::WebDesktopWindowPresenter> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::surface_ui::NativeWindow", SurfaceNativeWindow, apptraverse::ui::web::WebDesktopWindowPresenter, 0)
 protected: SurfaceNativeWindow() = default;
 public:
  explicit SurfaceNativeWindow(ae::ObjProp p) : SurfacePresenterMixin{p} {}
  template <class D> void Load(ae::Version<0>, D& d) { d(base_); }
  template <class D> void Save(ae::Version<0>, D& d) const { d(base_); }

};
APPTRAVERSE_REGISTER(SurfaceNativeWindow);
demo::SurfaceFactories Factories() {
  return {[](ae::Domain& d) { return SurfaceNativeWindow::ptr::Create(ae::CreateWith{d}); },
    [](ae::Domain& d) { return apptraverse::ui::web::WebPushButtonPresenter::ptr::Create(ae::CreateWith{d}); }};
}
#if !defined(APPTRAVERSE_SURFACE_UI_EXPORTER)
apptraverse::ApplicationModelRuntime runtime;
apptraverse::ui::web::WebPresentationHost presentation;
std::unique_ptr<apptraverse::web::WebApplicationHost> host;
ae::RamDomainStorage user, local;
std::filesystem::path state_dir{"/persistent/surface_generic_s0_state"};
void FinishShutdown(void* succeeded) {
  if (presentation.mount != nullptr) {
    apptraverse::ui::web::ReleaseOwnedEmvalHandle(presentation.mount);
    presentation.mount = nullptr;
  }
  host.reset();
  EM_ASM({ SurfaceUi.stopped(!!$0); }, succeeded != nullptr);
}
void Render(ae::Obj& root) {
  auto& app = static_cast<demo::SurfaceApplication&>(root);
  std::string ids;
  for (auto const& w : app.windows) {
    if (!ids.empty()) ids += ',';
    ids += std::to_string(w.id().id());
    auto& presenter = static_cast<apptraverse::ui::web::WebDesktopWindowPresenter&>(*w->presenter);
    EM_ASM({
      var client = Emval.toValue(Number($0));
      client.parentElement.dataset.surfaceId = String($1 >>> 0);
    }, presenter.ClientMountHandle(), w.id().id());
  }
  EM_ASM({ SurfaceUi.render(UTF8ToString(Number($0)), $1 >>> 0); }, ids.c_str(), app.current.id());
}
#endif
}
#if !defined(APPTRAVERSE_SURFACE_UI_EXPORTER)
extern "C" {
EMSCRIPTEN_KEEPALIVE void ApptraverseSurfaceUiSelect(unsigned id) {
  if (host && host->ui_session()) host->model_proxy().Invoke(demo::ApplicationId(), &demo::SurfaceApplication::Select, ae::ObjId{id});
}
EMSCRIPTEN_KEEPALIVE int ApptraverseSurfaceUiHidden() {
  return !host || host->PersistForPageLifecycle((state_dir / "user.bin").generic_string());
}
EMSCRIPTEN_KEEPALIVE void ApptraverseSurfaceUiShutdown() {
  if (host && ApptraverseSurfaceUiHidden()) host->RequestShutdown();
}
}
#endif
int main(int argc, char** argv) {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::web::EnsureWebUiPresenterRegistration();
  demo::EnsureSurfaceUiRegistration();
#if defined(APPTRAVERSE_SURFACE_UI_EXPORTER)
  if (argc != 2) return 2;
  return demo::ExportSurfaceSupply(argv[1], Factories(), false) ? 0 : 1;
#else
#if defined(NDEBUG)
  bool load = true;
#else
  bool load = false;
#endif
  for (int i = 1; i < argc; ++i) {
    auto argument = std::string_view{argv[i]};
    if (argument == "--state-dir" && i + 1 < argc) state_dir = argv[++i];
    else if (argument == "--load-only") load = true;
    else return 2;
  }
#if !defined(NDEBUG)
  if (!load) state_dir /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id()) + "/user";
#endif
  auto environment = apptraverse::ui::web::ReadWebDeviceEnvironment();
  apptraverse::web::WebApplicationHostCallbacks callbacks;
  callbacks.refresh_presentation_host = [](auto& native) {
    native.mount = emscripten::val::global("document").call<emscripten::val>("getElementById", std::string{"surface-mount"}).release_ownership();
  };
  callbacks.run_model = [environment, load](auto published) {
    if (load && (!apptraverse::LoadUserStorageFile(user, state_dir) || !apptraverse::LoadUserStorageFile(local, state_dir / "local"))) return 1;
    apptraverse::ApplicationModelSessionHooks<demo::SurfaceApplication> hooks;
    hooks.prepare = [environment](demo::SurfaceApplication& app) {
      demo::PrepareSurfaceUi(app, Factories());
      app.resources.device->ObserveEnvironment(environment, apptraverse::ui::UiEnvironmentReason::kStartup);
    };
    hooks.persist = [](demo::SurfaceApplication& app) {
      if (demo::SaveSurfaceUi(app) != 0) return 1;
      return apptraverse::SaveUserStorageFile(user, state_dir) && apptraverse::SaveUserStorageFile(local, state_dir / "local") ? 0 : 1;
    };
    return apptraverse::RunEmbeddedApplicationModelSession(user, local, apptraverse::embedded_distilled::surface_ui::kEmbeddedGraph, runtime, std::move(published), hooks);
  };
  callbacks.request_stop = [] { runtime.RequestStop(); };
  callbacks.capture_model_files = [](ae::Domain& domain, auto& files) {
    auto app = domain.Find(demo::ApplicationId()).as<demo::SurfaceApplication>();
    if (demo::SaveSurfaceUi(*app) != 0) return false;
    files = {{(state_dir / "user.bin").generic_string(), {}}, {(state_dir / "local/user.bin").generic_string(), {}}};
    return apptraverse::SerializeUserStorage(user, files[0].bytes) && apptraverse::SerializeUserStorage(local, files[1].bytes);
  };
  callbacks.after_initial = Render;
  callbacks.after_incremental = Render;
  callbacks.set_ui_input_blocked = [](bool blocked) { EM_ASM({ SurfaceUi.blocked(!!$0); }, blocked); };
  callbacks.on_shutdown_succeeded = [] { emscripten_async_call(FinishShutdown, reinterpret_cast<void*>(1), 0); };
  callbacks.on_shutdown_failed = [] { emscripten_async_call(FinishShutdown, nullptr, 0); };
  host = std::make_unique<apptraverse::web::WebApplicationHost>(runtime, presentation, std::move(callbacks));
  host->StartWithPersistentStorage("/persistent");
  emscripten_exit_with_live_runtime();
  return 0;
#endif
}
