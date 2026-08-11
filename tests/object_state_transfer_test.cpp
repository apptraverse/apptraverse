#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/object_state.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class TransferSharedNode;
class TransferLocalNode;
class TransferRootNode;
class TransferOwnedChild;
class TransferOwnedObject;
class TransferProbeEvent;

class TransferOwnedChild : public ae::Obj {
  APPTRAVERSE_OBJECT(TransferOwnedChild, ae::Obj, 0)

 protected:
  TransferOwnedChild() = default;

 public:
  explicit TransferOwnedChild(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class TransferOwnedObject : public ae::Obj {
  APPTRAVERSE_OBJECT(TransferOwnedObject, ae::Obj, 0)

 protected:
  TransferOwnedObject() = default;

 public:
  explicit TransferOwnedObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(child))

  std::string label;
  ae::ObjPtr<TransferOwnedChild> child;
};

class TransferSharedNode : public NodeFor<TransferSharedNode> {
  APPTRAVERSE_OBJECT(TransferSharedNode, Node, 0)

 protected:
  TransferSharedNode() = default;

 public:
  explicit TransferSharedNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class TransferLocalNode : public NodeFor<TransferLocalNode> {
  APPTRAVERSE_OBJECT(TransferLocalNode, Node, 0)

 protected:
  TransferLocalNode() = default;

 public:
  explicit TransferLocalNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label))

  std::string label;
};

class TransferRootNode : public NodeFor<TransferRootNode> {
  APPTRAVERSE_OBJECT(TransferRootNode, Node, 0)

 protected:
  TransferRootNode() = default;

 public:
  explicit TransferRootNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(label), AE_MMBR(owned), AE_MMBR(shared_peer),
                    AE_MMBR(local_peer))

  std::string label;
  ae::ObjPtr<TransferOwnedObject> owned;
  SharedPtr<TransferSharedNode> shared_peer;
  LocalPtr<TransferLocalNode> local_peer;

  void Apply(TransferProbeEvent const&) {}
};

class TransferProbeEvent : public EventFor<TransferRootNode, TransferProbeEvent> {
  APPTRAVERSE_OBJECT(TransferProbeEvent, Event, 0)

 protected:
  TransferProbeEvent() = default;

 public:
  explicit TransferProbeEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(peer), AE_MMBR(note))

  SharedPtr<TransferSharedNode> peer;
  std::string note;
};

APPTRAVERSE_REGISTER(TransferOwnedChild);
APPTRAVERSE_REGISTER(TransferOwnedObject);
APPTRAVERSE_REGISTER(TransferSharedNode);
APPTRAVERSE_REGISTER(TransferLocalNode);
APPTRAVERSE_REGISTER(TransferRootNode);
APPTRAVERSE_REGISTER(TransferProbeEvent);

bool StateContains(ObjectState const& state, ae::ObjId id) {
  for (auto const& object : state.objects) {
    if (object.obj_id == id) {
      return true;
    }
  }
  return false;
}

void TestCaptureRootExcludesSharedAndLocal() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto root_base =
      TransferRootNode::ptr::Create(ae::CreateWith{domain}.with_id(10));
  auto root =
      TransferRootNode::ptr::Create(ae::CreateWith{domain}.with_id(11));
  auto shared =
      TransferSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(12));
  auto local =
      TransferLocalNode::ptr::Create(ae::CreateWith{domain}.with_id(13));
  auto owned =
      TransferOwnedObject::ptr::Create(ae::CreateWith{domain}.with_id(14));
  auto child =
      TransferOwnedChild::ptr::Create(ae::CreateWith{domain}.with_id(15));

  root->label = "root";
  shared->label = "shared";
  local->label = "local";
  owned->label = "owned";
  child->label = "child";
  owned->child = child;
  root->owned = owned;
  root->shared_peer = shared;
  root->local_peer = local;
  root->base = root_base;
  root->CaptureBaseState();

  auto const source_local_id = root->local_peer.id();
  auto const source_shared_id = root->shared_peer.id();
  CHECK(source_local_id.id() == 13);
  CHECK(source_shared_id.id() == 12);
  CHECK(root->local_peer.is_valid());
  CHECK(root->shared_peer.is_loaded());

  auto const state = CaptureNodeState(root, storage);
  CHECK(state.root_id.id() == 11);
  CHECK(StateContains(state, root.id()));
  CHECK(StateContains(state, root_base.id()));
  CHECK(StateContains(state, owned.id()));
  CHECK(StateContains(state, child.id()));
  CHECK(!StateContains(state, shared.id()));
  CHECK(!StateContains(state, local.id()));

  // Source replica must be unchanged.
  CHECK(root->local_peer.is_valid());
  CHECK(root->local_peer.id() == source_local_id);
  CHECK(root->shared_peer.id() == source_shared_id);
  CHECK(root->shared_peer.is_loaded());
  CHECK(root->label == "root");
  CHECK(local->label == "local");
  CHECK(shared->label == "shared");

  ae::RamDomainStorage target_storage;
  ImportObjectState(state, target_storage);
  ae::Domain target_domain{ae::Now(), target_storage};
  auto loaded = TransferRootNode::ptr::Declare(
      ae::CreateWith{target_domain}.with_id(11));
  loaded.Load();
  CHECK(loaded.is_loaded());
  CHECK(loaded->GetClassId() == TransferRootNode::kClassId);
  CHECK(loaded->label == "root");
  CHECK(loaded->owned.is_valid());
  loaded->owned.Load();
  CHECK(loaded->owned.is_loaded());
  CHECK(loaded->owned->label == "owned");
  loaded->owned->child.Load();
  CHECK(loaded->owned->child->label == "child");

  CHECK(loaded->shared_peer.id() == source_shared_id);
  CHECK(loaded->shared_peer.domain() == &target_domain);
  CHECK(!loaded->shared_peer.is_loaded());
  CHECK(!loaded->local_peer.is_valid());
}

void TestCaptureEventSharedPtrReferenceOnly() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto shared =
      TransferSharedNode::ptr::Create(ae::CreateWith{domain}.with_id(21));
  shared->label = "event-shared";
  auto event =
      TransferProbeEvent::ptr::Create(ae::CreateWith{domain}.with_id(22));
  event->peer = shared;
  event->note = "hello";
  event.Save();

  auto const state = CaptureEventState(event, storage);
  CHECK(state.root_id.id() == 22);
  CHECK(StateContains(state, event.id()));
  CHECK(!StateContains(state, shared.id()));

  ae::RamDomainStorage target_storage;
  ImportObjectState(state, target_storage);
  ae::Domain target_domain{ae::Now(), target_storage};
  auto loaded = TransferProbeEvent::ptr::Declare(
      ae::CreateWith{target_domain}.with_id(22));
  loaded.Load();
  CHECK(loaded.is_loaded());
  CHECK(loaded->note == "hello");
  CHECK(loaded->peer.id().id() == 21);
  CHECK(loaded->peer.domain() == &target_domain);
  CHECK(!loaded->peer.is_loaded());
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestCaptureRootExcludesSharedAndLocal();
  apptraverse::test::TestCaptureEventSharedPtrReferenceOnly();
  return 0;
}
