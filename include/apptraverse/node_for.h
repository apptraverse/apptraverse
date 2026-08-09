#ifndef APPTRAVERSE_NODE_FOR_H_
#define APPTRAVERSE_NODE_FOR_H_

#include <utility>

#include "apptraverse/node.h"

namespace apptraverse {

template <typename ConcreteNode>
class NodeFor : public Node {
 protected:
  NodeFor() = default;

  explicit NodeFor(ae::ObjProp prop) : Node{prop} {}

 public:
  void Commit(Event::ptr event) { CommitImpl(std::move(event)); }

 protected:
  void CaptureBaseState() {
    Node::CaptureBaseState(static_cast<ConcreteNode&>(*this));
  }

  void RebuildFromBaseAndReplay() {
    Node::RebuildFromBaseAndReplay(static_cast<ConcreteNode&>(*this));
  }

  void InsertEvent(EventRecord record) {
    Node::InsertEvent(static_cast<ConcreteNode&>(*this), std::move(record));
  }

 private:
  void CaptureBaseStateImpl() override { CaptureBaseState(); }

  void ReloadFromStorageImpl() override {
    assert(domain != nullptr);
    assert(obj_id.IsValid());
    ae::DomainGraph graph{domain};
    graph.Load(static_cast<ConcreteNode&>(*this), obj_id);
  }

  void CommitImpl(Event::ptr event) override {
    Node::Commit(static_cast<ConcreteNode&>(*this), std::move(event));
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_FOR_H_
