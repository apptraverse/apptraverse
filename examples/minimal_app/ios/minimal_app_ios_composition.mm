#import "minimal_app_ios_composition.h"

#include <filesystem>
#include <memory>
#include <utility>

#include "apptraverse/platform/ios/ios_application_bridge.h"
#include "apptraverse/single_file_user_storage.h"
#include "apptraverse/platform/ios/ios_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"
#include "apptraverse/object_macros.h"
#include "minimal_app_desktop_close.h"
#include "minimal_app_lifecycle_ios.h"
#include "minimal_app_persistence.h"
#include "minimal_application.h"

ATIosApplicationBridge* ATCreateMinimalApplicationBridge(
    NSString* userDirectory,
    id<ATIosApplicationBridgeDelegate> delegate) {
  std::filesystem::path const state_dir{
      userDirectory.fileSystemRepresentation};
  auto user = std::make_shared<ae::RamDomainStorage>();

  apptraverse::ui::ios::IosApplicationBridgeCallbacks callbacks;
  callbacks.run_model =
      [state_dir, user](apptraverse::ApplicationModelRuntime& runtime,
                  apptraverse::ApplicationPublishedCallback on_published) {
        return apptraverse::example::minimal::RunIosMinimalApplicationModel(
            state_dir, *user, runtime, std::move(on_published));
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
      [state_dir, user](ae::Domain& domain) {
        auto root = domain.Find(
            apptraverse::example::minimal::MinimalApplicationRootId());
        auto& application = *root.as<apptraverse::example::minimal::MinimalApplication>();
        int const saved = apptraverse::example::minimal::
            SaveMinimalApplicationEmbeddedUserState(application);
        if (saved != 0) return saved;
        return apptraverse::SaveUserStorageFile(*user, state_dir) ? 0 : 1;
      };
  callbacks.mobile_presentation = [](ae::Obj& root) {
    auto& application = static_cast<apptraverse::example::minimal::MinimalApplication&>(root);
    // MinimalApp's model has one main window and an explicitly opened detail.
    // The existing Close event returns to the main page; no second navigation
    // state is invented by the platform presenter.
    auto active = application.ActivePageId();
    return apptraverse::ui::ios::IosMobilePresentation{{active}, active};
  };
  callbacks.on_viewport = [](ae::Obj& root, apptraverse::ModelObjectProxy& proxy,
                             std::int32_t width, std::int32_t height) {
    proxy.Invoke(root.obj_id,
        &apptraverse::example::minimal::MinimalApplication::SubmitViewport,
        width, height, static_cast<std::int32_t>(160), true);
  };
  return apptraverse::ui::ios::MakeIosApplicationBridge(
      std::move(callbacks), delegate);
}

void ATRegisterMinimalIosApplication(void) {
  apptraverse::EnsureObjectRegistration();
  apptraverse::ui::EnsureUiPresenterRegistration();
  apptraverse::ui::ios::EnsureIosUiPresenterRegistration();
  apptraverse::example::minimal::EnsureMinimalApplicationRegistration();
}
