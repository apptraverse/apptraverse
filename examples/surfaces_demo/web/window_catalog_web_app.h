#ifndef APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_
#define APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "apptraverse/application_model_runtime.h"
#include "apptraverse/application_ui_session.h"
#include "apptraverse/model_object_proxy.h"

namespace apptraverse {

class WebWindowCatalogApp {
 public:
  static WebWindowCatalogApp& Instance();

  void Start(std::filesystem::path state_dir);
  void OnPublicationFromModel(int kind_flag);

 private:
  WebWindowCatalogApp() = default;

  std::filesystem::path state_dir_;
  ApplicationModelRuntime runtime_;
  std::optional<ModelObjectProxy> model_proxy_;
  std::unique_ptr<ApplicationUiSession> ui_session_;
  std::thread model_thread_;

  std::mutex publication_mu_;
  std::vector<std::uint8_t> pending_publication_bytes_;
  int pending_publication_kind_{-1};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WEB_APP_H_
