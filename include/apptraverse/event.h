#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include "aether/obj/obj.h"

#include "apptraverse/object_macros.h"

namespace apptraverse {

class Event : public ae::Obj {
  APPTRAVERSE_OBJECT(Event, ae::Obj, 0)

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

  void ApplyTo(ae::Obj& target) const { ApplyToImpl(target); }

 private:
  virtual void ApplyToImpl(ae::Obj& target) const = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
