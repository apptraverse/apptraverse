#include "apptraverse/platform/web/web_ui_dom_em.h"

#include <emscripten/em_js.h>
#include <emscripten/val.h>

namespace apptraverse::ui::web {

EM_JS(void, ApptraverseAppendDomChild, (emscripten::EM_VAL parent,
                                        emscripten::EM_VAL child), {
  Emval.toValue(parent).appendChild(Emval.toValue(child));
});

EM_JS(void, ApptraverseRemoveDomNode, (emscripten::EM_VAL node), {
  var el = Emval.toValue(node);
  if (el && el.remove) {
    el.remove();
  }
});

void ReleaseOwnedEmvalHandle(emscripten::EM_VAL handle) {
  if (handle == nullptr) {
    return;
  }
  emscripten::val owned = emscripten::val::take_ownership(handle);
  (void)owned;
}

void AppendDomChild(emscripten::EM_VAL parent, emscripten::EM_VAL child) {
  ApptraverseAppendDomChild(parent, child);
}

void RemoveDomNode(emscripten::EM_VAL node) {
  if (node == nullptr) {
    return;
  }
  ApptraverseRemoveDomNode(node);
}

}  // namespace apptraverse::ui::web
