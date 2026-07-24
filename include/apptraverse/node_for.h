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

  void CaptureBaseState() {
    Node::CaptureBaseState(static_cast<ConcreteNode&>(*this));
  }

  void RebuildFromBaseAndReplay() {
    Node::RebuildFromBaseAndReplay(static_cast<ConcreteNode&>(*this));
  }

 private:
  void AcceptRemoteEventImpl(Event::ptr event, ae::TimePoint time) override {
    Node::AcceptRemoteEvent(static_cast<ConcreteNode&>(*this), std::move(event),
                            time);
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_FOR_H_
