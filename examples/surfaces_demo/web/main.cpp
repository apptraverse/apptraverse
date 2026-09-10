#include <emscripten.h>

#include "web_app.h"

int main() {
  // IDBFS mount + syncfs(true) completed in Module.preRun before main.
  apptraverse::WebApp::Instance().Start("/persistent/surfaces_runtime_state");
  // Keep the runtime alive for pthread / DOM callbacks. Exit only after the
  // controlled stop path has synced IndexedDB (browser may keep the tab open).
  emscripten_exit_with_live_runtime();
  return 0;
}
