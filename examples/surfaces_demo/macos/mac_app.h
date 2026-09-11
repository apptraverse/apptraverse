#ifndef APPTRAVERSE_SURFACES_MAC_APP_H_
#define APPTRAVERSE_SURFACES_MAC_APP_H_

#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "apptraverse/model_object_proxy.h"

#include "surfaces_lifecycle.h"
#include "surfaces_model.h"

namespace apptraverse {

class MacApp {
 public:
  int Run(std::filesystem::path const& state_dir);

  // Single graceful stop: persist current + geometry, then RequestStop.
  void RequestApplicationStop();

 private:
  friend void MacRequestApplicationStop(void* presentation_host);

  enum class MainOp {
    InitialPublished,
    IncrementalPublished,
    ModelFinished,
  };

  struct MainOpCtx {
    MacApp* app;
    MainOp op;
  };

  static void MainOpTrampoline(void* raw);
  void OnInitialPublished();
  void OnIncrementalPublished();
  void OnModelFinished();
  void QueueAllWindowBounds();
  // Enqueue PageShown for the key Surface before geometry snapshot.
  void QueueKeyWindowAsCurrent();
  // Bring Surfaces::mobile_current (or last Surface) to the front of the
  // window stack.
  void RestoreActiveSurfaceZOrder();
  void PostMain(void (*fn)(void*), void* ctx);

  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
  void* loading_window_{nullptr};
  void* app_delegate_{nullptr};
  bool stop_requested_{false};
  bool model_finished_{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_MAC_APP_H_
