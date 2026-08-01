#ifndef APPTRAVERSE_NODE_FOR_H_
#define APPTRAVERSE_NODE_FOR_H_

#include <utility>

#include "apptraverse/node.h"
#include "apptraverse/object_graph_traversal.h"

namespace apptraverse {

template <typename ConcreteNode>
class NodeFor : public Node {
 protected:
  NodeFor() = default;

  explicit NodeFor(ae::ObjProp prop) : Node{prop} {}

  void CaptureBaseState() {
    Node::CaptureBaseState(static_cast<ConcreteNode&>(*this));
  }

  void RebuildFromBaseAndReplay() {
    Node::RebuildFromBaseAndReplay(static_cast<ConcreteNode&>(*this));
  }

 private:
  bool AcceptSharedEventImpl(EventRecord record) override {
    return Node::InsertSharedEvent(static_cast<ConcreteNode&>(*this),
                                   std::move(record));
  }

  void CaptureBaseStateImpl() override { CaptureBaseState(); }

  void ReloadFromStorageImpl() override {
    assert(domain != nullptr);
    assert(obj_id.IsValid());
    ae::DomainGraph graph{domain};
    graph.Load(static_cast<ConcreteNode&>(*this), obj_id);
  }

  void TraverseSharedGraphImpl(
      detail::ObjectGraphTraversal& traversal) override {
    traversal.Traverse(static_cast<ConcreteNode&>(*this));
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_FOR_H_
