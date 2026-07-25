#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/node_for.h"

namespace apptraverse::test {

class SharedResource : public ae::Obj {
  AE_OBJECT(SharedResource, Obj, 0)

 protected:
  SharedResource() = default;

 public:
  explicit SharedResource(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(value))

  std::string value;
};

static_assert(!std::is_base_of_v<apptraverse::Node, SharedResource>);

class Client;

class ClientPresenter : public ae::Obj {
  AE_OBJECT(ClientPresenter, Obj, 0)

 protected:
  ClientPresenter() = default;

 public:
  explicit ClientPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(caption), AE_MMBR(client))

  std::string caption;
  ae::ObjPtr<Client> client;
};

static_assert(!std::is_base_of_v<apptraverse::Node, ClientPresenter>);

class Client : public apptraverse::NodeFor<Client> {
  AE_OBJECT(Client, Node, 0)

 protected:
  Client() = default;

 public:
  explicit Client(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(presenter), AE_MMBR(resource))

  std::string name;
  ClientPresenter::ptr presenter;
  SharedResource::ptr resource;

  void CapturePrefabBaseForTest() { CaptureBaseState(); }

  Client::ptr Instantiate(std::string instance_name);
};

Client::ptr Client::Instantiate(std::string instance_name) {
  assert(domain != nullptr);
  assert(obj_id.IsValid());
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(presenter.is_valid());
  assert(presenter.is_loaded());
  assert(journal.empty());

  auto prefab = Client::ptr::MakeFromThis(this);
  auto instance = prefab.Clone();
  auto instance_base = base.Clone();
  auto instance_presenter = presenter.Clone();

  assert(instance.is_valid());
  assert(instance.is_loaded());
  assert(instance_base.is_valid());
  assert(instance_base.is_loaded());
  assert(instance_presenter.is_valid());
  assert(instance_presenter.is_loaded());
  assert(instance.id() != prefab.id());
  assert(instance_base.id() != base.id());
  assert(instance_presenter.id() != presenter.id());

  instance->base = instance_base;
  instance->presenter = instance_presenter;
  instance_presenter->client = instance;

  instance->name = std::move(instance_name);
  assert(instance->journal.empty());

  instance->CaptureBaseState();
  return instance;
}

class Runtime : public ae::Obj {
  AE_OBJECT(Runtime, Obj, 0)

 protected:
  Runtime() = default;

 public:
  explicit Runtime(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client_prefab))

  Client::ptr client_prefab;

  Client::ptr CreateClient(std::string name) {
    assert(client_prefab.is_valid());
    assert(client_prefab.is_loaded());
    return client_prefab->Instantiate(std::move(name));
  }
};

static_assert(!std::is_base_of_v<apptraverse::Node, Runtime>);

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
  using apptraverse::test::Client;
  using apptraverse::test::ClientPresenter;
  using apptraverse::test::Runtime;
  using apptraverse::test::SharedResource;

  ae::RamDomainStorage storage;
  ae::Domain distillation_domain{ae::Now(), storage};
  CHECK(storage.state.empty());

  SharedResource::ptr resource = SharedResource::ptr::Create(
      ae::CreateWith{distillation_domain}.with_id(50));
  CHECK(static_cast<bool>(resource));
  resource->value = "Default client resource";

  ClientPresenter::ptr presenter_prefab = ClientPresenter::ptr::Create(
      ae::CreateWith{distillation_domain}.with_id(12));
  CHECK(static_cast<bool>(presenter_prefab));
  presenter_prefab->caption = "Client presenter";

  Client::ptr client_base_prefab =
      Client::ptr::Create(ae::CreateWith{distillation_domain}.with_id(11));
  CHECK(static_cast<bool>(client_base_prefab));
  client_base_prefab->name = "";
  CHECK(!client_base_prefab->base.is_valid());
  CHECK(client_base_prefab->journal.empty());
  CHECK(!client_base_prefab->presenter.is_valid());
  CHECK(!client_base_prefab->resource.is_valid());

  Client::ptr client_prefab =
      Client::ptr::Create(ae::CreateWith{distillation_domain}.with_id(10));
  CHECK(static_cast<bool>(client_prefab));
  client_prefab->name = "";
  client_prefab->base = client_base_prefab;
  client_prefab->presenter = presenter_prefab;
  client_prefab->resource = resource;
  presenter_prefab->client = client_prefab;

  CHECK(client_prefab->base.Load().get() == client_base_prefab.Load().get());
  CHECK(client_prefab->presenter.Load().get() == presenter_prefab.Load().get());
  CHECK(client_prefab->resource.Load().get() == resource.Load().get());
  CHECK(presenter_prefab->client.Load().get() == client_prefab.Load().get());

  client_prefab->CapturePrefabBaseForTest();

  CHECK(storage.state.size() == 3);
  CHECK(ContainsObj(storage, 11));
  CHECK(ContainsObj(storage, 12));
  CHECK(ContainsObj(storage, 50));
  CHECK(!ContainsObj(storage, 1));
  CHECK(!ContainsObj(storage, 10));

  auto* captured_base = client_prefab->base.Load().as<Client>();
  CHECK(captured_base != nullptr);
  CHECK(captured_base->name.empty());
  CHECK(!captured_base->base.is_valid());
  CHECK(captured_base->journal.empty());
  CHECK(captured_base->presenter.id().id() == 12);
  CHECK(captured_base->presenter.Load().get() == presenter_prefab.Load().get());
  CHECK(captured_base->resource.id().id() == 50);
  CHECK(captured_base->resource.Load().get() == resource.Load().get());

  CHECK(presenter_prefab->client.id().id() == 10);
  CHECK(presenter_prefab->client.Load().get() == client_prefab.Load().get());
  CHECK(presenter_prefab->client.Load().get() !=
        client_base_prefab.Load().get());

  Runtime::ptr runtime =
      Runtime::ptr::Create(ae::CreateWith{distillation_domain}.with_id(1));
  CHECK(static_cast<bool>(runtime));
  runtime->client_prefab = client_prefab;
  runtime.Save();

  CHECK(storage.state.size() == 5);
  CHECK(ContainsObj(storage, 1));
  CHECK(ContainsObj(storage, 10));
  CHECK(ContainsObj(storage, 11));
  CHECK(ContainsObj(storage, 12));
  CHECK(ContainsObj(storage, 50));

  ae::Domain runtime_domain{ae::Now(), storage};
  Runtime::ptr loaded_runtime =
      Runtime::ptr::Declare(ae::CreateWith{runtime_domain}.with_id(1));
  loaded_runtime.Load();

  CHECK(loaded_runtime.is_loaded());
  CHECK(loaded_runtime->client_prefab.is_loaded());
  CHECK(loaded_runtime->client_prefab.id().id() == 10);
  CHECK(loaded_runtime->client_prefab->GetClassId() == Client::kClassId);
  CHECK(loaded_runtime->client_prefab->base.id().id() == 11);
  CHECK(loaded_runtime->client_prefab->base.is_loaded());
  CHECK(loaded_runtime->client_prefab->base.Load().as<Client>() != nullptr);
  CHECK(loaded_runtime->client_prefab->presenter.id().id() == 12);
  CHECK(loaded_runtime->client_prefab->presenter.is_loaded());
  CHECK(loaded_runtime->client_prefab->resource.id().id() == 50);
  CHECK(loaded_runtime->client_prefab->resource.is_loaded());
  CHECK(loaded_runtime->client_prefab->presenter->client.Load().get() ==
        loaded_runtime->client_prefab.Load().get());

  auto* loaded_prefab_base =
      loaded_runtime->client_prefab->base.Load().as<Client>();
  CHECK(loaded_prefab_base != nullptr);
  CHECK(loaded_prefab_base->presenter.Load().get() ==
        loaded_runtime->client_prefab->presenter.Load().get());
  CHECK(loaded_prefab_base->resource.Load().get() ==
        loaded_runtime->client_prefab->resource.Load().get());
  CHECK(loaded_runtime->client_prefab->journal.empty());
  CHECK(loaded_prefab_base->journal.empty());

  auto* prefab_address = loaded_runtime->client_prefab.Load().get();
  auto* prefab_base_address =
      loaded_runtime->client_prefab->base.Load().get();
  auto* prefab_presenter_address =
      loaded_runtime->client_prefab->presenter.Load().get();
  auto* resource_address =
      loaded_runtime->client_prefab->resource.Load().get();
  CHECK(prefab_address != nullptr);
  CHECK(prefab_base_address != nullptr);
  CHECK(prefab_presenter_address != nullptr);
  CHECK(resource_address != nullptr);

  auto alice = loaded_runtime->CreateClient("Alice");
  auto bob = loaded_runtime->CreateClient("Bob");

  CHECK(alice.is_valid());
  CHECK(alice.is_loaded());
  CHECK(bob.is_valid());
  CHECK(bob.is_loaded());
  CHECK(alice.id() != bob.id());
  CHECK(alice.id().id() != 10);
  CHECK(bob.id().id() != 10);
  CHECK(alice->base.id() != bob->base.id());
  CHECK(alice->base.id().id() != 11);
  CHECK(bob->base.id().id() != 11);
  CHECK(alice->presenter.id() != bob->presenter.id());
  CHECK(alice->presenter.id().id() != 12);
  CHECK(bob->presenter.id().id() != 12);

  CHECK(alice.Load().get() != bob.Load().get());
  CHECK(alice.Load().get() != prefab_address);
  CHECK(bob.Load().get() != prefab_address);
  CHECK(alice->base.Load().get() != bob->base.Load().get());
  CHECK(alice->base.Load().get() != prefab_base_address);
  CHECK(bob->base.Load().get() != prefab_base_address);
  CHECK(alice->presenter.Load().get() != bob->presenter.Load().get());
  CHECK(alice->presenter.Load().get() != prefab_presenter_address);
  CHECK(bob->presenter.Load().get() != prefab_presenter_address);

  CHECK(alice->name == "Alice");
  CHECK(alice->journal.empty());
  CHECK(alice->presenter.is_valid());
  CHECK(alice->presenter.is_loaded());
  CHECK(alice->presenter->caption == "Client presenter");
  CHECK(alice->presenter->client.id() == alice.id());
  CHECK(alice->presenter->client.Load().get() == alice.Load().get());

  CHECK(alice->base.is_valid());
  CHECK(alice->base.is_loaded());
  auto* alice_base = alice->base.Load().as<Client>();
  CHECK(alice_base != nullptr);
  CHECK(alice_base->name == "Alice");
  CHECK(!alice_base->base.is_valid());
  CHECK(alice_base->journal.empty());
  CHECK(alice_base->presenter.id() == alice->presenter.id());
  CHECK(alice_base->presenter.Load().get() == alice->presenter.Load().get());
  CHECK(alice->resource.id().id() == 50);
  CHECK(alice->resource.Load().get() == resource_address);
  CHECK(alice_base->resource.id().id() == 50);
  CHECK(alice_base->resource.Load().get() == alice->resource.Load().get());

  CHECK(bob->name == "Bob");
  CHECK(bob->journal.empty());
  CHECK(bob->presenter.is_valid());
  CHECK(bob->presenter.is_loaded());
  CHECK(bob->presenter->caption == "Client presenter");
  CHECK(bob->presenter->client.id() == bob.id());
  CHECK(bob->presenter->client.Load().get() == bob.Load().get());

  auto* bob_base = bob->base.Load().as<Client>();
  CHECK(bob_base != nullptr);
  CHECK(bob_base->name == "Bob");
  CHECK(!bob_base->base.is_valid());
  CHECK(bob_base->journal.empty());
  CHECK(bob_base->presenter.id() == bob->presenter.id());
  CHECK(bob_base->presenter.Load().get() == bob->presenter.Load().get());
  CHECK(bob->resource.id().id() == 50);
  CHECK(bob->resource.Load().get() == resource_address);
  CHECK(bob_base->resource.id().id() == 50);
  CHECK(bob_base->resource.Load().get() == bob->resource.Load().get());

  CHECK(alice->resource.Load().get() == bob->resource.Load().get());
  CHECK(alice->resource.Load().get() ==
        loaded_runtime->client_prefab->resource.Load().get());
  CHECK(alice->resource.id().id() == 50);
  CHECK(bob->resource.id().id() == 50);
  CHECK(loaded_runtime->client_prefab->resource.id().id() == 50);

  CHECK(loaded_runtime->client_prefab.id().id() == 10);
  CHECK(loaded_runtime->client_prefab->name.empty());
  CHECK(loaded_runtime->client_prefab->base.id().id() == 11);
  CHECK(loaded_runtime->client_prefab->presenter.id().id() == 12);
  CHECK(loaded_runtime->client_prefab->presenter->client.Load().get() ==
        prefab_address);
  CHECK(loaded_runtime->client_prefab->resource.id().id() == 50);
  CHECK(loaded_runtime->client_prefab->journal.empty());
  CHECK(loaded_prefab_base->name.empty());
  CHECK(loaded_prefab_base->journal.empty());
  CHECK(loaded_runtime->client_prefab.Load().get() == prefab_address);
  CHECK(loaded_runtime->client_prefab->base.Load().get() ==
        prefab_base_address);
  CHECK(loaded_runtime->client_prefab->presenter.Load().get() ==
        prefab_presenter_address);

  auto const alice_id = alice.id();
  auto const alice_base_id = alice->base.id();
  auto const alice_presenter_id = alice->presenter.id();
  auto const bob_id = bob.id();
  auto const bob_base_id = bob->base.id();
  auto const bob_presenter_id = bob->presenter.id();

  CHECK(storage.state.size() == 9);
  CHECK(ContainsObj(storage, alice_base_id.id()));
  CHECK(ContainsObj(storage, alice_presenter_id.id()));
  CHECK(ContainsObj(storage, bob_base_id.id()));
  CHECK(ContainsObj(storage, bob_presenter_id.id()));
  CHECK(!ContainsObj(storage, alice_id.id()));
  CHECK(!ContainsObj(storage, bob_id.id()));

  alice.Save();
  bob.Save();

  CHECK(storage.state.size() == 11);
  CHECK(ContainsObj(storage, alice_id.id()));
  CHECK(ContainsObj(storage, bob_id.id()));

  ae::Domain reload_domain{ae::Now(), storage};
  Runtime::ptr reloaded_runtime =
      Runtime::ptr::Declare(ae::CreateWith{reload_domain}.with_id(1));
  reloaded_runtime.Load();
  CHECK(reloaded_runtime.is_loaded());

  Client::ptr loaded_alice =
      Client::ptr::Declare(ae::CreateWith{reload_domain}.with_id(alice_id));
  loaded_alice.Load();
  Client::ptr loaded_bob =
      Client::ptr::Declare(ae::CreateWith{reload_domain}.with_id(bob_id));
  loaded_bob.Load();

  CHECK(loaded_alice.is_loaded());
  CHECK(loaded_bob.is_loaded());
  CHECK(loaded_alice->name == "Alice");
  CHECK(loaded_bob->name == "Bob");

  auto* loaded_alice_base = loaded_alice->base.Load().as<Client>();
  auto* loaded_bob_base = loaded_bob->base.Load().as<Client>();
  CHECK(loaded_alice_base != nullptr);
  CHECK(loaded_bob_base != nullptr);
  CHECK(loaded_alice_base->name == "Alice");
  CHECK(loaded_bob_base->name == "Bob");
  CHECK(loaded_alice_base->journal.empty());
  CHECK(loaded_bob_base->journal.empty());
  CHECK(!loaded_alice_base->base.is_valid());
  CHECK(!loaded_bob_base->base.is_valid());

  CHECK(loaded_alice->presenter.is_loaded());
  CHECK(loaded_bob->presenter.is_loaded());
  CHECK(loaded_alice->presenter.id() == alice_presenter_id);
  CHECK(loaded_bob->presenter.id() == bob_presenter_id);
  CHECK(loaded_alice->presenter->client.Load().get() ==
        loaded_alice.Load().get());
  CHECK(loaded_bob->presenter->client.Load().get() == loaded_bob.Load().get());
  CHECK(loaded_alice_base->presenter.Load().get() ==
        loaded_alice->presenter.Load().get());
  CHECK(loaded_bob_base->presenter.Load().get() ==
        loaded_bob->presenter.Load().get());
  CHECK(loaded_alice->resource.id().id() == 50);
  CHECK(loaded_bob->resource.id().id() == 50);
  CHECK(loaded_alice_base->resource.id().id() == 50);
  CHECK(loaded_bob_base->resource.id().id() == 50);

  CHECK(loaded_alice->presenter.Load().get() !=
        loaded_bob->presenter.Load().get());
  CHECK(loaded_alice->base.Load().get() != loaded_bob->base.Load().get());
  CHECK(loaded_alice->resource.Load().get() ==
        loaded_bob->resource.Load().get());
  CHECK(loaded_alice->resource.Load().get() ==
        reloaded_runtime->client_prefab->resource.Load().get());
  CHECK(loaded_alice.Load().get() != alice.Load().get());
  CHECK(loaded_bob.Load().get() != bob.Load().get());

  CHECK(reloaded_runtime->client_prefab.id().id() == 10);
  CHECK(reloaded_runtime->client_prefab->base.id().id() == 11);
  CHECK(reloaded_runtime->client_prefab->presenter.id().id() == 12);
  CHECK(reloaded_runtime->client_prefab->resource.id().id() == 50);
  CHECK(reloaded_runtime->client_prefab->presenter->client.Load().get() ==
        reloaded_runtime->client_prefab.Load().get());
  CHECK(reloaded_runtime->client_prefab->presenter->client.Load().get() !=
        loaded_alice.Load().get());
  CHECK(reloaded_runtime->client_prefab->presenter->client.Load().get() !=
        loaded_bob.Load().get());

  return EXIT_SUCCESS;
}
