#include "minimal_app_lifecycle_macos.h"

#include <filesystem>
#include <iostream>

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_application_model_session.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/macos/macos_ui_presenters.h"
#include "apptraverse/ui/ui_presenters.h"
#include "embedded_distilled_minimal_app_macos_supply.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "minimal_application.h"

namespace apptraverse::example::minimal {

int RunMacMinimalApplicationModel(
    std::filesystem::path const& state_dir, ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::macos::EnsureMacUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

  bool const user_profile_present =
      std::filesystem::exists(state_dir) &&
      !std::filesystem::is_empty(state_dir);
  std::cerr << (user_profile_present
                    ? "MINIMAL_APP_MACOS_LOADED_FROM_PROFILE\n"
                    : "MINIMAL_APP_MACOS_LOADED_FROM_EMBEDDED_BACKING\n");

  DirectoryDomainStorage durable_storage{state_dir};
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [user_profile_present](MinimalApplication& application) {
    PrepareMinimalApplicationForRun(application);
    if (user_profile_present) {
      NormalizeMinimalApplicationAfterEmbeddedUserLoad(application);
    }
  };
  hooks.persist = [&durable_storage](MinimalApplication& application) {
    return PersistMinimalApplicationEmbeddedUserOverlay(application,
                                                        durable_storage);
  };
  return RunEmbeddedApplicationModelSession(
      durable_storage, embedded_distilled::minimal::kEmbeddedGraph, runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse::example::minimal
