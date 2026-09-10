#ifndef APPTRAVERSE_SURFACES_IOS_APP_H_
#define APPTRAVERSE_SURFACES_IOS_APP_H_

#include <cstddef>
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
// current buttons, page order, current page, app lifecycle. Semantic Surface
// actions are not issued here — the host picks the current
// IOSSurfacePresenter and calls AddClick / RemoveClick on it.
class IOSApp {
 public:
  // Main thread, from didFinishLaunchingWithOptions.
  void Start(std::filesystem::path const& state_dir);
  // Main thread, from applicationWillTerminate.
  void Stop();

  void AddCurrentClick();
  void RemoveCurrentClick();
  // Swipe finished on page `index`. Presentation-only; no model Event.
  void NoteVisiblePage(std::size_t index);
  // Pager geometry changed (viewDidLayoutSubviews).
  void LayoutPages();

 private:
  friend void IOSAttachPage(void* presentation_host, void* page_view);

  void OnInitialPublished();
  void OnIncrementalPublished();
  // Places every page at its index in Surfaces::surfaces and re-derives the
  // current page. Runs after publications and after pager geometry changes.
  void RelayoutPages();
  IOSSurfacePresenter::ptr CurrentPresenter();

  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
  void* window_{nullptr};
  void* root_controller_{nullptr};
  // Runtime-only pager state. Never reflected, never persisted, not model
  // state: the model has no current_surface.
  std::size_t current_index_{0};
  std::size_t page_count_{0};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_IOS_APP_H_
