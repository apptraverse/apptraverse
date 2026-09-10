#ifndef APPTRAVERSE_SURFACES_IOS_APP_H_
#define APPTRAVERSE_SURFACES_IOS_APP_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"

#include "ios_presenters.h"
#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

namespace apptraverse {

// The single mobile host: root UIWindow, paging scroll view, Add / Remove
// current buttons, page order, app lifecycle. Canonical current page is
// Surfaces::mobile_current (Surface identity). Runtime pager index is
// presentation-only. Semantic actions go through the current
// IOSSurfacePresenter (AddClick / RemoveClick / PageShown).
class IOSApp {
 public:
  // Main thread, from didFinishLaunchingWithOptions.
  void Start(std::filesystem::path const& state_dir);
  // Main thread, from applicationWillTerminate: RequestStop → Save → join.
  void Stop();

  void AddCurrentClick();
  void RemoveCurrentClick();
  // Swipe settled on page `index` → PageShown for that Surface.
  void NoteVisiblePage(std::size_t index);
  // Pager geometry changed (viewDidLayoutSubviews).
  void LayoutPages();

 private:
  friend void IOSAttachPage(void* presentation_host, void* page_view);

  void OnInitialPublished();
  void OnIncrementalPublished();
  void RelayoutPages();
  void ScrollToIndex(std::size_t index, bool animated);
  void ApplyDesiredOffset();
  void SetDesiredCurrent(std::uint32_t surface_id);
  std::uint32_t ModelCurrentId() const;
  std::uint32_t EffectiveCurrentId() const;
  std::size_t IndexOfSurfaceId(std::uint32_t surface_id) const;
  // After Initial: if mobile_current is live, open it; else seed surfaces[0].
  void EnsureModelCurrentSeeded();
  // After structural change: keep desired on live Surface; else neighbor.
  void ReconcileDesiredAfterTopologyChange(std::size_t previous_count);
  IOSSurfacePresenter::ptr FindLivePresenter(std::uint32_t surface_id);
  IOSSurfacePresenter::ptr CurrentPresenter();

  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
  void* window_{nullptr};
  void* root_controller_{nullptr};

  // Runtime pager index. Not persisted — Surfaces::mobile_current is.
  std::size_t current_index_{0};
  std::size_t page_count_{0};

  // Presentation intent until mobile_current catches up (rapid swipe races).
  std::uint32_t desired_current_id_{0};
  bool has_desired_current_{false};

  // Suppress PageShown while programmatically restoring scroll position.
  bool applying_model_current_{false};
  bool structural_apply_in_progress_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_IOS_APP_H_
