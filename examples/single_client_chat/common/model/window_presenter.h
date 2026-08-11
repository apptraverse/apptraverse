#ifndef APPTRAVERSE_WINDOW_PRESENTER_H_
#define APPTRAVERSE_WINDOW_PRESENTER_H_

#include "aether/obj/obj.h"

#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "model/window.h"

namespace apptraverse {

class ChatPresenter;

class WindowPresenter : public ae::Obj {
  APPTRAVERSE_OBJECT(WindowPresenter, ae::Obj, 0)

 protected:
  WindowPresenter() = default;

 public:
  explicit WindowPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(chat_presenter))

  LocalPtr<Window> window;
  LocalPtr<ChatPresenter> chat_presenter;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_PRESENTER_H_
