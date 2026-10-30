#include "apptraverse/platform/web/web_ui_dom_em.h"

#include <emscripten/em_js.h>

namespace apptraverse::ui::web {

EM_JS(void, ApptraverseReleaseOwnedEmvalHandle, (emscripten::EM_VAL handle), {
  if (handle) {
    Emval.decref(handle);
  }
});

EM_JS(void, ApptraverseAppendDomChild, (emscripten::EM_VAL parent,
                                          emscripten::EM_VAL child), {
  Emval.toValue(parent).appendChild(Emval.toValue(child));
});

void ReleaseOwnedEmvalHandle(emscripten::EM_VAL handle) {
  ApptraverseReleaseOwnedEmvalHandle(handle);
}

void AppendDomChild(emscripten::EM_VAL parent, emscripten::EM_VAL child) {
  ApptraverseAppendDomChild(parent, child);
}

}  // namespace apptraverse::ui::web
