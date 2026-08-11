#ifndef APPTRAVERSE_APP_H_
#define APPTRAVERSE_APP_H_

#include "aether/obj/obj.h"
#include "aether/obj/obj_ptr.h"

#include "model/client.h"
#include "apptraverse/object_macros.h"

namespace apptraverse {

class Window;

class App : public ae::Obj {
  APPTRAVERSE_OBJECT(App, ae::Obj, 1)

 protected:
  App() = default;

 public:
  explicit App(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(window), AE_MMBR(local_client))

  ae::ObjPtr<Window> window;
  Client::ptr local_client;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_APP_H_
