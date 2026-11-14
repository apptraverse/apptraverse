#import "minimal_app_macos_composition.h"

#include <filesystem>
#include <cstdlib>
#include <utility>

#include "apptraverse/platform/macos/macos_application_bridge.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/noninteractive_crt.h"
#include "minimal_app_desktop_close.h"
#include "minimal_app_lifecycle_macos.h"
#include "minimal_application.h"

ATMacApplicationBridge* ATCreateMinimalApplicationBridge(
    NSString* userDirectory,
    id<ATMacApplicationBridgeDelegate> delegate) {
  char const* native_path = userDirectory.fileSystemRepresentation;
  if (!native_path || native_path[0] == '\0') {
    apptraverse::WriteFatalStderr("cannot resolve MinimalApp USER directory\n");
    std::abort();
  }
  std::filesystem::path state_dir{native_path};
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
  state_dir /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
#endif

  apptraverse::ui::macos::MacApplicationBridgeCallbacks callbacks;
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
