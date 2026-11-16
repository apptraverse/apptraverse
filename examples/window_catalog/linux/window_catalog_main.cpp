#include <filesystem>
#include <string_view>

#include "window_catalog_graph.h"
#include "window_catalog_linux_application.h"
#include "apptraverse/noninteractive_crt.h"
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
#include "window_catalog_distillation.h"
#endif

int main(int argc, char** argv) {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::EnsureWindowCatalogGraphRegistration();
  std::filesystem::path state_dir{"window_catalog_linux_state"};
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
#if defined(APPTRAVERSE_WINDOW_CATALOG_DEBUG_BOOTSTRAP)
    // Supply preparation uses this Debug application without starting GTK.
    if (arg == "--distill-only" && i + 1 < argc) {
      apptraverse::DistillWindowCatalog(std::filesystem::absolute(argv[++i]));
      return 0;
    }
#endif
    if (arg == "--state-dir" && i + 1 < argc) {
      state_dir = argv[++i];
    }
  }
  apptraverse::WindowCatalogLinuxApplication application{state_dir};
  return application.Run(application.MakeHostCallbacks());
}
