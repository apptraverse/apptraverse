#include "minimal_app_persistence.h"

#include "minimal_application.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"

namespace apptraverse::example::minimal {

bool BootstrapMinimalApplicationProfileIfAbsent(
    std::filesystem::path const& state_dir,
    MinimalApplicationGraphBuilder const& build_graph) {
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  std::error_code profile_error;
  bool const profile_exists = std::filesystem::exists(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect minimal app profile\n");
    std::abort();
  }
  bool const profile_absent =
      !profile_exists || std::filesystem::is_empty(state_dir, profile_error);
  if (profile_error) {
    WriteFatalStderr("fatal: cannot inspect minimal app profile contents\n");
    std::abort();
  }
  if (!profile_absent) {
    return false;
  }
  DirectoryDomainStorage bootstrap_storage{state_dir};
  ae::Domain bootstrap_domain{bootstrap_storage};
  auto bootstrap_app = build_graph(bootstrap_domain);
  FinalizeDistilledGraph(*bootstrap_app);
  SaveDistilledRoot(*bootstrap_app);
  return true;
#else
  (void)state_dir;
  (void)build_graph;
  WriteFatalStderr("fatal: minimal app bootstrap requires distillation\n");
  std::abort();
#endif
}

int PersistMinimalApplication(MinimalApplication& application,
                              DirectoryDomainStorage& storage) {
  storage.BeginSaveSession();
  ui::SavePresentedWindow(application.main_window);
  MinimalApplication::ptr::MakeFromThis(&application).Save();
  if (storage.SaveSessionHadFailure()) {
    WriteFatalStderr("fatal: MinimalApplication save failed\n");
    return 1;
  }
  return 0;
}

}  // namespace apptraverse::example::minimal
