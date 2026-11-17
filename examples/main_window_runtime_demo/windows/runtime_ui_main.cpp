#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <filesystem>
#include <string_view>
#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_persistence.h"
#include "apptraverse/distilled_domain_export.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/platform/windows/win_application_host.h"
#include "apptraverse/platform/windows/win_ui_environment.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/platform/windows/win_ui_push_button_presenter.h"
#include "apptraverse/single_file_user_storage.h"
#include "runtime_ui_model.h"
#if defined(APPTRAVERSE_RUNTIME_UI_DEBUG_BOOTSTRAP)
#  include "runtime_ui_supply.h"
#else
#  include "embedded_distilled_runtime_ui_supply.h"
#endif
namespace demo = apptraverse::example::runtime_ui;
namespace {
demo::RuntimePresenterFactories Factories() {
  demo::RuntimePresenterFactories f;
  f.window = [](ae::Domain& d) {
    return apptraverse::ui::windows::WinDesktopWindowPresenter::ptr::Create(
        ae::CreateWith{d});
  };
  f.button = [](ae::Domain& d) {
    return apptraverse::ui::windows::WinPushButtonPresenter::ptr::Create(
        ae::CreateWith{d});
  };
  return f;
}
constexpr bool Dynamic() {
#if defined(APPTRAVERSE_RUNTIME_UI_DYNAMIC)
  return true;
#else
  return false;
#endif
}
int Save(demo::RuntimeApplication& app) {
  apptraverse::CompactApplicationNodesForUserOverlay(app);
  ae::DomainGraph graph{app.domain};
  return graph.SaveRoot(
                  demo::RuntimeApplication::ptr::MakeFromThis(&app).cached(),
                  app.obj_id)
                 .IsErr()
             ? 1
             : 0;
}
}  // namespace
int main(int argc, char** argv) {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::windows::EnsureWinControlPresenterRegistration();
  demo::EnsureRuntimeUiRegistration();
  std::filesystem::path requested = Dynamic()
                                        ? "dynamic_objects_generic_ui_state"
                                        : "main_window_generic_ui_state";
  for (int i = 1; i < argc; ++i) {
    std::string_view argument{argv[i]};
    if (argument == "--state-dir" && i + 1 < argc) requested = argv[++i];
#if defined(APPTRAVERSE_RUNTIME_UI_DEBUG_BOOTSTRAP)
    if (argument == "--distill-only" && i + 1 < argc) {
      ae::RamDomainStorage backing;
      demo::MakeRuntimeUiSupply(backing, Dynamic(), Factories());
      auto const constants = demo::RuntimeUiSupplyConstants(backing);
      bool const written = apptraverse::WriteEmbeddedDistilledSupplyHeader(
          std::filesystem::path{argv[++i]} /
              "embedded_distilled_runtime_ui_supply.h",
          "runtime_ui", "APPTRAVERSE_EMBEDDED_RUNTIME_UI_SUPPLY_H_",
          demo::RuntimeApplicationId(), {constants.begin(), constants.end()},
          backing, apptraverse::CollectStoredObjectIds(backing));
      return written ? 0 : 1;
    }
#endif
  }
  FreeConsole();
  apptraverse::ui::windows::EnsureWinProcessDpiAwareness();
  apptraverse::ui::windows::WinUiEnvironment environment{nullptr};
  auto const snapshot = environment.snapshot();
  apptraverse::ApplicationModelRuntime runtime;
  int result = 1;
  apptraverse::windows::WinApplicationHostCallbacks callbacks;
  callbacks
      .run_model = [&](apptraverse::ApplicationPublishedCallback published) {
    auto directory = requested;
    ae::RamDomainStorage user, local;
#if defined(APPTRAVERSE_RUNTIME_UI_DEBUG_BOOTSTRAP)
    directory /=
        "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
    directory /= "user";
    ae::RamDomainStorage backing;
    demo::MakeRuntimeUiSupply(backing, Dynamic(), Factories());
#else
    if (!apptraverse::LoadUserStorageFile(user, directory) ||
        !apptraverse::LoadUserStorageFile(local, directory / "local"))
      return;
#endif
    apptraverse::ApplicationModelSessionHooks<demo::RuntimeApplication> hooks;
    hooks.prepare = [&](demo::RuntimeApplication& app) {
      demo::PrepareRuntimeUi(app, Factories());
      app.window->device_state->ObserveEnvironment(
          snapshot, apptraverse::ui::UiEnvironmentReason::kStartup);
    };
    hooks.persist = [&](demo::RuntimeApplication& app) {
      if (Save(app) != 0) return 1;
      return apptraverse::SaveUserStorageFile(user, directory) &&
                     apptraverse::SaveUserStorageFile(local,
                                                      directory / "local")
                 ? 0
                 : 1;
    };
#if defined(APPTRAVERSE_RUNTIME_UI_DEBUG_BOOTSTRAP)
    result = apptraverse::RunClientLocalApplicationModelSession(
        user, local, backing, demo::RuntimeUiSupplyConstants(backing),
        demo::RuntimeApplicationId(), runtime, std::move(published), hooks);
#else
    result = apptraverse::RunEmbeddedApplicationModelSession(
        user, local,
        apptraverse::embedded_distilled::runtime_ui::kEmbeddedGraph, runtime,
        std::move(published), hooks);
#endif
  };
  callbacks.request_stop = [&] { runtime.RequestStop(); };
  callbacks.on_desktop_window_close = [&](ae::Obj&, ae::Obj&,
                                          apptraverse::ModelObjectProxy&) {
    runtime.RequestStop();
  };
  apptraverse::windows::WinApplicationHost host{
      runtime,
      {L"AppTraverseRuntimeUiLoading", L"Loading", L"Loading", 120, 100, 320,
       120},
      std::move(callbacks)};
  host.Run();
  return result;
}
