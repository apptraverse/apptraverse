#ifndef APPTRAVERSE_WINDOW_CATALOG_WIN_APP_H_
#define APPTRAVERSE_WINDOW_CATALOG_WIN_APP_H_

#include <filesystem>

#include "window_catalog_lifecycle.h"
#include "window_catalog_model.h"

namespace apptraverse {

class WindowCatalogWinApp {
 public:
  int Run(std::filesystem::path const& state_dir);

 private:
  WindowCatalogModelSession session_;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_CATALOG_WIN_APP_H_
