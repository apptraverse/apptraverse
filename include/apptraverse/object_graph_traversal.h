#ifndef APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_
#define APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_

#include <type_traits>
#include <utility>

#include "aether-miscpp/reflect/domain_visitor.h"

#include "apptraverse/event.h"
#include "apptraverse/node.h"

namespace apptraverse {

template <typename Target, typename ConcreteEvent>
class EventFor;

namespace detail {

class ObjectGraphTraversal {
  template <typename Target, typename ConcreteEvent>
  friend class ::apptraverse::EventFor;

 public:
  virtual ~ObjectGraphTraversal() = default;

  template <typename Root>
  void Traverse(Root& root) {
    TraverseConcrete(root);
  }

 protected:
  virtual void OnNode(Node& node) = 0;

 private:
  template <typename Value>
  void TraverseConcrete(Value& value) {
    auto graph_visitor = [this](auto& current) -> bool {
      using Current = std::remove_cvref_t<decltype(current)>;

      if constexpr (std::is_base_of_v<Node, Current>) {
        OnNode(static_cast<Node&>(current));
      }

      if constexpr (std::is_base_of_v<Event, Current>) {
        static_cast<Event&>(current).TraverseObjectGraph(*this);
        return false;
      }

      return true;
    };

    using DeepVisitor =
        ae::reflect::DomainNodeVisitor<decltype(graph_visitor),
                                       ae::reflect::VisitPolicy::kDeep>;
    ae::reflect::DomainVisit(cycle_detector_, value,
                             DeepVisitor{std::move(graph_visitor)});
  }

  ae::reflect::CycleDetector cycle_detector_;
};

}  // namespace detail
}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_
