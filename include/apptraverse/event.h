#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include "aether/obj/obj.h"

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

 private:
  void ApplyTo(ae::Obj& target) const { ApplyToImpl(target); }

  virtual void ApplyToImpl(ae::Obj& target) const = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
