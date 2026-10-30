#ifndef APPTRAVERSE_PLATFORM_WEB_WEB_UI_DOM_EM_H_
#define APPTRAVERSE_PLATFORM_WEB_WEB_UI_DOM_EM_H_

#include <emscripten/val.h>

namespace apptraverse::ui::web {

// Releases an owning EM_VAL from Emval.toHandle / take_ownership.
void ReleaseOwnedEmvalHandle(emscripten::EM_VAL handle);

void AppendDomChild(emscripten::EM_VAL parent, emscripten::EM_VAL child);

void RemoveDomNode(emscripten::EM_VAL node);

}  // namespace apptraverse::ui::web

#endif  // APPTRAVERSE_PLATFORM_WEB_WEB_UI_DOM_EM_H_
