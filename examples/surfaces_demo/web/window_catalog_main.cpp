#include <emscripten.h>

#include "window_catalog_web_app.h"

int main() {
  apptraverse::example::surfaces::StartWebWindowCatalogApplication();
  emscripten_exit_with_live_runtime();
  return 0;
}
