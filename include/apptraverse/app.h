#ifndef APPTRAVERSE_APP_H_
#define APPTRAVERSE_APP_H_

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

namespace apptraverse {

class Window;

class App : public ae::Obj {
  AE_OBJECT(App, Obj, 0)

 protected:
  App() = default;

 public:
  explicit App(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window))

  ae::ObjPtr<Window> window;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_APP_H_
