#ifndef APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_
#define APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_

#include <filesystem>
#include <memory>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/platform/web/web_application_host.h"
#include "apptraverse/platform/web/web_presentation_host.h"

namespace apptraverse {

// Web WindowCatalog host: model thread + main-thread UI mirror via
// web::WebApplicationHost, mobile pager (same model as Android), embedded
// BACKING plus USER overlay on IDBFS (/persistent) on Save and shutdown.
class WebWindowCatalogApp {
 public:
  static WebWindowCatalogApp& Instance();

  void Start(std::filesystem::path state_dir);

  // Main-thread bridge entry points (see extern "C" exports in the .cpp).
  void RequestSave();
  void RequestShutdown();
  void MobileSwipe(int delta);
  void MobilePageShown(unsigned window_obj_id);
  void ViewportChanged(int width, int height);
  void FinishTeardown(bool shutdown_succeeded);

 private:
  WebWindowCatalogApp() = default;

  void PublishMobilePages(ae::Obj& application_root);

  std::filesystem::path state_dir_;
  ApplicationModelRuntime runtime_;
  ui::web::WebPresentationHost presentation_host_;
  std::unique_ptr<DirectoryDomainStorage> user_storage_;
  std::unique_ptr<web::WebApplicationHost> host_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_
