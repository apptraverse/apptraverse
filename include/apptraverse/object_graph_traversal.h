#ifndef APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_
#define APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_

#include <algorithm>
#include <cassert>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether-miscpp/reflect/domain_visitor.h"
#include "aether/obj/obj_id.h"

#include "apptraverse/node.h"

namespace apptraverse {
namespace detail {

class ObjectGraphTraversal {
 public:
  virtual ~ObjectGraphTraversal() = default;

  template <typename Root>
  void Traverse(Root& root) {
    auto visitor = [&](auto& current) -> bool {
      using Current = std::remove_cvref_t<decltype(current)>;
      if constexpr (std::is_base_of_v<Node, Current>) {
        auto& node = static_cast<Node&>(current);
        if (MarkNode(node)) {
          OnNode(node);
        }
      }
      return true;
    };

    using DeepVisitor =
        ae::reflect::DomainNodeVisitor<decltype(visitor),
                                       ae::reflect::VisitPolicy::kDeep>;
    ae::reflect::CycleDetector detector;
    ae::reflect::DomainVisit(detector, root, DeepVisitor{std::move(visitor)});
  }

 protected:
  virtual void OnNode(Node& node) = 0;

 private:
  bool MarkNode(Node& node) {
    assert(node.obj_id.IsValid());
    if (std::find(visited_nodes_.begin(), visited_nodes_.end(), node.obj_id) !=
        visited_nodes_.end()) {
      return false;
    }
    visited_nodes_.push_back(node.obj_id);
    return true;
  }

  std::vector<ae::ObjId> visited_nodes_;
};

}  // namespace detail
}  // namespace apptraverse

#endif  // APPTRAVERSE_OBJECT_GRAPH_TRAVERSAL_H_
