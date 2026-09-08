#ifndef APPTRAVERSE_PRESENTER_H_
#define APPTRAVERSE_PRESENTER_H_

#include "aether-objects/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

// Platform-neutral presenter base. Not a Node: no journal. Local presenter
// state is not model-journaled; GUI may mutate it directly later.
//
// OnLoad is a GUI presentation-initialization hook, not an object-system
// deserialization callback. Ordinary Load/Create/Save must not create native
// resources. Call OnLoad only from InitializePresenters after the GUI mirror
// graph is fully loaded and references are resolved.
class Presenter : public ae::Obj {
  APPTRAVERSE_OBJECT(Presenter, ae::Obj, 0)

 protected:
  Presenter() = default;

 public:
  explicit Presenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  virtual void OnLoad() {}

  // Runtime-only. Not serialized. Win32 uses this as CreateWindow lpParam.
  void* presentation_host{nullptr};
  bool presentation_initialized{false};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PRESENTER_H_
