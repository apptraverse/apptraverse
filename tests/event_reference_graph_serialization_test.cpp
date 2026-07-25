#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class SharedResource : public ae::Obj {
  AE_OBJECT(SharedResource, Obj, 0)

 protected:
  SharedResource() = default;

 public:
  explicit SharedResource(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

class ReferencedObject : public ae::Obj {
  AE_OBJECT(ReferencedObject, Obj, 0)

 protected:
  ReferencedObject() = default;

 public:
  explicit ReferencedObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(resource))

  std::string name;
  SharedResource::ptr resource;
};

class ObjectReferenceEvent;

class ReferenceNode : public apptraverse::NodeFor<ReferenceNode> {
  AE_OBJECT(ReferenceNode, Node, 0)

 protected:
  ReferenceNode() = default;

 public:
  explicit ReferenceNode(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT()

  void Apply(ObjectReferenceEvent const&);
};

class ObjectReferenceEvent
    : public apptraverse::EventFor<ReferenceNode, ObjectReferenceEvent> {
  AE_OBJECT(ObjectReferenceEvent, Event, 0)

 protected:
  ObjectReferenceEvent() = default;

 public:
  explicit ObjectReferenceEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(referenced))

  ReferencedObject::ptr referenced;
};

void ReferenceNode::Apply(ObjectReferenceEvent const&) {}

}  // namespace apptraverse::test

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " (" << __FILE__ << ':'         \
                << __LINE__ << ")\n";                                        \
      return EXIT_FAILURE;                                                   \
    }                                                                        \
  } while (0)

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId::Type id) {
  return storage.state.find(ae::ObjId{id}) != storage.state.end();
}

}  // namespace

int main() {
  using apptraverse::test::ObjectReferenceEvent;
  using apptraverse::test::ReferencedObject;
  using apptraverse::test::SharedResource;

  {
    ae::RamDomainStorage reference_storage;
    ae::Domain sender_domain{ae::Now(), reference_storage};
    ae::Domain receiver_domain{ae::Now(), reference_storage};

    ReferencedObject::ptr sender_object = ReferencedObject::ptr::Create(
        ae::CreateWith{sender_domain}.with_id(300));
    CHECK(static_cast<bool>(sender_object));
    sender_object->name = "Sender object";

    ObjectReferenceEvent::ptr event = ObjectReferenceEvent::ptr::Create(
        ae::CreateWith{sender_domain}.with_id(200));
    CHECK(static_cast<bool>(event));

    event->referenced = sender_object;
    event->referenced.Reset();
    event->referenced.SetFlags(ae::ObjFlags::kUnloadedByDefault);

    CHECK(sender_object.is_valid());
    CHECK(sender_object.is_loaded());
    CHECK(event->referenced.is_valid());
    CHECK(event->referenced.id().id() == 300);
    CHECK(!event->referenced.is_loaded());

    event.Save();

    CHECK(ContainsObj(reference_storage, 200));
    CHECK(!ContainsObj(reference_storage, 300));

    ReferencedObject::ptr receiver_object = ReferencedObject::ptr::Create(
        ae::CreateWith{receiver_domain}.with_id(300));
    CHECK(static_cast<bool>(receiver_object));
    receiver_object->name = "Receiver object";

    auto* receiver_object_address = receiver_object.Load().get();
    CHECK(receiver_object_address != nullptr);

    ObjectReferenceEvent::ptr loaded_event =
        ObjectReferenceEvent::ptr::Declare(
            ae::CreateWith{receiver_domain}.with_id(200));
    loaded_event.Load();

    CHECK(static_cast<bool>(loaded_event));
    CHECK(loaded_event->GetClassId() == ObjectReferenceEvent::kClassId);
    CHECK(loaded_event->referenced.is_valid());
    CHECK(loaded_event->referenced.id().id() == 300);
    CHECK(!loaded_event->referenced.is_loaded());

    loaded_event->referenced.Load();

    CHECK(loaded_event->referenced.is_loaded());
    CHECK(loaded_event->referenced.Load().get() == receiver_object_address);
    CHECK(loaded_event->referenced->name == "Receiver object");
    CHECK(loaded_event->referenced.Load().get() != sender_object.Load().get());
  }

  {
    ae::RamDomainStorage graph_storage;
    ae::Domain graph_sender_domain{ae::Now(), graph_storage};
    ae::Domain graph_receiver_domain{ae::Now(), graph_storage};

    SharedResource::ptr sender_resource = SharedResource::ptr::Create(
        ae::CreateWith{graph_sender_domain}.with_id(500));
    CHECK(static_cast<bool>(sender_resource));
    sender_resource->name = "Sender Theme";

    ReferencedObject::ptr sender_object = ReferencedObject::ptr::Create(
        ae::CreateWith{graph_sender_domain}.with_id(501));
    CHECK(static_cast<bool>(sender_object));
    sender_object->name = "Graph object";

    sender_object->resource = sender_resource;
    sender_object->resource.Reset();
    sender_object->resource.SetFlags(ae::ObjFlags::kUnloadedByDefault);

    CHECK(sender_resource.is_valid());
    CHECK(sender_resource.is_loaded());
    CHECK(sender_object.is_loaded());
    CHECK(sender_object->resource.is_valid());
    CHECK(sender_object->resource.id().id() == 500);
    CHECK(!sender_object->resource.is_loaded());

    ObjectReferenceEvent::ptr event = ObjectReferenceEvent::ptr::Create(
        ae::CreateWith{graph_sender_domain}.with_id(201));
    CHECK(static_cast<bool>(event));
    event->referenced = sender_object;
    CHECK(event->referenced.is_loaded());

    event.Save();

    CHECK(ContainsObj(graph_storage, 201));
    CHECK(ContainsObj(graph_storage, 501));
    CHECK(!ContainsObj(graph_storage, 500));
    CHECK(graph_storage.state.size() == 2);

    SharedResource::ptr receiver_resource = SharedResource::ptr::Create(
        ae::CreateWith{graph_receiver_domain}.with_id(500));
    CHECK(static_cast<bool>(receiver_resource));
    receiver_resource->name = "Receiver Theme";

    auto* receiver_resource_address = receiver_resource.Load().get();
    CHECK(receiver_resource_address != nullptr);

    ObjectReferenceEvent::ptr loaded_event =
        ObjectReferenceEvent::ptr::Declare(
            ae::CreateWith{graph_receiver_domain}.with_id(201));
    loaded_event.Load();

    CHECK(static_cast<bool>(loaded_event));
    CHECK(loaded_event->GetClassId() == ObjectReferenceEvent::kClassId);
    CHECK(loaded_event->referenced.is_valid());
    CHECK(loaded_event->referenced.is_loaded());
    CHECK(loaded_event->referenced.id().id() == 501);
    CHECK(loaded_event->referenced->name == "Graph object");
    CHECK(loaded_event->referenced.Load().get() != sender_object.Load().get());

    CHECK(loaded_event->referenced->resource.is_valid());
    CHECK(loaded_event->referenced->resource.id().id() == 500);
    CHECK(!loaded_event->referenced->resource.is_loaded());

    loaded_event->referenced->resource.Load();

    CHECK(loaded_event->referenced->resource.is_loaded());
    CHECK(loaded_event->referenced->resource.Load().get() ==
          receiver_resource_address);
    CHECK(loaded_event->referenced->resource->name == "Receiver Theme");
    CHECK(loaded_event->referenced->resource.Load().get() !=
          sender_resource.Load().get());
  }

  return EXIT_SUCCESS;
}
