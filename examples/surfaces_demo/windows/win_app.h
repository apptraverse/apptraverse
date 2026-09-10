#ifndef APPTRAVERSE_SURFACES_WIN_APP_H_
#define APPTRAVERSE_SURFACES_WIN_APP_H_

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef RegisterClass
#  undef RegisterClass
#endif

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

class WinApp {
 public:
  int Run(std::filesystem::path const& state_dir);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void OnInitialPublished();
  void OnIncrementalPublished();
  void QueueAllWindowBounds();
  // Bring Surfaces::mobile_current (or last Surface) to the top of the Z-order.
  void RestoreActiveSurfaceZOrder();
  // Enqueue PageShown for the foreground Surface before geometry snapshot.
  void QueueForegroundAsCurrent();
  void RequestApplicationStop();

  SurfacesModelSession session_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::thread model_thread_;
  HWND notify_{nullptr};
  HWND loading_{nullptr};
  ae::RamDomainStorage ui_storage_;
  std::unique_ptr<ae::Domain> ui_domain_;
  Application::ptr ui_application_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_SURFACES_WIN_APP_H_
