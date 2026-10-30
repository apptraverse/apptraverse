#include <emscripten.h>

#include "apptraverse/noninteractive_crt.h"

#include "minimal_app_web_app.h"

int main() {
  apptraverse::EnableNoninteractiveCrt();
  apptraverse::example::minimal::StartWebMinimalApplication();
  emscripten_exit_with_live_runtime();
}
