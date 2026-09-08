#ifndef APPTRAVERSE_PRESENTER_H_
#define APPTRAVERSE_PRESENTER_H_

#include "aether-objects/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

// Platform-neutral presenter base. Not a Node: no journal. Local presenter
// state is not model-journaled; GUI may mutate it directly later.
//
// OnLoad / OnUnload are GUI presentation hooks, not object-system
// deserialization callbacks. Ordinary Load/Create/Save must not create or
// destroy native resources. Call OnLoad only from InitializePresenters after
// the GUI mirror graph is fully loaded and references are resolved. Call
// OnUnload only from UnloadPresenters for a graph that completed that pass.
// The object destructor does not tear down native presentation.
class Presenter : public ae::Obj {
  APPTRAVERSE_OBJECT(Presenter, ae::Obj, 0)

 protected:
  Presenter() = default;

 public:
  explicit Presenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  virtual void OnLoad() {}
  virtual void OnUnload() {}

  // Runtime-only. Not serialized. Win32 uses this as CreateWindow lpParam.
  void* presentation_host{nullptr};
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_PRESENTER_H_
