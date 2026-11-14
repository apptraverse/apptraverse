#import "minimal_app_macos_composition.h"

#include <filesystem>
#include <utility>

#include "apptraverse/platform/macos/macos_application_bridge.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/object_macros.h"
#include "minimal_app_desktop_close.h"
#include "minimal_app_lifecycle_macos.h"
#include "minimal_application.h"
#include "apptraverse/ui/ui_window_presenter.h"

ATMacApplicationBridge* ATCreateMinimalApplicationBridge(
    NSString* userDirectory,
    id<ATMacApplicationBridgeDelegate> delegate) {
  std::filesystem::path const state_dir{
      userDirectory.fileSystemRepresentation};

  apptraverse::ui::macos::MacApplicationBridgeCallbacks callbacks;
  callbacks.after_initial = [](ae::Obj& root) {
    auto& application = static_cast<apptraverse::example::minimal::MinimalApplication&>(root);
    application.main_window->presenter->model_proxy->Invoke(root.obj_id,
        &apptraverse::example::minimal::MinimalApplication::SubmitDesktopEnvironment,
        static_cast<std::int32_t>(160));
  };
  callbacks.run_model =
      [state_dir](apptraverse::ApplicationModelRuntime& runtime,
                  apptraverse::ApplicationPublishedCallback on_published) {
        return apptraverse::example::minimal::RunMacMinimalApplicationModel(
            state_dir, runtime, std::move(on_published));
      };
  callbacks.close_desktop_window =
      [](ae::Obj& root, ae::Obj& closed_window,
         apptraverse::ModelObjectProxy& proxy,
         std::function<void()> request_stop) {
        bool const closes_application =
            closed_window.obj_id ==
            apptraverse::example::minimal::MinimalMainWindowId();
        apptraverse::example::minimal::HandleMinimalAppDesktopWindowClose(
            root, closed_window, proxy, std::move(request_stop));
        return closes_application;
      };
  return apptraverse::ui::macos::MakeMacApplicationBridge(
      std::move(callbacks), delegate);
}

void ATRegisterMinimalMacApplication(void) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::macos::EnsureMacUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();
}
