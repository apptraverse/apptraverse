#ifndef APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_
#define APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_

#include <filesystem>
#include <memory>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/platform/web/web_application_host.h"
#include "apptraverse/platform/web/web_presentation_host.h"

namespace apptraverse {

// Web WindowCatalog host: model thread + main-thread UI mirror via
// web::WebApplicationHost, mobile pager (same model as Android), embedded
// BACKING plus RAM USER; shutdown writes one user.bin and syncs IDBFS.
class WebWindowCatalogApp {
 public:
  static WebWindowCatalogApp& Instance();

  void Start(std::filesystem::path state_dir);

  // Main-thread bridge entry points (see extern "C" exports in the .cpp).
  void RequestShutdown();
  void MobileSwipe(int delta);
  void MobilePageShown(unsigned window_obj_id);
  void ViewportChanged(int width, int height);
  void ChildViewportChanged(unsigned window_id, int width, int height);
  void FinishTeardown(bool shutdown_succeeded);

 private:
  WebWindowCatalogApp() = default;

  void PublishMobilePages(ae::Obj& application_root);

  std::filesystem::path state_dir_;
  ApplicationModelRuntime runtime_;
  ui::web::WebPresentationHost presentation_host_;
  std::unique_ptr<web::WebApplicationHost> host_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_
