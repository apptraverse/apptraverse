#include "minimal_app_lifecycle.h"

#include "minimal_application.h"

#include <fstream>
#include <vector>

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include "minimal_app_graph.h"
#endif

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/noninteractive_crt.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/platform/windows/win_ui_presenters.h"
#include "apptraverse/ui/ui_presented_window_lifecycle.h"
#include "apptraverse/ui/ui_presenters.h"

namespace apptraverse::example::minimal {

void RunMinimalApplicationModel(
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

  {
    DirectoryDomainStorage durable_storage{state_dir};
    ae::Domain domain{durable_storage};
    auto application =
        LoadApplication<MinimalApplication>(domain, MinimalApplicationRootId());

    std::filesystem::path const checkpoint_path =
        state_dir / "persistent_model_checkpoint.bin";
    std::error_code checkpoint_error;
    bool const has_checkpoint =
        std::filesystem::exists(checkpoint_path, checkpoint_error);
    if (has_checkpoint) {
      std::ifstream checkpoint_in(checkpoint_path,
                                  std::ios::binary | std::ios::ate);
      auto const checkpoint_size =
          static_cast<std::size_t>(checkpoint_in.tellg());
      checkpoint_in.seekg(0, std::ios::beg);
      std::vector<std::uint8_t> checkpoint_bytes(checkpoint_size);
      if (checkpoint_size > 0) {
        checkpoint_in.read(reinterpret_cast<char*>(checkpoint_bytes.data()),
                           static_cast<std::streamsize>(checkpoint_size));
      }
      ByteSource checkpoint;
      checkpoint.data = checkpoint_bytes.data();
      checkpoint.size = checkpoint_bytes.size();
      checkpoint.pos = 0;
      LoadPersistentModelSnapshot(checkpoint, domain, durable_storage,
                                  *application);
    }

    if (application->main_window.is_valid()) {
      if (!application->main_window.is_loaded()) {
        application->main_window.Load();
      }
      ui::LoadPresentedWindow(application->main_window);
    }

    runtime.Run(domain, *application, on_published);

    StabilizeDistilledPersistentSnapshot(*application);
    ui::SavePresentedWindow(application->main_window);
    MinimalApplication::ptr::MakeFromThis(&*application).Save();
    SavePersistentModelSnapshotToStorage(*application, durable_storage);
  }
}

}  // namespace apptraverse::example::minimal
