#ifndef APPTRAVERSE_NODE_H_
#define APPTRAVERSE_NODE_H_

#include <vector>

#include "aether/obj/obj.h"

#include "apptraverse/event_record.h"

namespace apptraverse {

class Node : public ae::Obj {
  AE_OBJECT(Node, Obj, 0)

 protected:
  Node() = default;

 public:
  explicit Node(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base), AE_MMBR(journal))

  Node::ptr base;
  std::vector<EventRecord> journal;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_H_
