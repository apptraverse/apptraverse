#ifndef APPTRAVERSE_EVENT_H_
#define APPTRAVERSE_EVENT_H_

#include <cassert>
#include <cstdint>

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

  AE_OBJECT_REFLECT(AE_MMBR(sender), AE_MMBR(sequence))

  ae::Obj::ptr sender;
  std::uint32_t sequence{0};

  bool HasValidIdentity() const {
    return sender.is_valid() && sequence != 0;
  }

  bool HasSameIdentity(Event const& other) const {
    assert(HasValidIdentity());
    assert(other.HasValidIdentity());

    return sender.id() == other.sender.id() && sequence == other.sequence;
  }

 private:
  virtual void ApplyTo(Node& target) const = 0;
  virtual void TraverseObjectGraph(detail::ObjectGraphTraversal& traversal) = 0;
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_H_
