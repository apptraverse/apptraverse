#ifndef APPTRAVERSE_WINDOW_H_
#define APPTRAVERSE_WINDOW_H_

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

class WindowPresenter;
class Chat;

class Window : public ae::Obj {
  APPTRAVERSE_OBJECT(Window, ae::Obj, 0)

 protected:
  Window() = default;

 public:
  explicit Window(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(presenter), AE_MMBR(chat))

  ae::ObjPtr<WindowPresenter> presenter;
  ae::ObjPtr<Chat> chat;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_WINDOW_H_
