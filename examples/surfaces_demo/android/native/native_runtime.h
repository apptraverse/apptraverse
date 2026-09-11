#ifndef APPTRAVERSE_SURFACES_ANDROID_NATIVE_RUNTIME_H_
#define APPTRAVERSE_SURFACES_ANDROID_NATIVE_RUNTIME_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"

#include "surfaces_lifecycle.h"
#include "surfaces_model.h"
#include "surfaces_ui_bridge.h"

namespace apptraverse::android {

// Process-wide native core of the Android surfaces demo. Owns the model
// session/thread work and the GUI mirror Domain. Same lifecycle as the Win32
// host: model thread publishes, the main thread consumes and drives presenters.
class NativeRuntime {
 public:
  NativeRuntime(std::filesystem::path state_dir, SurfacesUiBridge ui_bridge);

  NativeRuntime(NativeRuntime const&) = delete;
  NativeRuntime& operator=(NativeRuntime const&) = delete;

  // Model thread. Returns after the session drained pending work and saved.
  void RunModel();
  // Any thread.
  void RequestStop();

  // Main (GUI) thread only.
  void ConsumePublication(SurfacesPublicationKind kind);
  void AddFromSurface(std::uint32_t surface_id);
  void RemoveSurface(std::uint32_t surface_id);
  // The pager settled on this page: report it as the current mobile page.
  void PageShown(std::uint32_t surface_id);
  // Android lifecycle checkpoint: persist model Domain without stopping.
  // Home / recents can kill the process after onStop; Back still Saves in Run.
  void PersistState();
  // Usable host presentation size shared by every live Surface page.
  void ReportPresentationSize(std::int32_t width, std::int32_t height);
  void UnloadUi();

 private:
  void PublishPages();
  // Empty when the page is no longer live: a tap can still carry an id from a
  // page list that a newer publication already replaced.
  SurfacePresenter::ptr FindLivePresenter(std::uint32_t surface_id);

  SurfacesUiBridge ui_bridge_;
  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
};

}  // namespace apptraverse::android

#endif  // APPTRAVERSE_SURFACES_ANDROID_NATIVE_RUNTIME_H_
