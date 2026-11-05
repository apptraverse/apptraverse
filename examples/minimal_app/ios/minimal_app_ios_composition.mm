#import "minimal_app_ios_composition.h"

#include <filesystem>
#include <utility>

#include "apptraverse/platform/ios/ios_application_bridge.h"
#include "apptraverse/directory_domain_storage.h"
#include "minimal_app_desktop_close.h"
#include "minimal_app_lifecycle_ios.h"
#include "minimal_app_persistence.h"
#include "minimal_application.h"

ATIosApplicationBridge* ATCreateMinimalApplicationBridge(
    NSString* userDirectory,
    id<ATIosApplicationBridgeDelegate> delegate) {
  std::filesystem::path const state_dir{
      userDirectory.fileSystemRepresentation};

  apptraverse::ui::ios::IosApplicationBridgeCallbacks callbacks;
  callbacks.run_model =
      [state_dir](apptraverse::ApplicationModelRuntime& runtime,
                  apptraverse::ApplicationPublishedCallback on_published) {
        return apptraverse::example::minimal::RunIosMinimalApplicationModel(
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
  callbacks.persist_model =
      [state_dir](ae::Domain& domain) {
        apptraverse::DirectoryDomainStorage storage{state_dir};
        return apptraverse::example::minimal::
            PersistMinimalApplicationEmbeddedUserOverlayFromDomain(domain,
                                                                   storage);
      };
  return apptraverse::ui::ios::MakeIosApplicationBridge(
      std::move(callbacks), delegate);
}
