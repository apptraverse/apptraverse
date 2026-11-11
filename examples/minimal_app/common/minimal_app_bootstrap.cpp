#include "minimal_app_persistence.h"
#include "minimal_app_prepare.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"

namespace apptraverse::example::minimal {
void NormalizeMinimalApplicationAfterEmbeddedUserLoad(
    MinimalApplication& application) {
#if defined(APPTRAVERSE_ENABLE_DISTILLATION)
  // Embedded USER save finalizes journals (clears base). Reload leaves nodes
  // materialized but without base until runtime nodes are initialized.
  FinalizeDistilledGraph(application);
#else
  (void)application;
#endif
}

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
  PrepareMinimalApplicationForRun(*bootstrap_app);
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

}  // namespace apptraverse::example::minimal
