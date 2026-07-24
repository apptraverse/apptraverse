#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include "aether/obj/obj.h"

namespace apptraverse {

class Event : public ae::Obj {
  AE_OBJECT(Event, Obj, 0)

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
