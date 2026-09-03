#ifndef APPTRAVERSE_PRESENTER_H_
#define APPTRAVERSE_PRESENTER_H_

#include "aether-objects/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

// Platform-neutral presenter base. Native handles are created in OnLoad after
// the presentation graph is loaded and pointers to UI Domain copies resolve.
class Presenter : public ae::Obj {
  APPTRAVERSE_OBJECT(Presenter, ae::Obj, 0)

 protected:
  Presenter() = default;

 public:
  explicit Presenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  virtual void OnLoad() {}
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PRESENTER_H_
