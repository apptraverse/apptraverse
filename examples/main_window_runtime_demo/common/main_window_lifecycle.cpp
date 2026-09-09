#include "main_window_lifecycle.h"

#ifdef APPTRAVERSE_ENABLE_DISTILLATION
#include <filesystem>
#endif

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_serialization.h"

#include "main_window_ids.h"
#include "main_window_model.h"

namespace apptraverse {
namespace {

// Registrars live in this always-linked TU so both distill and load-only
// executables register the graph classes without a second Ensure* wrapper.
APPTRAVERSE_REGISTER(MainWindow);
APPTRAVERSE_REGISTER(MainWindowPresenter);
APPTRAVERSE_REGISTER(Application);

}  // namespace

void ModelSession::RequestStop() {
  {
    std::lock_guard<std::mutex> lock{mu};
    stop = true;
  }
  cv.notify_all();
}

void ModelSession::Run(std::function<void()> on_published) {
#ifdef APPTRAVERSE_ENABLE_DISTILLATION
  // Development bootstrap: create persisted state only when it does not exist.
  bool state_missing = true;
  if (std::filesystem::exists(state_dir)) {
    DirectoryDomainStorage probe{state_dir};
    state_missing =
        probe
            .Enumerate(ae::ObjId{main_window::ToObjId(
                main_window::ObjId::Application)})
            .empty();
  }
  if (state_missing) {
    DirectoryDomainStorage bootstrap_storage{state_dir};
    ae::Domain bootstrap_domain{bootstrap_storage};
    auto bootstrap_app = BuildMainWindowGraph(bootstrap_domain);
    FinalizeDistilledGraph(*bootstrap_app);
    SaveDistilledRoot(*bootstrap_app);
  }
#endif

  {
    DirectoryDomainStorage storage{state_dir};
    ae::Domain domain{storage};
    auto application = LoadApplication<Application>(
        domain,
        ae::ObjId{main_window::ToObjId(main_window::ObjId::Application)});
    LoadStoredAncestorLayersFromRoot(*application, storage);

    auto* buffer = channel.AcquireProducer();
    SerializeInitialPublication(*application, buffer->sink);
    {
      std::lock_guard<std::mutex> lock{mu};
      channel.NotePublished();
      channel.PublishProducer();
    }
    cv.notify_all();
    on_published();

    {
      std::unique_lock<std::mutex> lock{mu};
      cv.wait(lock, [&] { return stop; });
    }
  }
}

}  // namespace apptraverse
