#include <filesystem>
#include <string_view>
#include <utility>
#include <emscripten.h>
#include "window_catalog_web_app.h"
#include "apptraverse/noninteractive_crt.h"

int main(int argc, char** argv) {
  std::filesystem::path profile{"/persistent/window_catalog_web_state"};
  for (int i = 1; i < argc; ++i) {
    if (std::string_view{argv[i]} == "--state-dir" && i + 1 < argc) {
      profile = argv[++i];
    } else {
      apptraverse::WriteFatalStderr("fatal: unknown Web WindowCatalog argument\n");
      return 1;
    }
  }
  apptraverse::WebWindowCatalogApp::Instance().Start(std::move(profile));
  emscripten_exit_with_live_runtime();
  return 0;
}
