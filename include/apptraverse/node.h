#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include "aether/obj/obj.h"

#include "apptraverse/event.h"

namespace apptraverse {

class Node : public ae::Obj {
  AE_OBJECT(Node, ae::Obj, 0)

 protected:
  Node() = default;

 public:
  explicit Node(ae::ObjProp prop) : ae::Obj{prop} {}

  AE_OBJECT_REFLECT()

 protected:
  bool ApplyEvent(Event const& event) { return event.ApplyTo(*this); }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
