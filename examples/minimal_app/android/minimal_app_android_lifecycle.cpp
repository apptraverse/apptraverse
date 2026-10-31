#include "minimal_app_android_lifecycle.h"

#include "minimal_application.h"

#include <vector>

#include "android_log.h"

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/platform/android/android_ui_registration.h"
#include "apptraverse/ui/ui_presenters.h"

#include "minimal_app_android_graph.h"
#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"

namespace apptraverse::example::minimal {

int RunMinimalApplicationModel(
    std::filesystem::path const& state_dir,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published,
    bool allow_distilled_bootstrap) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::android::EnsureAndroidUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  if (allow_distilled_bootstrap) {
    bool const bootstrapped = BootstrapMinimalApplicationProfileIfAbsent(
        state_dir, [](ae::Domain& domain) {
          return BuildAndroidMinimalApplicationGraph(domain);
        });
    if (bootstrapped) {
      apptraverse::android::LogInfo("MINIMAL_APP_PROFILE_BOOTSTRAP");
    } else {
      apptraverse::android::LogInfo("MINIMAL_APP_LOADED_FROM_PROFILE");
    }
  }
#else
  (void)allow_distilled_bootstrap;
#endif

  DirectoryDomainStorage durable_storage{state_dir};
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [](MinimalApplication& application) {
    PrepareMinimalApplicationForRun(application);
  };
  hooks.persist = [&durable_storage](MinimalApplication& application) -> int {
    return PersistMinimalApplication(application, durable_storage);
  };

  return RunApplicationModelSession(
      durable_storage, MinimalApplicationRootId(), runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse::example::minimal
