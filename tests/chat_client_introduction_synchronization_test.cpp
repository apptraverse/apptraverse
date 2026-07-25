#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
#include "apptraverse/event_identity.h"
#include "apptraverse/graph_journal_scanner.h"
#include "apptraverse/graph_synchronizer.h"
#include "apptraverse/journal_event_message.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/journal_message_transport.h"
#include "apptraverse/journal_transport_message.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

class Chat;
class Client;
class ChatPresenter;
class ClientPresenter;
class ClientJoinedEvent;

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

struct ChatEntry {
  ae::ObjPtr<Client> client;

  AE_REFLECT_MEMBERS(client)
};

class Chat : public apptraverse::NodeFor<Chat> {
  AE_OBJECT(Chat, Node, 0)

 protected:
  Chat() = default;

 public:
  explicit Chat(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(timeline), AE_MMBR(presenter))

  std::vector<ChatEntry> timeline;
  ae::ObjPtr<ChatPresenter> presenter;

  void Apply(ClientJoinedEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitFromPresenter(apptraverse::Event::ptr event, ae::TimePoint time,
                           ae::ObjId origin,
                           std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, origin, std::move(recipients));
  }
};

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

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(chat), AE_MMBR(presenter),
                    AE_MMBR(resource))

  std::string name;
  ae::ObjPtr<Chat> chat;
  ae::ObjPtr<ClientPresenter> presenter;
  SharedResource::ptr resource;

  void CapturePrefabBaseForTest() { CaptureBaseState(); }

  void PrepareForRemoteIntroduction();

  Client::ptr Instantiate(std::string name, ae::ObjPtr<Chat> chat);
};

class ClientJoinedEvent
    : public apptraverse::EventFor<Chat, ClientJoinedEvent> {
  AE_OBJECT(ClientJoinedEvent, Event, 0)

 protected:
  ClientJoinedEvent() = default;

 public:
  explicit ClientJoinedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(client))

  ae::ObjPtr<Client> client;
};

void Chat::Apply(ClientJoinedEvent const& event) {
  timeline.push_back(ChatEntry{
      event.client,
  });
}

class ChatPresenter : public ae::Obj {
  AE_OBJECT(ChatPresenter, Obj, 0)

 protected:
  ChatPresenter() = default;

 public:
  explicit ChatPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(caption), AE_MMBR(chat), AE_MMBR(client))

  std::string caption;
  ae::ObjPtr<Chat> chat;
  ae::ObjPtr<Client> client;

  void SetClient(ae::ObjPtr<Client> value) {
    assert(value.is_valid());
    assert(value.is_loaded());
    client = std::move(value);
  }

  void Join(ae::ObjId event_id, ae::TimePoint time,
            std::vector<ae::ObjId> recipients);

  bool IsLocal(ChatEntry const& entry) const {
    return client.is_valid() && entry.client.is_valid() &&
           client.id() == entry.client.id();
  }
};

static_assert(!std::is_base_of_v<apptraverse::Node, ChatPresenter>);

void Client::PrepareForRemoteIntroduction() {
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(chat.is_valid());
  assert(presenter.is_valid());
  assert(resource.is_valid());
  assert(journal.empty());

  auto* concrete_base = const_cast<Client*>(base.Load().as<Client>());
  assert(concrete_base != nullptr);

  auto unload = [](auto& pointer) {
    pointer.Reset();
    pointer.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  };

  unload(chat);
  unload(presenter);
  unload(resource);
  unload(concrete_base->chat);
  unload(concrete_base->presenter);
  unload(concrete_base->resource);
}

Client::ptr Client::Instantiate(std::string name, ae::ObjPtr<Chat> chat) {
  assert(domain != nullptr);
  assert(obj_id.IsValid());
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(presenter.is_valid());
  assert(presenter.is_loaded());
  assert(resource.is_valid());
  assert(resource.is_loaded());
  assert(journal.empty());
  assert(chat.is_valid());
  assert(chat.is_loaded());
  assert(chat.domain() == domain);

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

  instance->base = instance_base;
  instance->presenter = instance_presenter;
  instance_presenter->client = instance;
  instance->name = std::move(name);
  instance->chat = chat;
  assert(instance->journal.empty());

  instance->CaptureBaseState();
  return instance;
}

void ChatPresenter::Join(ae::ObjId event_id, ae::TimePoint time,
                         std::vector<ae::ObjId> recipients) {
  assert(domain != nullptr);
  assert(chat.is_valid());
  assert(chat.is_loaded());
  assert(client.is_valid());
  assert(client.is_loaded());
  assert(chat.domain() == domain);
  assert(client.domain() == domain);
  assert(event_id.IsValid());

  client->PrepareForRemoteIntroduction();

  ClientJoinedEvent::ptr event =
      ClientJoinedEvent::ptr::Create(ae::CreateWith{*domain}.with_id(event_id));
  event->client = client;
  assert(event->client.is_loaded());

  chat->CommitFromPresenter(event, time, client.id(), std::move(recipients));
}

class Runtime : public ae::Obj {
  AE_OBJECT(Runtime, Obj, 0)

 protected:
  Runtime() = default;

 public:
  explicit Runtime(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat), AE_MMBR(client_prefab))

  ae::ObjPtr<Chat> chat;
  ae::ObjPtr<Client> client_prefab;

  Client::ptr CreateLocalClient(std::string name) {
    assert(chat.is_valid());
    assert(chat.is_loaded());
    assert(client_prefab.is_valid());
    assert(client_prefab.is_loaded());
    assert(chat->presenter.is_valid());
    assert(chat->presenter.is_loaded());

    auto result = client_prefab->Instantiate(std::move(name), chat);
    chat->presenter->SetClient(result);
    return result;
  }
};

static_assert(!std::is_base_of_v<apptraverse::Node, Runtime>);

Runtime::ptr BuildInitialRuntime(ae::Domain& domain) {
  ChatPresenter::ptr chat_presenter =
      ChatPresenter::ptr::Create(ae::CreateWith{domain}.with_id(102));
  chat_presenter->caption = "Chat presenter";

  Chat::ptr chat_base =
      Chat::ptr::Create(ae::CreateWith{domain}.with_id(101));

  Chat::ptr chat = Chat::ptr::Create(ae::CreateWith{domain}.with_id(100));
  chat->base = chat_base;
  chat->presenter = chat_presenter;
  chat_presenter->chat = chat;
  chat->CaptureBaseStateForTest();

  SharedResource::ptr resource =
      SharedResource::ptr::Create(ae::CreateWith{domain}.with_id(50));
  resource->value = "Default client resource";

  ClientPresenter::ptr client_presenter_prefab =
      ClientPresenter::ptr::Create(ae::CreateWith{domain}.with_id(12));
  client_presenter_prefab->caption = "Client presenter";

  Client::ptr client_base_prefab =
      Client::ptr::Create(ae::CreateWith{domain}.with_id(11));
  client_base_prefab->name = "";

  Client::ptr client_prefab =
      Client::ptr::Create(ae::CreateWith{domain}.with_id(10));
  client_prefab->name = "";
  client_prefab->base = client_base_prefab;
  client_prefab->presenter = client_presenter_prefab;
  client_prefab->resource = resource;
  client_presenter_prefab->client = client_prefab;
  client_prefab->CapturePrefabBaseForTest();

  Runtime::ptr runtime =
      Runtime::ptr::Create(ae::CreateWith{domain}.with_id(1));
  runtime->chat = chat;
  runtime->client_prefab = client_prefab;
  runtime.Save();
  return runtime;
}

class LoopbackJournalMessageTransport final
    : public apptraverse::IJournalMessageTransport {
 public:
  LoopbackJournalMessageTransport(
      ae::Domain& message_domain, ae::RamDomainStorage& transfer_storage,
      ae::Domain& receiver_domain, ae::RamDomainStorage& receiver_storage,
      apptraverse::JournalMessageReceiver& receiver)
      : message_domain_{&message_domain},
        transfer_storage_{&transfer_storage},
        receiver_domain_{&receiver_domain},
        receiver_storage_{&receiver_storage},
        receiver_{&receiver} {}

  std::size_t send_count{0};
  std::vector<ae::ObjId> message_ids;

  void Send(apptraverse::JournalTransportMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    assert(message.domain() == message_domain_);

    auto* event_message =
        message.Load().as<apptraverse::JournalEventMessage>();
    assert(event_message != nullptr);
    assert(event_message->target.is_valid());
    assert(event_message->target.id().id() == 100);
    assert(!event_message->target.is_loaded());
    assert(event_message->identity.IsValid());
    assert(event_message->event.is_valid());
    assert(event_message->event.is_loaded());
    assert(event_message->event.id().id() == 200);

    apptraverse::EventIdentity const expected_identity =
        event_message->identity;

    message_ids.push_back(message.id());
    message.Save();

    receiver_storage_->state.insert(transfer_storage_->state.begin(),
                                    transfer_storage_->state.end());

    apptraverse::JournalTransportMessage::ptr incoming =
        apptraverse::JournalTransportMessage::ptr::Declare(
            ae::CreateWith{*receiver_domain_}.with_id(message.id()));
    incoming.Load();

    auto* incoming_event_message =
        incoming.Load().as<apptraverse::JournalEventMessage>();
    assert(incoming_event_message != nullptr);
    assert(incoming_event_message->identity == expected_identity);
    assert(incoming_event_message->event.is_valid());
    assert(!incoming_event_message->event.is_loaded());

    receiver_->Receive(incoming);

    assert(incoming_event_message->event.is_loaded());

    ++send_count;
  }

 private:
  ae::Domain* message_domain_;
  ae::RamDomainStorage* transfer_storage_;
  ae::Domain* receiver_domain_;
  ae::RamDomainStorage* receiver_storage_;
  apptraverse::JournalMessageReceiver* receiver_;
};

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

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId id) {
  return storage.state.find(id) != storage.state.end();
}

int CountPending(apptraverse::GraphJournalScanner const& scanner, auto& root,
                 ae::ObjId recipient) {
  int count = 0;
  scanner.VisitPending(
      root, recipient,
      [&](apptraverse::Node&, apptraverse::EventRecord&,
          apptraverse::EventRecipientState&) { ++count; });
  return count;
}

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::EventIdentity;
  using apptraverse::EventRecordOrigin;
  using apptraverse::GraphJournalScanner;
  using apptraverse::GraphSynchronizer;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::BuildInitialRuntime;
  using apptraverse::test::Chat;
  using apptraverse::test::Client;
  using apptraverse::test::ClientJoinedEvent;
  using apptraverse::test::LoopbackJournalMessageTransport;
  using apptraverse::test::Runtime;
  using apptraverse::test::SharedResource;

  ae::RamDomainStorage alice_storage;
  ae::RamDomainStorage support_storage;
  ae::RamDomainStorage transfer_storage;

  ae::Domain alice_domain{ae::Now(), alice_storage};
  ae::Domain support_domain{ae::Now(), support_storage};
  ae::Domain message_domain{ae::Now(), transfer_storage};

  auto alice_runtime = BuildInitialRuntime(alice_domain);
  auto support_runtime = BuildInitialRuntime(support_domain);

  CHECK(alice_storage.state.size() == 8);
  CHECK(support_storage.state.size() == 8);
  CHECK(ContainsObj(alice_storage, 1));
  CHECK(ContainsObj(alice_storage, 10));
  CHECK(ContainsObj(alice_storage, 11));
  CHECK(ContainsObj(alice_storage, 12));
  CHECK(ContainsObj(alice_storage, 50));
  CHECK(ContainsObj(alice_storage, 100));
  CHECK(ContainsObj(alice_storage, 101));
  CHECK(ContainsObj(alice_storage, 102));

  CHECK(alice_runtime.id().id() == 1);
  CHECK(support_runtime.id().id() == 1);
  CHECK(alice_runtime.Load().get() != support_runtime.Load().get());
  CHECK(alice_runtime->chat.id().id() == 100);
  CHECK(support_runtime->chat.id().id() == 100);
  CHECK(alice_runtime->chat.Load().get() != support_runtime->chat.Load().get());
  CHECK(alice_runtime->chat->presenter.id().id() == 102);
  CHECK(support_runtime->chat->presenter.id().id() == 102);
  CHECK(alice_runtime->chat->presenter.Load().get() !=
        support_runtime->chat->presenter.Load().get());

  auto alice = alice_runtime->CreateLocalClient("Alice");
  auto support = support_runtime->CreateLocalClient("Support");

  CHECK(alice.is_valid());
  CHECK(alice.is_loaded());
  CHECK(support.is_valid());
  CHECK(support.is_loaded());
  CHECK(alice.id() != support.id());

  auto const alice_id = alice.id();
  auto const alice_base_id = alice->base.id();
  auto const alice_presenter_id = alice->presenter.id();
  auto* alice_address = alice.Load().get();
  auto* alice_chat_address = alice_runtime->chat.Load().get();
  auto* alice_resource_address =
      alice_runtime->client_prefab->resource.Load().get();
  auto* alice_presenter_address = alice->presenter.Load().get();

  auto const support_id = support.id();
  auto const support_base_id = support->base.id();
  auto const support_presenter_id = support->presenter.id();
  auto* support_address = support.Load().get();
  auto* support_chat_address = support_runtime->chat.Load().get();
  auto* support_resource_address =
      support_runtime->client_prefab->resource.Load().get();

  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(alice_runtime->chat->timeline.empty());
  CHECK(alice_runtime->chat->journal.empty());
  CHECK(support_runtime->chat->timeline.empty());
  CHECK(support_runtime->chat->journal.empty());

  alice_runtime->chat->presenter->Join(
      ae::ObjId{200}, ae::TimePoint{std::chrono::microseconds{100}},
      {support_id});

  CHECK(alice_runtime->chat->timeline.size() == 1);
  CHECK(alice_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->journal.size() == 1);
  CHECK(alice_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(alice_runtime->chat->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(alice_runtime->chat->journal[0].identity.origin == alice_id);
  CHECK(alice_runtime->chat->journal[0].identity.sequence == 1);
  auto* alice_join_recipient =
      alice_runtime->chat->journal[0].FindRecipient(support_id);
  CHECK(alice_join_recipient != nullptr);
  CHECK(alice_join_recipient->delivery_status == DeliveryStatus::kPending);
  auto* join_event =
      alice_runtime->chat->journal[0].event.Load().as<ClientJoinedEvent>();
  CHECK(join_event != nullptr);
  CHECK(join_event->client.is_loaded());
  CHECK(join_event->client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->client.is_loaded());
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[0]));

  CHECK(alice->base.is_loaded());
  CHECK(alice->chat.is_valid());
  CHECK(alice->chat.id().id() == 100);
  CHECK(!alice->chat.is_loaded());
  CHECK(alice->presenter.is_valid());
  CHECK(alice->presenter.id() == alice_presenter_id);
  CHECK(!alice->presenter.is_loaded());
  CHECK(alice->resource.is_valid());
  CHECK(alice->resource.id().id() == 50);
  CHECK(!alice->resource.is_loaded());

  auto* alice_base = alice->base.Load().as<Client>();
  CHECK(alice_base != nullptr);
  CHECK(alice_base->chat.is_valid());
  CHECK(alice_base->chat.id().id() == 100);
  CHECK(!alice_base->chat.is_loaded());
  CHECK(alice_base->presenter.is_valid());
  CHECK(alice_base->presenter.id() == alice_presenter_id);
  CHECK(!alice_base->presenter.is_loaded());
  CHECK(alice_base->resource.is_valid());
  CHECK(alice_base->resource.id().id() == 50);
  CHECK(!alice_base->resource.is_loaded());
  CHECK(!alice_base->base.is_valid());
  CHECK(alice_base->journal.empty());
  CHECK(alice_base->name == "Alice");
  CHECK(alice_runtime->chat->presenter->client.is_loaded());

  JournalMessageReceiver receiver;
  LoopbackJournalMessageTransport transport{
      message_domain, transfer_storage, support_domain, support_storage,
      receiver};
  GraphSynchronizer synchronizer{support_id, message_domain, transport};

  synchronizer.Synchronize(alice_runtime);

  CHECK(transport.send_count == 1);
  CHECK(transport.message_ids.size() == 1);
  auto const message_id = transport.message_ids[0];

  CHECK(transfer_storage.state.size() == 4);
  CHECK(ContainsObj(transfer_storage, message_id));
  CHECK(ContainsObj(transfer_storage, 200));
  CHECK(ContainsObj(transfer_storage, alice_id));
  CHECK(ContainsObj(transfer_storage, alice_base_id));

  CHECK(!ContainsObj(transfer_storage, 1));
  CHECK(!ContainsObj(transfer_storage, 10));
  CHECK(!ContainsObj(transfer_storage, 11));
  CHECK(!ContainsObj(transfer_storage, 12));
  CHECK(!ContainsObj(transfer_storage, 50));
  CHECK(!ContainsObj(transfer_storage, 100));
  CHECK(!ContainsObj(transfer_storage, 101));
  CHECK(!ContainsObj(transfer_storage, 102));
  CHECK(!ContainsObj(transfer_storage, alice_presenter_id));
  CHECK(!ContainsObj(transfer_storage, support_id));
  CHECK(!ContainsObj(transfer_storage, support_base_id));
  CHECK(!ContainsObj(transfer_storage, support_presenter_id));

  CHECK(alice_runtime->chat->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(alice_runtime->chat->journal[0].FindRecipient(support_id) != nullptr);
  CHECK(alice_runtime->chat->journal[0]
            .FindRecipient(support_id)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->timeline.size() == 1);
  CHECK(alice_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[0]));
  CHECK(alice->name == "Alice");
  CHECK(alice_base->name == "Alice");
  CHECK(!alice->chat.is_loaded());
  CHECK(!alice->presenter.is_loaded());
  CHECK(!alice->resource.is_loaded());
  CHECK(!alice_base->chat.is_loaded());
  CHECK(!alice_base->presenter.is_loaded());
  CHECK(!alice_base->resource.is_loaded());

  alice->chat.Load();
  alice->resource.Load();
  alice->presenter.Load();
  CHECK(alice->chat.is_loaded());
  CHECK(alice->chat.Load().get() == alice_chat_address);
  CHECK(alice->chat.id().id() == 100);
  CHECK(alice->resource.is_loaded());
  CHECK(alice->resource.Load().get() == alice_resource_address);
  CHECK(alice->resource.id().id() == 50);
  CHECK(alice->presenter.is_loaded());
  CHECK(alice->presenter.Load().get() == alice_presenter_address);
  CHECK(alice->presenter.id() == alice_presenter_id);
  CHECK(alice->presenter->client.Load().get() == alice_address);

  alice_base->chat.Load();
  alice_base->resource.Load();
  alice_base->presenter.Load();
  CHECK(alice_base->chat.Load().get() == alice_chat_address);
  CHECK(alice_base->resource.Load().get() == alice_resource_address);
  CHECK(alice_base->presenter.Load().get() == alice_presenter_address);

  CHECK(support_runtime->chat->timeline.size() == 1);
  CHECK(support_runtime->chat->journal.size() == 1);
  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(support_runtime->chat->journal[0].identity ==
        alice_runtime->chat->journal[0].identity);
  CHECK(support_runtime->chat->journal[0].recipients.empty());
  CHECK(support_runtime->chat->timeline[0].client.id() == alice_id);

  auto* support_join =
      support_runtime->chat->journal[0].event.Load().as<ClientJoinedEvent>();
  CHECK(support_join != nullptr);
  CHECK(support_join->client.id() == alice_id);
  CHECK(support_runtime->chat->timeline[0].client.Load().get() ==
        support_join->client.Load().get());

  auto remote_alice = support_runtime->chat->timeline[0].client;
  CHECK(remote_alice.is_loaded());
  CHECK(remote_alice.Load().get() != alice_address);
  CHECK(remote_alice->name == "Alice");
  CHECK(remote_alice->journal.empty());
  CHECK(remote_alice->base.is_loaded());
  CHECK(remote_alice->base.id() == alice_base_id);
  auto* remote_alice_base = remote_alice->base.Load().as<Client>();
  CHECK(remote_alice_base != nullptr);
  CHECK(remote_alice_base->name == "Alice");
  CHECK(!remote_alice_base->base.is_valid());
  CHECK(remote_alice_base->journal.empty());

  CHECK(remote_alice->chat.is_valid());
  CHECK(remote_alice->chat.id().id() == 100);
  CHECK(!remote_alice->chat.is_loaded());
  CHECK(remote_alice->presenter.is_valid());
  CHECK(remote_alice->presenter.id() == alice_presenter_id);
  CHECK(!remote_alice->presenter.is_loaded());
  CHECK(remote_alice->resource.is_valid());
  CHECK(remote_alice->resource.id().id() == 50);
  CHECK(!remote_alice->resource.is_loaded());

  CHECK(remote_alice_base->chat.is_valid());
  CHECK(remote_alice_base->chat.id().id() == 100);
  CHECK(!remote_alice_base->chat.is_loaded());
  CHECK(remote_alice_base->presenter.is_valid());
  CHECK(remote_alice_base->presenter.id() == alice_presenter_id);
  CHECK(!remote_alice_base->presenter.is_loaded());
  CHECK(remote_alice_base->resource.is_valid());
  CHECK(remote_alice_base->resource.id().id() == 50);
  CHECK(!remote_alice_base->resource.is_loaded());

  CHECK(!ContainsObj(support_storage, alice_presenter_id));

  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support.Load().get() == support_address);
  CHECK(support->name == "Support");
  CHECK(support->journal.empty());
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[0]));
  CHECK(alice_id != support_id);

  GraphJournalScanner scanner;
  CHECK(CountPending(scanner, alice_runtime, support_id) == 0);
  CHECK(CountPending(scanner, support_runtime, support_id) == 0);

  remote_alice->chat.Load();
  remote_alice->resource.Load();
  CHECK(remote_alice->chat.is_loaded());
  CHECK(remote_alice->chat.Load().get() == support_chat_address);
  CHECK(remote_alice->chat.id().id() == 100);
  CHECK(remote_alice->resource.is_loaded());
  CHECK(remote_alice->resource.Load().get() == support_resource_address);
  CHECK(remote_alice->resource.id().id() == 50);
  CHECK(remote_alice->chat.Load().get() != alice_chat_address);
  CHECK(remote_alice->resource.Load().get() != alice_resource_address);
  CHECK(!remote_alice->presenter.is_loaded());

  support_runtime.Save();

  CHECK(ContainsObj(support_storage, alice_id));
  CHECK(ContainsObj(support_storage, alice_base_id));
  CHECK(ContainsObj(support_storage, 200));
  CHECK(!ContainsObj(support_storage, alice_presenter_id));

  ae::Domain reload_domain{ae::Now(), support_storage};
  Runtime::ptr reloaded_runtime =
      Runtime::ptr::Declare(ae::CreateWith{reload_domain}.with_id(1));
  reloaded_runtime.Load();
  CHECK(reloaded_runtime.is_loaded());

  auto loaded_chat = reloaded_runtime->chat;
  CHECK(loaded_chat.is_loaded());
  CHECK(loaded_chat->timeline.size() == 1);
  CHECK(loaded_chat->journal.size() == 1);

  auto loaded_support = loaded_chat->presenter->client;
  CHECK(loaded_support.is_loaded());
  CHECK(loaded_support.id() == support_id);
  CHECK(loaded_support->name == "Support");

  auto loaded_remote_alice = loaded_chat->timeline[0].client;
  CHECK(loaded_remote_alice.is_loaded());
  CHECK(loaded_remote_alice.id() == alice_id);
  CHECK(loaded_remote_alice->name == "Alice");

  auto* loaded_join =
      loaded_chat->journal[0].event.Load().as<ClientJoinedEvent>();
  CHECK(loaded_join != nullptr);
  CHECK(loaded_join->client.id() == alice_id);
  CHECK(loaded_join->client.Load().get() == loaded_remote_alice.Load().get());
  CHECK(loaded_chat->timeline[0].client.Load().get() ==
        loaded_remote_alice.Load().get());
  CHECK(loaded_chat->presenter->client.Load().get() ==
        loaded_support.Load().get());
  CHECK(loaded_remote_alice.Load().get() != loaded_support.Load().get());
  CHECK(loaded_remote_alice.id() != loaded_support.id());
  CHECK(!loaded_chat->presenter->IsLocal(loaded_chat->timeline[0]));

  auto* loaded_remote_base = loaded_remote_alice->base.Load().as<Client>();
  CHECK(loaded_remote_base != nullptr);
  CHECK(loaded_remote_base->name == "Alice");
  CHECK(loaded_remote_alice->journal.empty());
  CHECK(loaded_chat->journal[0].event.id().id() == 200);
  CHECK(loaded_chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(loaded_chat->journal[0].identity ==
        alice_runtime->chat->journal[0].identity);
  CHECK(loaded_chat->journal[0].recipients.empty());

  CHECK(!loaded_remote_alice->chat.is_loaded());
  CHECK(!loaded_remote_alice->resource.is_loaded());
  CHECK(!loaded_remote_alice->presenter.is_loaded());

  loaded_remote_alice->chat.Load();
  loaded_remote_alice->resource.Load();
  CHECK(loaded_remote_alice->chat.is_loaded());
  CHECK(loaded_remote_alice->chat.Load().get() == loaded_chat.Load().get());
  CHECK(loaded_remote_alice->chat.id().id() == 100);
  CHECK(loaded_remote_alice->resource.is_loaded());
  CHECK(loaded_remote_alice->resource.id().id() == 50);
  CHECK(loaded_remote_alice->resource.Load().get() ==
        reloaded_runtime->client_prefab->resource.Load().get());
  CHECK(!loaded_remote_alice->presenter.is_loaded());

  CHECK(loaded_chat->timeline[0].client.Load().get() ==
        loaded_join->client.Load().get());
  CHECK(loaded_chat->timeline[0].client.Load().get() ==
        loaded_remote_alice.Load().get());
  CHECK(loaded_remote_alice.Load().get() !=
        loaded_chat->presenter->client.Load().get());

  return EXIT_SUCCESS;
}
