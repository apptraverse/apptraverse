#ifndef APPTRAVERSE_NODE_FOR_H_
#define APPTRAVERSE_NODE_FOR_H_

#include <type_traits>
#include <utility>

#include "apptraverse/node.h"
#include "apptraverse/object_graph_copy_detail.h"
#include "apptraverse/shared_discovery.h"

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

  void ReloadFromStorageImpl() override {
    assert(this->domain != nullptr);
    assert(this->obj_id.IsValid());
    ae::DomainGraph graph{this->domain};
    graph.Load(static_cast<ConcreteNode&>(*this), this->obj_id);
  }

  void CommitImpl(Event::ptr event) override {
    Node::CommitInto(static_cast<ConcreteNode&>(*this), std::move(event));
  }

  void ReflectForSharedDiscoveryImpl(
      detail::SharedDiscoveryContext& ctx) override {
    detail::ReflectObjectForSharedDiscovery(static_cast<ConcreteNode&>(*this),
                                            ctx);
  }

  void PrepareSyncGraphImpl(detail::PrepareSyncGraphContext& ctx) override {
    detail::PrepareSyncGraphObject(static_cast<ConcreteNode&>(*this), ctx);
  }

  RemoteEventResult TryAcceptRemoteEventImpl(
      Event::ptr event, std::uint64_t original_timestamp_us) override {
    return Node::TryAcceptRemoteEventInto(static_cast<ConcreteNode&>(*this),
                                          std::move(event),
                                          original_timestamp_us);
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
