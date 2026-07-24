#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/node.h"

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

class ClientObject : public ae::Obj {
  AE_OBJECT(ClientObject, Obj, 0)

 protected:
  ClientObject() = default;

 public:
  explicit ClientObject(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(resource))

  std::string name;
  SharedResource::ptr resource;
};

class ClientReferenceEvent;

class ReferenceNode : public apptraverse::Node {
  AE_OBJECT(ReferenceNode, Node, 0)

 protected:
  ReferenceNode() = default;

 public:
  explicit ReferenceNode(ae::ObjProp prop) : Node{prop} {}

  AE_OBJECT_REFLECT()

  void Apply(ClientReferenceEvent const&);
};

class ClientReferenceEvent
    : public apptraverse::EventFor<ReferenceNode, ClientReferenceEvent> {
  AE_OBJECT(ClientReferenceEvent, Event, 0)

 protected:
  ClientReferenceEvent() = default;

 public:
  explicit ClientReferenceEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client))

  ClientObject::ptr client;
};

void ReferenceNode::Apply(ClientReferenceEvent const&) {}

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
  using apptraverse::test::ClientObject;
  using apptraverse::test::ClientReferenceEvent;
  using apptraverse::test::SharedResource;

  // Scenario 1: unloaded reference only.
  {
    ae::RamDomainStorage reference_storage;
    ae::Domain sender_domain{ae::Now(), reference_storage};
    ae::Domain receiver_domain{ae::Now(), reference_storage};

    ClientObject::ptr sender_client =
        ClientObject::ptr::Create(ae::CreateWith{sender_domain}.with_id(300));
    CHECK(static_cast<bool>(sender_client));
    sender_client->name = "Sender Alice";

    ClientReferenceEvent::ptr event = ClientReferenceEvent::ptr::Create(
        ae::CreateWith{sender_domain}.with_id(200));
    CHECK(static_cast<bool>(event));

    event->client = sender_client;
    event->client.Reset();
    event->client.SetFlags(ae::ObjFlags::kUnloadedByDefault);

    CHECK(sender_client.is_valid());
    CHECK(sender_client.is_loaded());
    CHECK(event->client.is_valid());
    CHECK(event->client.id().id() == 300);
    CHECK(!event->client.is_loaded());

    event.Save();

    CHECK(ContainsObj(reference_storage, 200));
    CHECK(!ContainsObj(reference_storage, 300));

    ClientObject::ptr receiver_client = ClientObject::ptr::Create(
        ae::CreateWith{receiver_domain}.with_id(300));
    CHECK(static_cast<bool>(receiver_client));
    receiver_client->name = "Receiver Alice";

    auto* receiver_client_address = receiver_client.Load().get();
    CHECK(receiver_client_address != nullptr);

    ClientReferenceEvent::ptr loaded_event =
        ClientReferenceEvent::ptr::Declare(
            ae::CreateWith{receiver_domain}.with_id(200));
    loaded_event.Load();

    CHECK(static_cast<bool>(loaded_event));
    CHECK(loaded_event->GetClassId() == ClientReferenceEvent::kClassId);
    CHECK(loaded_event->client.is_valid());
    CHECK(loaded_event->client.id().id() == 300);
    CHECK(!loaded_event->client.is_loaded());

    loaded_event->client.Load();

    CHECK(loaded_event->client.is_loaded());
    CHECK(loaded_event->client.Load().get() == receiver_client_address);
    CHECK(loaded_event->client->name == "Receiver Alice");
    CHECK(loaded_event->client.Load().get() != sender_client.Load().get());
  }

  // Scenario 2: loaded client included, unloaded resource external.
  {
    ae::RamDomainStorage graph_storage;
    ae::Domain graph_sender_domain{ae::Now(), graph_storage};
    ae::Domain graph_receiver_domain{ae::Now(), graph_storage};

    SharedResource::ptr sender_resource = SharedResource::ptr::Create(
        ae::CreateWith{graph_sender_domain}.with_id(500));
    CHECK(static_cast<bool>(sender_resource));
    sender_resource->name = "Sender Theme";

    ClientObject::ptr sender_client = ClientObject::ptr::Create(
        ae::CreateWith{graph_sender_domain}.with_id(501));
    CHECK(static_cast<bool>(sender_client));
    sender_client->name = "Bob";

    sender_client->resource = sender_resource;
    sender_client->resource.Reset();
    sender_client->resource.SetFlags(ae::ObjFlags::kUnloadedByDefault);

    CHECK(sender_resource.is_valid());
    CHECK(sender_resource.is_loaded());
    CHECK(sender_client.is_loaded());
    CHECK(sender_client->resource.is_valid());
    CHECK(sender_client->resource.id().id() == 500);
    CHECK(!sender_client->resource.is_loaded());

    ClientReferenceEvent::ptr event = ClientReferenceEvent::ptr::Create(
        ae::CreateWith{graph_sender_domain}.with_id(201));
    CHECK(static_cast<bool>(event));
    event->client = sender_client;
    CHECK(event->client.is_loaded());

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

    ClientReferenceEvent::ptr loaded_event =
        ClientReferenceEvent::ptr::Declare(
            ae::CreateWith{graph_receiver_domain}.with_id(201));
    loaded_event.Load();

    CHECK(static_cast<bool>(loaded_event));
    CHECK(loaded_event->GetClassId() == ClientReferenceEvent::kClassId);
    CHECK(loaded_event->client.is_valid());
    CHECK(loaded_event->client.is_loaded());
    CHECK(loaded_event->client.id().id() == 501);
    CHECK(loaded_event->client->name == "Bob");
    CHECK(loaded_event->client.Load().get() != sender_client.Load().get());

    CHECK(loaded_event->client->resource.is_valid());
    CHECK(loaded_event->client->resource.id().id() == 500);
    CHECK(!loaded_event->client->resource.is_loaded());

    loaded_event->client->resource.Load();

    CHECK(loaded_event->client->resource.is_loaded());
    CHECK(loaded_event->client->resource.Load().get() ==
          receiver_resource_address);
    CHECK(loaded_event->client->resource->name == "Receiver Theme");
    CHECK(loaded_event->client->resource.Load().get() !=
          sender_resource.Load().get());
  }

  return EXIT_SUCCESS;
}
