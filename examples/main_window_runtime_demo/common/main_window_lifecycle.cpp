#include "main_window_lifecycle.h"

#include <filesystem>
#include <mutex>

#include "aether-objects/obj/domain.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/distill.h"
#include "apptraverse/object_serialization.h"

#include "main_window_ids.h"
#include "main_window_model.h"

namespace apptraverse {

void ModelSession::RequestStop() {
  stop.store(true, std::memory_order_release);
  cv.notify_all();
}

void ModelSession::Run() {
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

  DirectoryDomainStorage storage{state_dir};
  ae::Domain domain{storage};
  auto application = LoadApplication<Application>(
      domain, ae::ObjId{main_window::ToObjId(main_window::ObjId::Application)});
  LoadStoredAncestorLayersFromRoot(*application, storage);

  auto* buffer = channel.AcquireProducer();
  SerializeInitialPublication(*application, buffer->sink);
  channel.NotePublished();
  channel.PublishProducer();
#ifdef _WIN32
  auto const hwnd =
      reinterpret_cast<HWND>(notify_hwnd.load(std::memory_order_acquire));
  if (hwnd != nullptr) {
    PostMessageW(hwnd, WM_APPTRAVERSE_PUBLISHED, 0, 0);
  }
#endif
  cv.notify_all();

  {
    std::unique_lock<std::mutex> lock{mu};
    cv.wait(lock, [&] { return stop.load(std::memory_order_acquire); });
  }

#ifdef _WIN32
  if (done_event != nullptr) {
    SetEvent(done_event);
  }
#endif
  cv.notify_all();
}

}  // namespace apptraverse
