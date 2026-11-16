#ifndef APPTRAVERSE_LINUX_WINDOW_CATALOG_APPLICATION_H_
#define APPTRAVERSE_LINUX_WINDOW_CATALOG_APPLICATION_H_

#include <filesystem>

#include "window_catalog_lifecycle.h"
#include "apptraverse/platform/linux/linux_application_host.h"

namespace apptraverse {

// Application host composition shared by the executable and native acceptance
// runner. The model and publication lifecycle remain in the common template.
class WindowCatalogLinuxApplication {
 public:
  explicit WindowCatalogLinuxApplication(std::filesystem::path state_directory);

  WindowCatalogModelSession& session() { return session_; }
  linux::LinuxApplicationHostCallbacks MakeHostCallbacks();
  int Run(linux::LinuxApplicationHostCallbacks callbacks);

 private:
  WindowCatalogModelSession session_;
  ae::ObjId pending_focus_{};
  int model_exit_status_{1};
};

}  // namespace apptraverse

#endif
