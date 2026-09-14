#ifndef APPTRAVERSE_SHARED_NODE_DEMO_MODEL_H_
#define APPTRAVERSE_SHARED_NODE_DEMO_MODEL_H_

#include <cstdint>
#include <string>
#include <stdexcept>

#include "apptraverse/event_for.h"
#include "apptraverse/link.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_event_order.h"
#include "apptraverse/shared_node.h"

namespace apptraverse::example::shared_node {

class SetValueEvent;

// Minimal SharedNode business fixture.
class SharedValueNode
    : public apptraverse::NodeFor<SharedValueNode, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::shared_node::SharedValueNode",
                           SharedValueNode, SharedNode, 1)

 protected:
  SharedValueNode() = default;

 public:
  explicit SharedValueNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("SharedValueNode v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    SharedNode::Load(ae::Version<1>{}, dnv);
    dnv(value);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    SharedNode::Save(ae::Version<1>{}, dnv);
    dnv(value);
  }

  std::int32_t value{0};

  void Apply(SetValueEvent const& event);

  void InsertAtForTest(SharedEventOrder order, Event::ptr event) {
    InsertEvent(EventRecord{.event = std::move(event),
                            .identity = {},
                            .order = std::move(order)});
  }
};

class SetValueEvent
    : public apptraverse::EventFor<SharedValueNode, SetValueEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::shared_node::SetValueEvent",
                           SetValueEvent, Event, 0)

 protected:
  SetValueEvent() = default;

 public:
  explicit SetValueEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::int32_t value{0};
};

// Ordinary business object that reuses the same Link instance.
class Client : public apptraverse::NodeFor<Client> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::shared_node::Client", Client,
                           Node, 1)

 protected:
  Client() = default;

 public:
  explicit Client(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(link))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("Client v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    Node::Load(ae::Version<2>{}, dnv);
    dnv(name, link);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    Node::Save(ae::Version<2>{}, dnv);
    dnv(name, link);
  }

  std::string name;
  apptraverse::Link::ptr link;
};

// Nested SharedNode: shared edge to a child SharedNode (both have local sync).
class ChildSharedNode
    : public apptraverse::NodeFor<ChildSharedNode, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::shared_node::ChildSharedNode",
                           ChildSharedNode, SharedNode, 1)

 protected:
  ChildSharedNode() = default;

 public:
  explicit ChildSharedNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(child_value))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("ChildSharedNode v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    SharedNode::Load(ae::Version<1>{}, dnv);
    dnv(child_value);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    SharedNode::Save(ae::Version<1>{}, dnv);
    dnv(child_value);
  }

  std::int32_t child_value{0};
};

class RootSharedNode
    : public apptraverse::NodeFor<RootSharedNode, apptraverse::SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::shared_node::RootSharedNode",
                           RootSharedNode, SharedNode, 1)

 protected:
  RootSharedNode() = default;

 public:
  explicit RootSharedNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(root_value), AE_MMBR(child))

  template <typename Dnv>
  void Load(ae::Version<0>, Dnv&) {
    throw std::runtime_error("RootSharedNode v0 is not supported");
  }

  template <typename Dnv>
  void Load(ae::Version<1>, Dnv& dnv) {
    SharedNode::Load(ae::Version<1>{}, dnv);
    dnv(root_value, child);
  }

  template <typename Dnv>
  void Save(ae::Version<1>, Dnv& dnv) const {
    SharedNode::Save(ae::Version<1>{}, dnv);
    dnv(root_value, child);
  }

  std::int32_t root_value{0};
  ChildSharedNode::ptr child;
};

// Generic non-SharedNode LocalPtr fixture (proves network exclusion is not
// SharedNode-specific).
class LocalOnlyPayload : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT(
      "apptraverse::example::shared_node::LocalOnlyPayload", LocalOnlyPayload,
      ae::Obj, 0)

 protected:
  LocalOnlyPayload() = default;

 public:
  explicit LocalOnlyPayload(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(mark))

  std::string mark;
};

class LocalPtrHolder : public ae::Obj {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::example::shared_node::LocalPtrHolder",
                           LocalPtrHolder, ae::Obj, 0)

 protected:
  LocalPtrHolder() = default;

 public:
  explicit LocalPtrHolder(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(local))

  std::string name;
  apptraverse::LocalPtr<LocalOnlyPayload> local;
};

void EnsureSharedNodeDemoRegistration();

}  // namespace apptraverse::example::shared_node

#endif  // APPTRAVERSE_SHARED_NODE_DEMO_MODEL_H_
