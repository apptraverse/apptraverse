#import "minimal_app_ios_composition.h"

#include <filesystem>
#include <cstdlib>
#include <memory>
#include <utility>

#include "apptraverse/platform/ios/ios_application_bridge.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/platform/ios/ios_ui_presenters.h"
#include "apptraverse/object_macros.h"
#include "minimal_app_desktop_close.h"
#include "minimal_app_lifecycle_ios.h"
#include "minimal_application.h"

ATIosApplicationBridge* ATCreateMinimalApplicationBridge(
    NSString* userDirectory,
    id<ATIosApplicationBridgeDelegate> delegate) {
  char const* native_path = userDirectory.fileSystemRepresentation;
  if (!native_path || native_path[0] == '\0') {
    apptraverse::WriteFatalStderr("cannot resolve MinimalApp USER directory\n");
    std::abort();
  }
  std::filesystem::path state_dir{native_path};
#if defined(APPTRAVERSE_MINIMAL_APPLE_DEBUG_FRESH)
  // A fresh Debug graph must never replace a previous run's USER.
  state_dir /= "debug-run-" + std::to_string(ae::ObjId::GenerateUnique().id());
#endif
  struct Profile {
    ae::RamDomainStorage user, local;
  };
  auto profile = std::make_shared<Profile>();

  apptraverse::ui::ios::IosApplicationBridgeCallbacks callbacks;
  callbacks.run_model =
      [state_dir, profile](apptraverse::ApplicationModelRuntime& runtime,
                  apptraverse::ApplicationPublishedCallback on_published,
                  apptraverse::ui::UiDeviceEnvironment const& environment) {
        return apptraverse::example::minimal::RunIosMinimalApplicationModel(
            state_dir, profile->user, profile->local, runtime,
            std::move(on_published), environment);
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
  callbacks.persist_model =
      [state_dir, profile](ae::Domain& domain) {
        auto root = domain.Find(
            apptraverse::example::minimal::MinimalApplicationRootId());
        auto& application = *root.as<apptraverse::example::minimal::MinimalApplication>();
        return apptraverse::example::minimal::PersistIosMinimalApplication(
            application, profile->user, profile->local, state_dir);
      };
  callbacks.mobile_presentation = [](ae::Obj& root) {
    auto& application = static_cast<apptraverse::example::minimal::MinimalApplication&>(root);
    // MinimalApp's model has one main window and an explicitly opened detail.
    // The existing Close event returns to the main page; no second navigation
    // state is invented by the platform presenter.
    auto active = application.ActivePageId();
    return apptraverse::ui::ios::IosMobilePresentation{{active}, active};
  };
  return apptraverse::ui::ios::MakeIosApplicationBridge(
      std::move(callbacks), delegate);
}

void ATRegisterMinimalIosApplication(void) {
  apptraverse::EnsureCoreObjectRegistration();
  apptraverse::ui::ios::EnsureIosUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();
}
