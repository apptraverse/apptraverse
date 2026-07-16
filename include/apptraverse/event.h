#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include "aether/obj/obj.h"

namespace apptraverse {

class Node;

class Event : public ae::Obj {
  AE_OBJECT(Event, ae::Obj, 0)

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : ae::Obj{prop} {}

  AE_OBJECT_REFLECT()

 private:
  virtual bool ApplyTo(Node& target) const = 0;

  friend class Node;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
