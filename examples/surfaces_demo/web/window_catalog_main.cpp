#include <emscripten.h>

#include "window_catalog_web_app.h"

int main() {
  apptraverse::WebWindowCatalogApp::Instance().Start(
      "/persistent/window_catalog_runtime_state_v5");
  emscripten_exit_with_live_runtime();
  return 0;
}
