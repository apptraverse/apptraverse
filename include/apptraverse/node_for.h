#ifndef APPTRAVERSE_NODE_FOR_H_
#define APPTRAVERSE_NODE_FOR_H_

#include <type_traits>
#include <utility>

#include "apptraverse/node.h"

namespace apptraverse {

template <typename ConcreteNode, typename BaseNode = Node>
class NodeFor : public BaseNode {
  static_assert(std::is_base_of_v<Node, BaseNode>,
                "NodeFor BaseNode must derive from Node");

 protected:
  NodeFor() = default;

  explicit NodeFor(ae::ObjProp prop) : BaseNode{prop} {}

 private:
  void CaptureBaseStateImpl() override {
    Node::CaptureBaseStateInto(static_cast<ConcreteNode&>(*this));
  }

  void CommitImpl(Event::ptr event) override {
    Node::CommitInto(static_cast<ConcreteNode&>(*this), std::move(event));
  }

  void CompactJournalImpl(std::uint64_t now_us) override {
    Node::CompactJournalInto(static_cast<ConcreteNode&>(*this), now_us);
  }

  bool TryReplayFromBaseImpl() override {
    return Node::TryRebuildFromBaseAndReplay(static_cast<ConcreteNode&>(*this));
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
