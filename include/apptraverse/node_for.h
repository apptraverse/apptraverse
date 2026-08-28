#ifndef APPTRAVERSE_NODE_FOR_H_
#define APPTRAVERSE_NODE_FOR_H_

#include <type_traits>
#include <utility>

#include "apptraverse/node.h"
#include "apptraverse/shared_event_id.h"
#include "apptraverse/shared_event_order.h"

namespace apptraverse {

template <typename ConcreteNode, typename BaseNode = Node>
class NodeFor : public BaseNode {
  static_assert(std::is_base_of_v<Node, BaseNode>,
                "NodeFor BaseNode must derive from Node");

 protected:
  NodeFor() = default;

  explicit NodeFor(ae::ObjProp prop) : BaseNode{prop} {}

 public:
  void ReplayFromBase() {
    Node::RebuildFromBaseAndReplay(static_cast<ConcreteNode&>(*this));
  }

  // Shared replication insert with canonical SharedEventOrder (may mid-insert).
  void InsertSharedOrderedEvent(Event::ptr event, SharedEventId identity,
                                SharedEventOrder order) {
    EventRecord record{.event = std::move(event),
                       .identity = std::move(identity),
                       .order = std::move(order)};
    InsertEvent(std::move(record));
  }

  // Shared local commit: identity/order known before journal insertion.
  void CommitShared(Event::ptr event, SharedEventId identity,
                    SharedEventOrder order) {
    Node::CommitSharedInto(static_cast<ConcreteNode&>(*this), std::move(event),
                           std::move(identity), std::move(order));
  }

 private:
  void CaptureBaseStateImpl() override {
    Node::CaptureBaseStateInto(static_cast<ConcreteNode&>(*this));
  }

  void CommitImpl(Event::ptr event) override {
    Node::CommitInto(static_cast<ConcreteNode&>(*this), std::move(event));
  }

 protected:
  void RebuildFromBaseAndReplay() {
    Node::RebuildFromBaseAndReplay(static_cast<ConcreteNode&>(*this));
  }

  void InsertEvent(EventRecord record) {
    Node::InsertEvent(static_cast<ConcreteNode&>(*this), std::move(record));
  }
};

}  // namespace apptraverse

#endif  // APPTRAVERSE_NODE_FOR_H_
