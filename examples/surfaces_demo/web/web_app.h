#ifndef APPTRAVERSE_SURFACES_WEB_APP_H_
#define APPTRAVERSE_SURFACES_WEB_APP_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"
#include "aether-objects/obj/obj_id.h"

#include "apptraverse/model_object_proxy.h"

#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

namespace apptraverse {

// Browser host for surfaces_demo. Same responsibilities as WinApp / Android
// NativeRuntime: model session, GUI Domain, publication consumption. Current
// page is Surfaces::mobile_current (same model as Android). DOM stays on the
// browser main thread; the model runs on one pthread.
class WebApp {
 public:
  static WebApp& Instance();

  // Called once from main after IDBFS has been synced into the virtual FS.
  void Start(std::filesystem::path state_dir);

  // Browser main thread: Add / Remove on Surfaces::mobile_current.
  void AddCurrent();
  void RemoveCurrent();
  // Tab click → SurfacePresenter::PageShown → SetCurrentSurfaceEvent.
  void SelectSurface(std::uint32_t surface_id);
  // Dev / last-page path: RequestStop → Save → syncfs(false).
  void RequestStop();

  // Called from WebSurfacePresenter::OnLoad / OnUnload on the GUI thread.
  void OnSurfacePageLoaded(std::uint32_t surface_id, std::uint32_t number,
                           bool newly_created);
  void OnSurfacePageUnloaded(std::uint32_t surface_id);

  // Model pthread → main-thread dispatch entry.
  void OnPublicationFromModel(int kind);
  // After model Run returned and Save finished: join + IDBFS syncfs(false).
  void FinishStopAndSync();
  // Main thread: push MEMFS → IndexedDB (after model Save completed).
  void SyncIndexedDbFromFs();
  void OnIndexedDbSyncDone(int err);
  void RequestSyncFromJs();

 private:
  WebApp() = default;

  void ConsumePublication(SurfacesPublicationKind kind);
  // Web-host only: Post Application::Save on the model thread, then
  // syncfs(false) on the main thread. SurfacesModelSession is unchanged.
  void QueueIndexedDbPersist();
  void SyncTabOrder();
  void ShowCurrentPage();
  // Presentation intent for Add/Remove/highlight before model publication ACK.
  // Canonical persistence remains Surfaces::mobile_current.
  void SetDesiredCurrent(std::uint32_t surface_id);
  std::uint32_t EffectiveCurrentId() const;
  // After Initial, if model has no mobile_current yet, settle on surfaces[0]
  // the same way the Android pager reports its first page.
  void EnsureModelCurrentSeeded();
  SurfacePresenter::ptr FindLivePresenter(std::uint32_t surface_id);
  // ObjId of Surfaces::mobile_current, or 0 when empty.
  std::uint32_t ModelCurrentId() const;
  bool SurfaceIsLive(std::uint32_t surface_id) const;

  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;

  // Runtime-only. Not reflected. Prefer over model for Remove/Add/highlight
  // until mobile_current catches up (rapid Select races).
  std::uint32_t desired_current_id_{0};
  bool has_desired_current_{false};

  // True only while ApplySurfacesStructural runs so OnLoad can PageShown a
  // newly added Surface (pager settle on the new page).
  bool structural_apply_in_progress_{false};
  bool initializing_presentation_{false};
  bool stopping_{false};
  bool stopped_{false};
  std::size_t pending_remove_index_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WEB_APP_H_
