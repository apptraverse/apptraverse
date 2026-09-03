#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include <cassert>

#include "aether-objects/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

class Node;

class Event : public ae::Obj {
  APPTRAVERSE_OBJECT(Event, ae::Obj, 0)

  friend class Node;

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  bool CanApplyTo(Node const& target) const { return CanApplyToImpl(target); }

 private:
  void ApplyTo(ae::Obj& target) const { ApplyToImpl(target); }

  virtual void ApplyToImpl(ae::Obj& target) const = 0;

  virtual bool CanApplyToImpl(Node const& target) const {
    (void)target;
    assert(false && "Concrete Event must inherit through EventFor");
    return false;
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
