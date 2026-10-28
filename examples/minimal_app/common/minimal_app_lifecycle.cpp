#include "minimal_app_lifecycle.h"

#include "minimal_application.h"

#include <vector>

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include "minimal_app_graph.h"
#endif

#include "apptraverse/application_model_session.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::minimal {

int RunMinimalApplicationModel(
    std::filesystem::path const& state_dir,
    ApplicationModelRuntime& runtime,
    ApplicationPublishedCallback on_published) {
  EnsureObjectRegistration();
  ui::EnsureUiPresenterRegistration();
  ui::windows::EnsureWinUiPresenterRegistration();
  EnsureMinimalApplicationRegistration();

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  std::error_code profile_error;
  bool const profile_exists = std::filesystem::exists(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect minimal app profile\n");
    std::abort();
  }
  bool const profile_absent = !profile_exists ||
      std::filesystem::is_empty(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect minimal app profile contents\n");
    std::abort();
  }
  if (profile_absent) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildMinimalApplicationGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }
#endif

  DirectoryDomainStorage durable_storage{state_dir};
  ApplicationModelSessionHooks<MinimalApplication> hooks;
  hooks.prepare = [](MinimalApplication& application) {
    ui::LoadPresentedWindow(application.main_window);
  };
  hooks.persist = [&durable_storage](MinimalApplication& application) -> int {
    durable_storage.BeginSaveSession();
    ui::SavePresentedWindow(application.main_window);
    MinimalApplication::ptr::MakeFromThis(&application).Save();
    if (durable_storage.SaveSessionHadFailure()) {
      WriteFatalStderr("fatal: MinimalApplication shutdown save failed\n");
      return 1;
    }
    return 0;
  };

  return RunApplicationModelSession(
      durable_storage, MinimalApplicationRootId(), runtime,
      std::move(on_published), hooks);
}

}  // namespace apptraverse::example::minimal
