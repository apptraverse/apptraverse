#ifndef APPTRAVERSE_EVENT_FOR_H_
#define APPTRAVERSE_EVENT_FOR_H_

#include "apptraverse/event.h"
#include "apptraverse/object_graph_traversal.h"

namespace apptraverse {

class Node;

template <typename Target, typename ConcreteEvent>
class EventFor : public Event {
 protected:
  EventFor() = default;
  explicit EventFor(ae::ObjProp prop) : Event{prop} {}

 private:
  void ApplyTo(Node& target) const override {
    static_cast<Target&>(target).Apply(
        static_cast<ConcreteEvent const&>(*this));
  }

  void TraverseObjectGraph(detail::ObjectGraphTraversal& traversal) override {
    traversal.TraverseConcrete(static_cast<ConcreteEvent&>(*this));
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_EVENT_FOR_H_
