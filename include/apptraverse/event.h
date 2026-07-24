#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include "aether/obj/obj.h"

namespace apptraverse {

class Node;

namespace detail {
class ObjectGraphTraversal;
}

class Event : public ae::Obj {
  AE_OBJECT(Event, Obj, 0)

  friend class Node;
  friend class detail::ObjectGraphTraversal;

 protected:
  Event() = default;

 public:
  explicit Event(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT()

 private:
  virtual void ApplyTo(Node& target) const = 0;
  virtual void TraverseObjectGraph(detail::ObjectGraphTraversal& traversal) = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
