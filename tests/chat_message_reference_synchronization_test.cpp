#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/event_for.h"
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
class SendMessageEvent;
class RenameClientEvent;

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

enum class ChatEntryKind : std::uint8_t {
  kClientJoined,
  kMessage,
};

struct ChatEntry {
  ChatEntryKind kind{ChatEntryKind::kClientJoined};
  ae::ObjPtr<Client> client;
  std::string text;

  AE_REFLECT_MEMBERS(kind, client, text)
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
  void Apply(SendMessageEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitFromPresenter(apptraverse::Event::ptr event, ae::TimePoint time) {
    CommitEvent(std::move(event), time);
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

  void Rename(std::string name, ae::ObjId event_id, ae::TimePoint time);
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

  void Apply(RenameClientEvent const& event);

  void CapturePrefabBaseForTest() { CaptureBaseState(); }

  void CommitFromPresenter(apptraverse::Event::ptr event, ae::TimePoint time) {
    CommitEvent(std::move(event), time);
  }

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

class SendMessageEvent : public apptraverse::EventFor<Chat, SendMessageEvent> {
  AE_OBJECT(SendMessageEvent, Event, 0)

 protected:
  SendMessageEvent() = default;

 public:
  explicit SendMessageEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(sender), AE_MMBR(text))

  ae::ObjPtr<Client> sender;
  std::string text;
};

class RenameClientEvent
    : public apptraverse::EventFor<Client, RenameClientEvent> {
  AE_OBJECT(RenameClientEvent, Event, 0)

 protected:
  RenameClientEvent() = default;

 public:
  explicit RenameClientEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name))

  std::string name;
};

void Chat::Apply(ClientJoinedEvent const& event) {
  timeline.push_back(ChatEntry{
      ChatEntryKind::kClientJoined,
      event.client,
      {},
  });
}

void Chat::Apply(SendMessageEvent const& event) {
  timeline.push_back(ChatEntry{
      ChatEntryKind::kMessage,
      event.sender,
      event.text,
  });
}

void Client::Apply(RenameClientEvent const& event) { name = event.name; }

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

  void Join(ae::ObjId event_id, ae::TimePoint time);
  void Send(std::string text, ae::ObjId event_id, ae::TimePoint time);

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

void ChatPresenter::Join(ae::ObjId event_id, ae::TimePoint time) {
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

  chat->CommitFromPresenter(event, time);
}

void ChatPresenter::Send(std::string text, ae::ObjId event_id,
                         ae::TimePoint time) {
  assert(domain != nullptr);
  assert(chat.is_valid());
  assert(chat.is_loaded());
  assert(client.is_valid());
  assert(client.is_loaded());
  assert(chat.domain() == domain);
  assert(client.domain() == domain);
  assert(event_id.IsValid());

  SendMessageEvent::ptr event =
      SendMessageEvent::ptr::Create(ae::CreateWith{*domain}.with_id(event_id));
  event->sender = client;
  event->sender.Reset();
  event->sender.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  event->text = std::move(text);

  assert(event->sender.is_valid());
  assert(event->sender.id() == client.id());
  assert(!event->sender.is_loaded());
  assert(client.is_loaded());

  chat->CommitFromPresenter(event, time);
}

void ClientPresenter::Rename(std::string name, ae::ObjId event_id,
                             ae::TimePoint time) {
  assert(domain != nullptr);
  assert(client.is_valid());
  assert(client.is_loaded());
  assert(client.domain() == domain);
  assert(event_id.IsValid());

  auto event =
      RenameClientEvent::ptr::Create(ae::CreateWith{*domain}.with_id(event_id));
  event->name = std::move(name);
  client->CommitFromPresenter(event, time);
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
  std::vector<std::pair<ae::ObjId::Type, ae::ObjId::Type>> target_event_pairs;

  void Send(apptraverse::JournalTransportMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    assert(message.domain() == message_domain_);

    auto* event_message =
        message.Load().as<apptraverse::JournalEventMessage>();
    assert(event_message != nullptr);
    assert(event_message->target.is_valid());
    assert(!event_message->target.is_loaded());
    assert(event_message->event.is_valid());
    assert(event_message->event.is_loaded());

    target_event_pairs.emplace_back(event_message->target.id().id(),
                                    event_message->event.id().id());
    message_ids.push_back(message.id());
    message.Save();

    receiver_storage_->state.insert(transfer_storage_->state.begin(),
                                    transfer_storage_->state.end());

    apptraverse::JournalTransportMessage::ptr incoming =
        apptraverse::JournalTransportMessage::ptr::Declare(
            ae::CreateWith{*receiver_domain_}.with_id(message.id()));
    incoming.Load();
    receiver_->Receive(std::move(incoming));

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

int CountPending(apptraverse::GraphJournalScanner const& scanner, auto& root) {
  int count = 0;
  scanner.VisitPending(root, [&](apptraverse::Node&,
                                 apptraverse::EventRecord&) { ++count; });
  return count;
}

using PendingKey = std::pair<ae::ObjId::Type, ae::ObjId::Type>;

std::set<PendingKey> CollectPending(apptraverse::GraphJournalScanner const& scanner,
                                    auto& root) {
  std::set<PendingKey> found;
  scanner.VisitPending(root, [&](apptraverse::Node& node,
                                 apptraverse::EventRecord& record) {
    found.emplace(node.obj_id.id(), record.event.id().id());
  });
  return found;
}

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::GraphJournalScanner;
  using apptraverse::GraphSynchronizer;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::BuildInitialRuntime;
  using apptraverse::test::ChatEntryKind;
  using apptraverse::test::Client;
  using apptraverse::test::ClientJoinedEvent;
  using apptraverse::test::ClientPresenter;
  using apptraverse::test::LoopbackJournalMessageTransport;
  using apptraverse::test::RenameClientEvent;
  using apptraverse::test::Runtime;
  using apptraverse::test::SendMessageEvent;

  ae::RamDomainStorage alice_storage;
  ae::RamDomainStorage support_storage;
  ae::RamDomainStorage introduction_transfer_storage;
  ae::RamDomainStorage message_transfer_storage;
  ae::RamDomainStorage rename_transfer_storage;
  ae::RamDomainStorage support_introduction_transfer_storage;
  ae::RamDomainStorage support_message_transfer_storage;

  ae::Domain alice_domain{ae::Now(), alice_storage};
  ae::Domain support_domain{ae::Now(), support_storage};
  ae::Domain introduction_message_domain{ae::Now(),
                                         introduction_transfer_storage};
  ae::Domain message_message_domain{ae::Now(), message_transfer_storage};
  ae::Domain rename_message_domain{ae::Now(), rename_transfer_storage};
  ae::Domain support_introduction_message_domain{
      ae::Now(), support_introduction_transfer_storage};
  ae::Domain support_message_message_domain{ae::Now(),
                                            support_message_transfer_storage};

  auto alice_runtime = BuildInitialRuntime(alice_domain);
  auto support_runtime = BuildInitialRuntime(support_domain);

  CHECK(alice_storage.state.size() == 8);
  CHECK(support_storage.state.size() == 8);

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
  auto* alice_base_address = alice->base.Load().get();
  auto* alice_runtime_address = alice_runtime.Load().get();
  auto* alice_chat_address = alice_runtime->chat.Load().get();
  auto* alice_resource_address =
      alice_runtime->client_prefab->resource.Load().get();

  auto const support_id = support.id();
  auto const support_base_id = support->base.id();
  auto const support_presenter_id = support->presenter.id();
  auto* support_address = support.Load().get();
  auto* support_base_address = support->base.Load().get();
  auto* support_runtime_address = support_runtime.Load().get();
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
      ae::ObjId{200}, ae::TimePoint{std::chrono::microseconds{100}});

  JournalMessageReceiver receiver;
  LoopbackJournalMessageTransport introduction_transport{
      introduction_message_domain, introduction_transfer_storage,
      support_domain, support_storage, receiver};
  GraphSynchronizer introduction_synchronizer{introduction_message_domain,
                                              introduction_transport};
  introduction_synchronizer.Synchronize(alice_runtime);

  CHECK(introduction_transport.send_count == 1);
  CHECK(introduction_transfer_storage.state.size() == 4);
  CHECK(ContainsObj(introduction_transfer_storage,
                     introduction_transport.message_ids[0]));
  CHECK(ContainsObj(introduction_transfer_storage, 200));
  CHECK(ContainsObj(introduction_transfer_storage, alice_id));
  CHECK(ContainsObj(introduction_transfer_storage, alice_base_id));
  CHECK(!ContainsObj(introduction_transfer_storage, alice_presenter_id));
  CHECK(!ContainsObj(introduction_transfer_storage, 100));
  CHECK(!ContainsObj(introduction_transfer_storage, 50));

  CHECK(support_runtime->chat->timeline.size() == 1);
  CHECK(support_runtime->chat->journal.size() == 1);
  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);

  auto remote_alice = support_runtime->chat->timeline[0].client;
  CHECK(remote_alice.is_loaded());
  CHECK(remote_alice.id() == alice_id);
  CHECK(remote_alice.Load().get() != alice_address);
  auto* remote_alice_address = remote_alice.Load().get();
  CHECK(!remote_alice->presenter.is_loaded());

  GraphJournalScanner scanner;
  CHECK(CountPending(scanner, alice_runtime) == 0);
  CHECK(CountPending(scanner, support_runtime) == 0);

  CHECK(alice_runtime->chat->presenter->client.is_loaded());
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice.is_loaded());
  CHECK(alice->journal.empty());

  bool const alice_chat_loaded_before_send = alice->chat.is_loaded();
  bool const alice_presenter_loaded_before_send = alice->presenter.is_loaded();
  bool const alice_resource_loaded_before_send = alice->resource.is_loaded();
  auto const alice_chat_id_before_send = alice->chat.id();
  auto const alice_presenter_id_before_send = alice->presenter.id();
  auto const alice_resource_id_before_send = alice->resource.id();

  CHECK(remote_alice.is_loaded());
  CHECK(support.is_loaded());
  CHECK(remote_alice.Load().get() != support.Load().get());
  CHECK(support_runtime->chat->timeline.size() == 1);
  CHECK(support_runtime->chat->journal.size() == 1);
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[0]));
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[0]));

  alice_runtime->chat->presenter->Send(
      "Hello from Alice", ae::ObjId{201},
      ae::TimePoint{std::chrono::microseconds{200}});

  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->timeline[0].kind == ChatEntryKind::kClientJoined);
  CHECK(alice_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(alice_runtime->chat->timeline[1].client.is_valid());
  CHECK(alice_runtime->chat->timeline[1].client.id() == alice_id);
  CHECK(!alice_runtime->chat->timeline[1].client.is_loaded());
  CHECK(alice_runtime->chat->timeline[1].text == "Hello from Alice");

  CHECK(alice_runtime->chat->journal.size() == 2);
  CHECK(alice_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(alice_runtime->chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(alice_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(alice_runtime->chat->journal[1].time ==
        ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(alice_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kPending);

  auto* sender_event =
      alice_runtime->chat->journal[1].event.Load().as<SendMessageEvent>();
  CHECK(sender_event != nullptr);
  CHECK(sender_event->sender.is_valid());
  CHECK(sender_event->sender.id() == alice_id);
  CHECK(!sender_event->sender.is_loaded());
  CHECK(sender_event->text == "Hello from Alice");

  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[1]));
  CHECK(alice_runtime->chat->presenter->client.is_loaded());
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice.id() == alice_id);
  CHECK(alice->base.id() == alice_base_id);
  CHECK(alice->presenter.id() == alice_presenter_id);
  CHECK(alice->chat.is_loaded() == alice_chat_loaded_before_send);
  CHECK(alice->presenter.is_loaded() == alice_presenter_loaded_before_send);
  CHECK(alice->resource.is_loaded() == alice_resource_loaded_before_send);
  CHECK(alice->chat.id() == alice_chat_id_before_send);
  CHECK(alice->presenter.id() == alice_presenter_id_before_send);
  CHECK(alice->resource.id() == alice_resource_id_before_send);
  CHECK(alice->base.Load().get() == alice_base_address);
  auto* alice_base = alice->base.Load().as<Client>();
  CHECK(alice_base != nullptr);
  CHECK(alice_base->name == "Alice");
  CHECK(alice->journal.empty());

  CHECK(CountPending(scanner, alice_runtime) == 1);
  CHECK(CountPending(scanner, support_runtime) == 0);

  LoopbackJournalMessageTransport message_transport{
      message_message_domain, message_transfer_storage, support_domain,
      support_storage, receiver};
  GraphSynchronizer message_synchronizer{message_message_domain,
                                         message_transport};
  message_synchronizer.Synchronize(alice_runtime);

  CHECK(message_transport.send_count == 1);
  CHECK(message_transport.message_ids.size() == 1);
  auto const message_id = message_transport.message_ids[0];
  CHECK(message_transfer_storage.state.size() == 2);
  CHECK(ContainsObj(message_transfer_storage, message_id));
  CHECK(ContainsObj(message_transfer_storage, 201));

  CHECK(!ContainsObj(message_transfer_storage, 1));
  CHECK(!ContainsObj(message_transfer_storage, 10));
  CHECK(!ContainsObj(message_transfer_storage, 11));
  CHECK(!ContainsObj(message_transfer_storage, 12));
  CHECK(!ContainsObj(message_transfer_storage, 50));
  CHECK(!ContainsObj(message_transfer_storage, 100));
  CHECK(!ContainsObj(message_transfer_storage, 101));
  CHECK(!ContainsObj(message_transfer_storage, 102));
  CHECK(!ContainsObj(message_transfer_storage, alice_id));
  CHECK(!ContainsObj(message_transfer_storage, alice_base_id));
  CHECK(!ContainsObj(message_transfer_storage, alice_presenter_id));
  CHECK(!ContainsObj(message_transfer_storage, support_id));
  CHECK(!ContainsObj(message_transfer_storage, support_base_id));
  CHECK(!ContainsObj(message_transfer_storage, support_presenter_id));
  CHECK(!ContainsObj(message_transfer_storage, 200));

  CHECK(alice_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->timeline[1].text == "Hello from Alice");
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[1]));
  CHECK(alice->name == "Alice");
  CHECK(alice_base->name == "Alice");
  CHECK(alice->chat.is_loaded() == alice_chat_loaded_before_send);
  CHECK(alice->presenter.is_loaded() == alice_presenter_loaded_before_send);
  CHECK(alice->resource.is_loaded() == alice_resource_loaded_before_send);
  CHECK(alice->journal.empty());
  CHECK(CountPending(scanner, alice_runtime) == 0);

  sender_event->sender.Load();
  alice_runtime->chat->timeline[1].client.Load();
  CHECK(sender_event->sender.is_loaded());
  CHECK(alice_runtime->chat->timeline[1].client.is_loaded());
  CHECK(sender_event->sender.Load().get() == alice_address);
  CHECK(alice_runtime->chat->timeline[1].client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);

  CHECK(remote_alice.Load().get() == remote_alice_address);
  CHECK(support.Load().get() == support_address);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support_runtime->chat->timeline.size() == 2);
  CHECK(support_runtime->chat->journal.size() == 2);

  CHECK(support_runtime->chat->timeline[0].kind ==
        ChatEntryKind::kClientJoined);
  CHECK(support_runtime->chat->timeline[0].client.Load().get() ==
        remote_alice_address);
  CHECK(support_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(support_runtime->chat->timeline[1].client.is_valid());
  CHECK(support_runtime->chat->timeline[1].client.id() == alice_id);
  CHECK(!support_runtime->chat->timeline[1].client.is_loaded());
  CHECK(support_runtime->chat->timeline[1].text == "Hello from Alice");

  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(support_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(support_runtime->chat->journal[1].time ==
        ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(support_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);

  auto* receiver_message_event =
      support_runtime->chat->journal[1].event.Load().as<SendMessageEvent>();
  CHECK(receiver_message_event != nullptr);
  CHECK(receiver_message_event->sender.is_valid());
  CHECK(receiver_message_event->sender.id() == alice_id);
  CHECK(!receiver_message_event->sender.is_loaded());
  CHECK(receiver_message_event->text == "Hello from Alice");

  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[1]));
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support_id != alice_id);
  CHECK(support->name == "Support");
  CHECK(support->journal.empty());
  CHECK(remote_alice->name == "Alice");
  CHECK(remote_alice->journal.empty());
  CHECK(!remote_alice->presenter.is_loaded());
  CHECK(!ContainsObj(support_storage, alice_presenter_id));

  receiver_message_event->sender.Load();
  support_runtime->chat->timeline[1].client.Load();
  CHECK(receiver_message_event->sender.is_loaded());
  CHECK(support_runtime->chat->timeline[1].client.is_loaded());
  CHECK(receiver_message_event->sender.Load().get() == remote_alice_address);
  CHECK(support_runtime->chat->timeline[1].client.Load().get() ==
        remote_alice_address);
  CHECK(receiver_message_event->sender.Load().get() != support_address);
  CHECK(support_runtime->chat->timeline[1].client.Load().get() ==
        receiver_message_event->sender.Load().get());

  auto* support_join =
      support_runtime->chat->journal[0].event.Load().as<ClientJoinedEvent>();
  CHECK(support_join != nullptr);
  CHECK(support_runtime->chat->timeline[0].client.Load().get() ==
        remote_alice_address);
  CHECK(support_join->client.Load().get() == remote_alice_address);
  CHECK(support_runtime->chat->timeline[1].client.Load().get() ==
        remote_alice_address);
  CHECK(receiver_message_event->sender.Load().get() == remote_alice_address);

  CHECK(CountPending(scanner, alice_runtime) == 0);
  CHECK(CountPending(scanner, support_runtime) == 0);

  auto const send_count_before_repeat = message_transport.send_count;
  auto const message_ids_before_repeat = message_transport.message_ids.size();
  auto const transfer_size_before_repeat = message_transfer_storage.state.size();
  auto const support_timeline_before_repeat =
      support_runtime->chat->timeline.size();
  auto const support_journal_before_repeat =
      support_runtime->chat->journal.size();

  message_synchronizer.Synchronize(alice_runtime);

  CHECK(message_transport.send_count == send_count_before_repeat);
  CHECK(message_transport.message_ids.size() == message_ids_before_repeat);
  CHECK(message_transfer_storage.state.size() == transfer_size_before_repeat);
  CHECK(support_runtime->chat->timeline.size() ==
        support_timeline_before_repeat);
  CHECK(support_runtime->chat->journal.size() == support_journal_before_repeat);
  CHECK(support_runtime->chat->timeline[1].text == "Hello from Alice");

  CHECK(alice.is_loaded());
  CHECK(alice.id() == alice_id);
  CHECK(alice->name == "Alice");
  CHECK(alice->journal.empty());
  CHECK(alice_base->name == "Alice");
  CHECK(remote_alice.Load().get() == remote_alice_address);
  CHECK(remote_alice->name == "Alice");
  CHECK(remote_alice->journal.empty());
  CHECK(alice_runtime->chat->journal.size() == 2);
  CHECK(support_runtime->chat->journal.size() == 2);
  CHECK(alice_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(alice_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(alice_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(CountPending(scanner, alice_runtime) == 0);
  CHECK(CountPending(scanner, support_runtime) == 0);
  CHECK(alice->presenter.is_valid());
  CHECK(alice->presenter.id() == alice_presenter_id);
  CHECK(!alice->presenter.is_loaded());
  CHECK(ContainsObj(alice_storage, alice_presenter_id));
  CHECK(!ContainsObj(support_storage, alice_presenter_id));

  alice->presenter.Load();
  auto* alice_client_presenter =
      alice->presenter.Load().as<ClientPresenter>();
  CHECK(alice_client_presenter != nullptr);
  CHECK(alice->presenter.is_loaded());
  CHECK(alice->presenter.id() == alice_presenter_id);
  auto* alice_presenter_address = alice->presenter.Load().get();
  CHECK(alice_presenter_address != nullptr);
  CHECK(alice_presenter_address->domain == &alice_domain);
  CHECK(alice_client_presenter->client.Load().get() == alice_address);
  CHECK(alice_client_presenter->client.id() == alice_id);
  CHECK(alice_client_presenter->client.Load().get() !=
        alice_runtime->client_prefab.Load().get());
  CHECK(!ContainsObj(support_storage, alice_presenter_id));
  CHECK(rename_transfer_storage.state.empty());

  alice->presenter->Rename("Alice Cooper", ae::ObjId{202},
                           ae::TimePoint{std::chrono::microseconds{100}});

  CHECK(alice->name == "Alice Cooper");
  CHECK(alice.id() == alice_id);
  CHECK(alice->base.id() == alice_base_id);
  CHECK(alice->presenter.id() == alice_presenter_id);
  CHECK(alice->journal.size() == 1);
  CHECK(alice->journal[0].event.id().id() == 202);
  CHECK(alice->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(alice->journal[0].delivery_status == DeliveryStatus::kPending);
  auto* sender_rename =
      alice->journal[0].event.Load().as<RenameClientEvent>();
  CHECK(sender_rename != nullptr);
  CHECK(sender_rename->name == "Alice Cooper");
  CHECK(alice_base->name == "Alice");
  CHECK(!alice_base->base.is_valid());
  CHECK(alice_base->journal.empty());

  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->journal.size() == 2);
  CHECK(alice_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(alice_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(alice_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->timeline[1].client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->timeline[0].client->name == "Alice Cooper");
  CHECK(alice_runtime->chat->timeline[1].client->name == "Alice Cooper");
  auto* alice_join =
      alice_runtime->chat->journal[0].event.Load().as<ClientJoinedEvent>();
  CHECK(alice_join != nullptr);
  CHECK(alice_join->client.Load().get() == alice_address);
  CHECK(sender_event->sender.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[1]));

  CHECK(remote_alice->name == "Alice");
  CHECK(remote_alice->journal.empty());
  CHECK(support_runtime->chat->timeline[0].client->name == "Alice");
  CHECK(support_runtime->chat->timeline[1].client->name == "Alice");
  CHECK(support.Load().get() == support_address);
  CHECK(support->name == "Support");

  auto alice_pending = CollectPending(scanner, alice_runtime);
  CHECK(alice_pending.size() == 1);
  CHECK(alice_pending.count(PendingKey{alice_id.id(), 202}) == 1);
  CHECK(CountPending(scanner, alice_runtime) == 1);
  CHECK(CountPending(scanner, support_runtime) == 0);

  LoopbackJournalMessageTransport rename_transport{
      rename_message_domain, rename_transfer_storage, support_domain,
      support_storage, receiver};
  GraphSynchronizer rename_synchronizer{rename_message_domain, rename_transport};
  rename_synchronizer.Synchronize(alice_runtime);

  CHECK(rename_transport.send_count == 1);
  CHECK(rename_transport.message_ids.size() == 1);
  CHECK(rename_transport.target_event_pairs.size() == 1);
  CHECK((rename_transport.target_event_pairs[0] ==
         PendingKey{alice_id.id(), 202}));

  CHECK(rename_transfer_storage.state.size() == 2);
  CHECK(ContainsObj(rename_transfer_storage, rename_transport.message_ids[0]));
  CHECK(ContainsObj(rename_transfer_storage, 202));
  CHECK(!ContainsObj(rename_transfer_storage, 1));
  CHECK(!ContainsObj(rename_transfer_storage, 10));
  CHECK(!ContainsObj(rename_transfer_storage, 11));
  CHECK(!ContainsObj(rename_transfer_storage, 12));
  CHECK(!ContainsObj(rename_transfer_storage, 50));
  CHECK(!ContainsObj(rename_transfer_storage, 100));
  CHECK(!ContainsObj(rename_transfer_storage, 101));
  CHECK(!ContainsObj(rename_transfer_storage, 102));
  CHECK(!ContainsObj(rename_transfer_storage, alice_id));
  CHECK(!ContainsObj(rename_transfer_storage, alice_base_id));
  CHECK(!ContainsObj(rename_transfer_storage, alice_presenter_id));
  CHECK(!ContainsObj(rename_transfer_storage, support_id));
  CHECK(!ContainsObj(rename_transfer_storage, support_base_id));
  CHECK(!ContainsObj(rename_transfer_storage, support_presenter_id));
  CHECK(!ContainsObj(rename_transfer_storage, 200));
  CHECK(!ContainsObj(rename_transfer_storage, 201));

  CHECK(alice->name == "Alice Cooper");
  CHECK(alice->journal.size() == 1);
  CHECK(alice->journal[0].event.id().id() == 202);
  CHECK(alice->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(alice->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(sender_rename->name == "Alice Cooper");
  CHECK(alice_base->name == "Alice");
  CHECK(alice->presenter.Load().get() == alice_presenter_address);
  CHECK(alice->presenter->client.Load().get() == alice_address);
  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->journal.size() == 2);
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[1]));
  CHECK(CountPending(scanner, alice_runtime) == 0);

  CHECK(remote_alice.Load().get() == remote_alice_address);
  CHECK(remote_alice->name == "Alice Cooper");
  CHECK(remote_alice.id() == alice_id);
  CHECK(remote_alice->base.id() == alice_base_id);
  auto* remote_alice_base = remote_alice->base.Load().as<Client>();
  CHECK(remote_alice_base != nullptr);
  CHECK(remote_alice_base->name == "Alice");
  CHECK(remote_alice->journal.size() == 1);
  CHECK(remote_alice->journal[0].event.id().id() == 202);
  CHECK(remote_alice->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(remote_alice->journal[0].delivery_status == DeliveryStatus::kDelivered);
  auto* receiver_rename =
      remote_alice->journal[0].event.Load().as<RenameClientEvent>();
  CHECK(receiver_rename != nullptr);
  CHECK(receiver_rename->name == "Alice Cooper");
  CHECK(receiver_rename != sender_rename);

  CHECK(support_runtime->chat->timeline.size() == 2);
  CHECK(support_runtime->chat->journal.size() == 2);
  CHECK(support_runtime->chat->timeline[0].client.Load().get() ==
        remote_alice_address);
  CHECK(support_runtime->chat->timeline[1].client.Load().get() ==
        remote_alice_address);
  CHECK(support_runtime->chat->timeline[0].client->name == "Alice Cooper");
  CHECK(support_runtime->chat->timeline[1].client->name == "Alice Cooper");
  CHECK(support_join->client.Load().get() == remote_alice_address);
  CHECK(receiver_message_event->sender.Load().get() == remote_alice_address);
  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[1].event.id().id() == 201);

  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support.Load().get() == support_address);
  CHECK(support.id() == support_id);
  CHECK(support->name == "Support");
  CHECK(support->journal.empty());
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[1]));
  CHECK(remote_alice.Load().get() != support_address);
  CHECK(!ContainsObj(support_storage, alice_presenter_id));
  CHECK(!remote_alice->presenter.is_loaded());

  CHECK(CountPending(scanner, alice_runtime) == 0);
  CHECK(CountPending(scanner, support_runtime) == 0);

  auto const rename_send_count_before_repeat = rename_transport.send_count;
  auto const rename_message_ids_before_repeat =
      rename_transport.message_ids.size();
  auto const rename_transfer_size_before_repeat =
      rename_transfer_storage.state.size();
  auto const alice_journal_before_repeat = alice->journal.size();
  auto const remote_journal_before_repeat = remote_alice->journal.size();
  auto const alice_name_before_repeat = alice->name;
  auto const remote_name_before_repeat = remote_alice->name;
  auto const chat_timeline_before_repeat =
      support_runtime->chat->timeline.size();

  rename_synchronizer.Synchronize(alice_runtime);

  CHECK(rename_transport.send_count == rename_send_count_before_repeat);
  CHECK(rename_transport.message_ids.size() ==
        rename_message_ids_before_repeat);
  CHECK(rename_transfer_storage.state.size() ==
        rename_transfer_size_before_repeat);
  CHECK(alice->journal.size() == alice_journal_before_repeat);
  CHECK(remote_alice->journal.size() == remote_journal_before_repeat);
  CHECK(alice->name == alice_name_before_repeat);
  CHECK(remote_alice->name == remote_name_before_repeat);
  CHECK(support_runtime->chat->timeline.size() == chat_timeline_before_repeat);

  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->journal.size() == 2);
  CHECK(alice_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(alice_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(alice_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice->name == "Alice Cooper");
  CHECK(alice->journal.size() == 1);
  CHECK(alice->journal[0].event.id().id() == 202);
  CHECK(alice->journal[0].delivery_status == DeliveryStatus::kDelivered);

  CHECK(support_runtime->chat->timeline.size() == 2);
  CHECK(support_runtime->chat->journal.size() == 2);
  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(support_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support->name == "Support");
  CHECK(support->journal.empty());
  CHECK(remote_alice->name == "Alice Cooper");
  CHECK(remote_alice->journal.size() == 1);
  CHECK(remote_alice->journal[0].event.id().id() == 202);
  CHECK(remote_alice->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(CountPending(scanner, alice_runtime) == 0);
  CHECK(CountPending(scanner, support_runtime) == 0);

  CHECK(support.is_loaded());
  CHECK(support->base.is_loaded());
  CHECK(support->chat.is_valid());
  CHECK(support->chat.is_loaded());
  CHECK(support->presenter.is_valid());
  CHECK(support->presenter.is_loaded());
  CHECK(support->resource.is_valid());
  CHECK(support->resource.is_loaded());
  CHECK(support_runtime->chat->presenter->client.is_loaded());
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  auto* support_presenter_address = support->presenter.Load().get();
  CHECK(support_presenter_address != nullptr);

  support_runtime->chat->presenter->Join(
      ae::ObjId{203}, ae::TimePoint{std::chrono::microseconds{300}});

  CHECK(support_runtime->chat->timeline.size() == 3);
  CHECK(support_runtime->chat->timeline[0].kind ==
        ChatEntryKind::kClientJoined);
  CHECK(support_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(support_runtime->chat->timeline[2].kind ==
        ChatEntryKind::kClientJoined);
  CHECK(support_runtime->chat->timeline[2].client.id() == support_id);
  CHECK(support_runtime->chat->timeline[2].client.is_loaded());
  CHECK(support_runtime->chat->timeline[2].client.Load().get() ==
        support_address);
  CHECK(support_runtime->chat->timeline[2].text.empty());
  CHECK(support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[2]));

  CHECK(support_runtime->chat->journal.size() == 3);
  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(support_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(support_runtime->chat->journal[1].time ==
        ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(support_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[2].event.id().id() == 203);
  CHECK(support_runtime->chat->journal[2].time ==
        ae::TimePoint{std::chrono::microseconds{300}});
  CHECK(support_runtime->chat->journal[2].delivery_status ==
        DeliveryStatus::kPending);
  auto* support_join_event =
      support_runtime->chat->journal[2].event.Load().as<ClientJoinedEvent>();
  CHECK(support_join_event != nullptr);
  CHECK(support_join_event->client.is_loaded());
  CHECK(support_join_event->client.id() == support_id);
  CHECK(support_join_event->client.Load().get() == support_address);

  CHECK(support->base.is_loaded());
  CHECK(support->chat.is_valid());
  CHECK(support->chat.id().id() == 100);
  CHECK(!support->chat.is_loaded());
  CHECK(support->presenter.is_valid());
  CHECK(support->presenter.id() == support_presenter_id);
  CHECK(!support->presenter.is_loaded());
  CHECK(support->resource.is_valid());
  CHECK(support->resource.id().id() == 50);
  CHECK(!support->resource.is_loaded());
  CHECK(support->journal.empty());
  CHECK(support->name == "Support");

  auto* support_base = support->base.Load().as<Client>();
  CHECK(support_base != nullptr);
  CHECK(support_base->name == "Support");
  CHECK(!support_base->base.is_valid());
  CHECK(support_base->journal.empty());
  CHECK(support_base->chat.is_valid());
  CHECK(support_base->chat.id().id() == 100);
  CHECK(!support_base->chat.is_loaded());
  CHECK(support_base->presenter.is_valid());
  CHECK(support_base->presenter.id() == support_presenter_id);
  CHECK(!support_base->presenter.is_loaded());
  CHECK(support_base->resource.is_valid());
  CHECK(support_base->resource.id().id() == 50);
  CHECK(!support_base->resource.is_loaded());
  CHECK(support_runtime->chat->presenter->client.is_loaded());
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);

  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->journal.size() == 2);
  auto support_pending = CollectPending(scanner, support_runtime);
  CHECK(support_pending.size() == 1);
  CHECK(support_pending.count(PendingKey{100, 203}) == 1);
  CHECK(CountPending(scanner, support_runtime) == 1);
  CHECK(CountPending(scanner, alice_runtime) == 0);

  LoopbackJournalMessageTransport support_introduction_transport{
      support_introduction_message_domain, support_introduction_transfer_storage,
      alice_domain, alice_storage, receiver};
  GraphSynchronizer support_introduction_synchronizer{
      support_introduction_message_domain, support_introduction_transport};
  support_introduction_synchronizer.Synchronize(support_runtime);

  CHECK(support_introduction_transport.send_count == 1);
  CHECK(support_introduction_transport.message_ids.size() == 1);
  CHECK(support_introduction_transport.target_event_pairs.size() == 1);
  CHECK((support_introduction_transport.target_event_pairs[0] ==
         PendingKey{100, 203}));

  CHECK(support_introduction_transfer_storage.state.size() == 4);
  CHECK(ContainsObj(support_introduction_transfer_storage,
                     support_introduction_transport.message_ids[0]));
  CHECK(ContainsObj(support_introduction_transfer_storage, 203));
  CHECK(ContainsObj(support_introduction_transfer_storage, support_id));
  CHECK(ContainsObj(support_introduction_transfer_storage, support_base_id));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 1));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 10));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 11));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 12));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 50));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 100));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 101));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 102));
  CHECK(!ContainsObj(support_introduction_transfer_storage,
                     support_presenter_id));
  CHECK(!ContainsObj(support_introduction_transfer_storage, alice_id));
  CHECK(!ContainsObj(support_introduction_transfer_storage, alice_base_id));
  CHECK(!ContainsObj(support_introduction_transfer_storage,
                     alice_presenter_id));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 200));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 201));
  CHECK(!ContainsObj(support_introduction_transfer_storage, 202));

  CHECK(support_runtime->chat->journal[2].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->timeline.size() == 3);
  CHECK(support.Load().get() == support_address);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[2]));
  CHECK(support->base.Load().get() == support_base_address);
  CHECK(CountPending(scanner, support_runtime) == 0);

  CHECK(alice_runtime->chat->timeline.size() == 3);
  CHECK(alice_runtime->chat->journal.size() == 3);
  CHECK(alice_runtime->chat->journal[2].event.id().id() == 203);
  CHECK(alice_runtime->chat->journal[2].time ==
        ae::TimePoint{std::chrono::microseconds{300}});
  CHECK(alice_runtime->chat->journal[2].delivery_status ==
        DeliveryStatus::kDelivered);
  auto* alice_support_join =
      alice_runtime->chat->journal[2].event.Load().as<ClientJoinedEvent>();
  CHECK(alice_support_join != nullptr);

  auto remote_support = alice_runtime->chat->timeline[2].client;
  CHECK(remote_support.is_valid());
  CHECK(remote_support.is_loaded());
  CHECK(remote_support.id() == support_id);
  CHECK(remote_support.Load().get() != support_address);
  auto* remote_support_address = remote_support.Load().get();
  CHECK(remote_support->name == "Support");
  CHECK(remote_support->journal.empty());
  CHECK(remote_support->base.id() == support_base_id);
  CHECK(remote_support->base.is_loaded());
  auto* remote_support_base = remote_support->base.Load().as<Client>();
  CHECK(remote_support_base != nullptr);
  CHECK(remote_support_base->name == "Support");
  CHECK(!remote_support_base->base.is_valid());
  CHECK(remote_support_base->journal.empty());
  CHECK(alice_runtime->chat->timeline[2].client.Load().get() ==
        remote_support_address);
  CHECK(alice_support_join->client.Load().get() == remote_support_address);

  CHECK(remote_support->chat.is_valid());
  CHECK(remote_support->chat.id().id() == 100);
  CHECK(!remote_support->chat.is_loaded());
  CHECK(remote_support->presenter.is_valid());
  CHECK(remote_support->presenter.id() == support_presenter_id);
  CHECK(!remote_support->presenter.is_loaded());
  CHECK(remote_support->resource.is_valid());
  CHECK(remote_support->resource.id().id() == 50);
  CHECK(!remote_support->resource.is_loaded());
  CHECK(remote_support_base->chat.is_valid());
  CHECK(remote_support_base->chat.id().id() == 100);
  CHECK(!remote_support_base->chat.is_loaded());
  CHECK(remote_support_base->presenter.is_valid());
  CHECK(remote_support_base->presenter.id() == support_presenter_id);
  CHECK(!remote_support_base->presenter.is_loaded());
  CHECK(remote_support_base->resource.is_valid());
  CHECK(remote_support_base->resource.id().id() == 50);
  CHECK(!remote_support_base->resource.is_loaded());
  CHECK(!ContainsObj(alice_storage, support_presenter_id));
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice->name == "Alice Cooper");
  CHECK(alice->journal[0].event.id().id() == 202);
  CHECK(alice->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(!alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[2]));
  CHECK(support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[2]));

  remote_support->chat.Load();
  remote_support->resource.Load();
  CHECK(remote_support->chat.is_loaded());
  CHECK(remote_support->chat.Load().get() == alice_chat_address);
  CHECK(remote_support->resource.is_loaded());
  CHECK(remote_support->resource.Load().get() == alice_resource_address);
  CHECK(remote_support->chat.Load().get() != support_chat_address);
  CHECK(remote_support->resource.Load().get() != support_resource_address);
  CHECK(!remote_support->presenter.is_loaded());

  CHECK(support_runtime->chat->timeline.size() == 3);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support_runtime->chat->journal[2].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(CountPending(scanner, support_runtime) == 0);
  CHECK(alice_runtime->chat->timeline.size() == 3);
  CHECK(remote_support.Load().get() == remote_support_address);
  CHECK(CountPending(scanner, alice_runtime) == 0);

  bool const support_chat_loaded_before_send = support->chat.is_loaded();
  bool const support_presenter_loaded_before_send =
      support->presenter.is_loaded();
  bool const support_resource_loaded_before_send =
      support->resource.is_loaded();
  auto const support_chat_id_before_send = support->chat.id();
  auto const support_presenter_id_before_send = support->presenter.id();
  auto const support_resource_id_before_send = support->resource.id();

  support_runtime->chat->presenter->Send(
      "Hello from Support", ae::ObjId{204},
      ae::TimePoint{std::chrono::microseconds{400}});

  CHECK(support_runtime->chat->timeline.size() == 4);
  CHECK(support_runtime->chat->timeline[3].kind == ChatEntryKind::kMessage);
  CHECK(support_runtime->chat->timeline[3].client.is_valid());
  CHECK(support_runtime->chat->timeline[3].client.id() == support_id);
  CHECK(!support_runtime->chat->timeline[3].client.is_loaded());
  CHECK(support_runtime->chat->timeline[3].text == "Hello from Support");
  CHECK(support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[3]));

  CHECK(support_runtime->chat->journal.size() == 4);
  CHECK(support_runtime->chat->journal[3].event.id().id() == 204);
  CHECK(support_runtime->chat->journal[3].time ==
        ae::TimePoint{std::chrono::microseconds{400}});
  CHECK(support_runtime->chat->journal[3].delivery_status ==
        DeliveryStatus::kPending);
  auto* support_sender_event =
      support_runtime->chat->journal[3].event.Load().as<SendMessageEvent>();
  CHECK(support_sender_event != nullptr);
  CHECK(support_sender_event->sender.is_valid());
  CHECK(support_sender_event->sender.id() == support_id);
  CHECK(!support_sender_event->sender.is_loaded());
  CHECK(support_sender_event->text == "Hello from Support");

  CHECK(support.id() == support_id);
  CHECK(support->base.id() == support_base_id);
  CHECK(support->presenter.id() == support_presenter_id);
  CHECK(support->chat.is_loaded() == support_chat_loaded_before_send);
  CHECK(support->presenter.is_loaded() == support_presenter_loaded_before_send);
  CHECK(support->resource.is_loaded() == support_resource_loaded_before_send);
  CHECK(support->chat.id() == support_chat_id_before_send);
  CHECK(support->presenter.id() == support_presenter_id_before_send);
  CHECK(support->resource.id() == support_resource_id_before_send);
  CHECK(support->journal.empty());
  CHECK(support->base.Load().get() == support_base_address);
  CHECK(support_base->name == "Support");

  CHECK(alice_runtime->chat->timeline.size() == 3);
  CHECK(alice_runtime->chat->journal.size() == 3);
  CHECK(remote_support.Load().get() == remote_support_address);
  CHECK(CountPending(scanner, support_runtime) == 1);
  CHECK(CountPending(scanner, alice_runtime) == 0);

  LoopbackJournalMessageTransport support_message_transport{
      support_message_message_domain, support_message_transfer_storage,
      alice_domain, alice_storage, receiver};
  GraphSynchronizer support_message_synchronizer{
      support_message_message_domain, support_message_transport};
  support_message_synchronizer.Synchronize(support_runtime);

  CHECK(support_message_transport.send_count == 1);
  CHECK(support_message_transport.message_ids.size() == 1);
  CHECK(support_message_transport.target_event_pairs.size() == 1);
  CHECK((support_message_transport.target_event_pairs[0] ==
         PendingKey{100, 204}));
  CHECK(support_message_transfer_storage.state.size() == 2);
  CHECK(ContainsObj(support_message_transfer_storage,
                     support_message_transport.message_ids[0]));
  CHECK(ContainsObj(support_message_transfer_storage, 204));
  CHECK(!ContainsObj(support_message_transfer_storage, 1));
  CHECK(!ContainsObj(support_message_transfer_storage, 10));
  CHECK(!ContainsObj(support_message_transfer_storage, 11));
  CHECK(!ContainsObj(support_message_transfer_storage, 12));
  CHECK(!ContainsObj(support_message_transfer_storage, 50));
  CHECK(!ContainsObj(support_message_transfer_storage, 100));
  CHECK(!ContainsObj(support_message_transfer_storage, 101));
  CHECK(!ContainsObj(support_message_transfer_storage, 102));
  CHECK(!ContainsObj(support_message_transfer_storage, support_id));
  CHECK(!ContainsObj(support_message_transfer_storage, support_base_id));
  CHECK(!ContainsObj(support_message_transfer_storage, support_presenter_id));
  CHECK(!ContainsObj(support_message_transfer_storage, alice_id));
  CHECK(!ContainsObj(support_message_transfer_storage, alice_base_id));
  CHECK(!ContainsObj(support_message_transfer_storage, alice_presenter_id));
  CHECK(!ContainsObj(support_message_transfer_storage, 200));
  CHECK(!ContainsObj(support_message_transfer_storage, 201));
  CHECK(!ContainsObj(support_message_transfer_storage, 202));
  CHECK(!ContainsObj(support_message_transfer_storage, 203));

  CHECK(support_runtime->chat->journal[3].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->timeline.size() == 4);
  CHECK(support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[3]));
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(support->name == "Support");
  CHECK(support->journal.empty());
  CHECK(support->base.Load().get() == support_base_address);
  CHECK(CountPending(scanner, support_runtime) == 0);

  support_sender_event->sender.Load();
  support_runtime->chat->timeline[3].client.Load();
  CHECK(support_sender_event->sender.Load().get() == support_address);
  CHECK(support_runtime->chat->timeline[3].client.Load().get() ==
        support_address);

  CHECK(alice_runtime->chat->timeline.size() == 4);
  CHECK(alice_runtime->chat->journal.size() == 4);
  CHECK(alice_runtime->chat->timeline[3].kind == ChatEntryKind::kMessage);
  CHECK(alice_runtime->chat->timeline[3].client.id() == support_id);
  CHECK(!alice_runtime->chat->timeline[3].client.is_loaded());
  CHECK(alice_runtime->chat->timeline[3].text == "Hello from Support");
  CHECK(!alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[3]));
  CHECK(alice_runtime->chat->journal[3].event.id().id() == 204);
  CHECK(alice_runtime->chat->journal[3].time ==
        ae::TimePoint{std::chrono::microseconds{400}});
  CHECK(alice_runtime->chat->journal[3].delivery_status ==
        DeliveryStatus::kDelivered);
  auto* alice_support_message =
      alice_runtime->chat->journal[3].event.Load().as<SendMessageEvent>();
  CHECK(alice_support_message != nullptr);
  CHECK(alice_support_message->sender.is_valid());
  CHECK(alice_support_message->sender.id() == support_id);
  CHECK(!alice_support_message->sender.is_loaded());
  CHECK(alice_support_message->text == "Hello from Support");

  CHECK(remote_support.Load().get() == remote_support_address);
  CHECK(alice.Load().get() == alice_address);
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(alice->name == "Alice Cooper");
  CHECK(alice->journal[0].event.id().id() == 202);
  CHECK(alice->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(remote_support->name == "Support");
  CHECK(remote_support->journal.empty());

  alice_support_message->sender.Load();
  alice_runtime->chat->timeline[3].client.Load();
  CHECK(alice_support_message->sender.Load().get() == remote_support_address);
  CHECK(alice_runtime->chat->timeline[3].client.Load().get() ==
        remote_support_address);
  CHECK(alice_runtime->chat->timeline[2].client.Load().get() ==
        remote_support_address);
  CHECK(alice_support_join->client.Load().get() == remote_support_address);

  CHECK(alice_runtime->chat->timeline.size() == 4);
  CHECK(alice_runtime->chat->journal.size() == 4);
  CHECK(support_runtime->chat->timeline.size() == 4);
  CHECK(support_runtime->chat->journal.size() == 4);

  CHECK(alice_runtime->chat->timeline[0].kind == ChatEntryKind::kClientJoined);
  CHECK(alice_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(alice_runtime->chat->timeline[1].text == "Hello from Alice");
  CHECK(alice_runtime->chat->timeline[2].kind == ChatEntryKind::kClientJoined);
  CHECK(alice_runtime->chat->timeline[3].kind == ChatEntryKind::kMessage);
  CHECK(alice_runtime->chat->timeline[3].text == "Hello from Support");
  CHECK(support_runtime->chat->timeline[0].kind ==
        ChatEntryKind::kClientJoined);
  CHECK(support_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(support_runtime->chat->timeline[1].text == "Hello from Alice");
  CHECK(support_runtime->chat->timeline[2].kind ==
        ChatEntryKind::kClientJoined);
  CHECK(support_runtime->chat->timeline[3].kind == ChatEntryKind::kMessage);
  CHECK(support_runtime->chat->timeline[3].text == "Hello from Support");

  CHECK(alice_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(alice_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(alice_runtime->chat->journal[2].event.id().id() == 203);
  CHECK(alice_runtime->chat->journal[3].event.id().id() == 204);
  CHECK(support_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(support_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(support_runtime->chat->journal[2].event.id().id() == 203);
  CHECK(support_runtime->chat->journal[3].event.id().id() == 204);
  CHECK(alice_runtime->chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(alice_runtime->chat->journal[1].time ==
        ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(alice_runtime->chat->journal[2].time ==
        ae::TimePoint{std::chrono::microseconds{300}});
  CHECK(alice_runtime->chat->journal[3].time ==
        ae::TimePoint{std::chrono::microseconds{400}});
  CHECK(alice_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[2].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[3].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[2].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(support_runtime->chat->journal[3].delivery_status ==
        DeliveryStatus::kDelivered);

  CHECK(alice_runtime->chat->journal[0].event.Load().as<ClientJoinedEvent>() !=
        nullptr);
  CHECK(alice_runtime->chat->journal[1].event.Load().as<SendMessageEvent>() !=
        nullptr);
  CHECK(alice_runtime->chat->journal[2].event.Load().as<ClientJoinedEvent>() !=
        nullptr);
  CHECK(alice_runtime->chat->journal[3].event.Load().as<SendMessageEvent>() !=
        nullptr);

  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[0]));
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[1]));
  CHECK(!alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[2]));
  CHECK(!alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[3]));
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[0]));
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[1]));
  CHECK(support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[2]));
  CHECK(support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[3]));

  CHECK(alice->name == "Alice Cooper");
  CHECK(alice->journal.size() == 1);
  CHECK(alice->journal[0].event.id().id() == 202);
  CHECK(alice->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(remote_support->name == "Support");
  CHECK(remote_support->journal.empty());
  CHECK(remote_alice->name == "Alice Cooper");
  CHECK(remote_alice->journal.size() == 1);
  CHECK(remote_alice->journal[0].event.id().id() == 202);
  CHECK(remote_alice->journal[0].delivery_status == DeliveryStatus::kDelivered);
  CHECK(support->name == "Support");
  CHECK(support->journal.empty());
  CHECK(alice_base->name == "Alice");
  CHECK(alice_base->journal.empty());
  CHECK(support_base->name == "Support");
  CHECK(support_base->journal.empty());
  CHECK(remote_support_base->name == "Support");
  CHECK(remote_support_base->journal.empty());
  CHECK(remote_alice_base->name == "Alice");
  CHECK(remote_alice_base->journal.empty());

  CHECK(CountPending(scanner, alice_runtime) == 0);
  CHECK(CountPending(scanner, support_runtime) == 0);

  auto const support_msg_send_count_before_repeat =
      support_message_transport.send_count;
  auto const support_msg_ids_before_repeat =
      support_message_transport.message_ids.size();
  auto const support_msg_transfer_size_before_repeat =
      support_message_transfer_storage.state.size();
  auto const alice_timeline_before_repeat =
      alice_runtime->chat->timeline.size();
  auto const alice_journal_size_before_repeat =
      alice_runtime->chat->journal.size();

  support_message_synchronizer.Synchronize(support_runtime);

  CHECK(support_message_transport.send_count ==
        support_msg_send_count_before_repeat);
  CHECK(support_message_transport.message_ids.size() ==
        support_msg_ids_before_repeat);
  CHECK(support_message_transfer_storage.state.size() ==
        support_msg_transfer_size_before_repeat);
  CHECK(alice_runtime->chat->timeline.size() == alice_timeline_before_repeat);
  CHECK(alice_runtime->chat->journal.size() ==
        alice_journal_size_before_repeat);
  CHECK(alice_runtime->chat->timeline[3].text == "Hello from Support");
  CHECK(remote_support.Load().get() == remote_support_address);
  CHECK(alice.Load().get() == alice_address);

  alice_runtime.Save();
  support_runtime.Save();

  ae::Domain alice_reload_domain{ae::Now(), alice_storage};
  Runtime::ptr reloaded_alice_runtime =
      Runtime::ptr::Declare(ae::CreateWith{alice_reload_domain}.with_id(1));
  reloaded_alice_runtime.Load();
  CHECK(reloaded_alice_runtime.is_loaded());

  auto reloaded_alice_chat = reloaded_alice_runtime->chat;
  CHECK(reloaded_alice_chat->timeline.size() == 4);
  CHECK(reloaded_alice_chat->journal.size() == 4);
  CHECK(reloaded_alice_chat->timeline[0].kind == ChatEntryKind::kClientJoined);
  CHECK(reloaded_alice_chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(reloaded_alice_chat->timeline[1].text == "Hello from Alice");
  CHECK(reloaded_alice_chat->timeline[2].kind == ChatEntryKind::kClientJoined);
  CHECK(reloaded_alice_chat->timeline[3].kind == ChatEntryKind::kMessage);
  CHECK(reloaded_alice_chat->timeline[3].text == "Hello from Support");
  CHECK(reloaded_alice_chat->journal[0].event.id().id() == 200);
  CHECK(reloaded_alice_chat->journal[1].event.id().id() == 201);
  CHECK(reloaded_alice_chat->journal[2].event.id().id() == 203);
  CHECK(reloaded_alice_chat->journal[3].event.id().id() == 204);
  CHECK(reloaded_alice_chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_alice_chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_alice_chat->journal[2].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_alice_chat->journal[3].delivery_status ==
        DeliveryStatus::kDelivered);

  auto reloaded_alice = reloaded_alice_chat->presenter->client;
  CHECK(reloaded_alice.is_loaded());
  CHECK(reloaded_alice.id() == alice_id);
  CHECK(reloaded_alice->name == "Alice Cooper");
  CHECK(reloaded_alice->journal.size() == 1);
  CHECK(reloaded_alice->journal[0].event.id().id() == 202);
  CHECK(reloaded_alice->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  auto* reloaded_alice_rename =
      reloaded_alice->journal[0].event.Load().as<RenameClientEvent>();
  CHECK(reloaded_alice_rename != nullptr);
  CHECK(reloaded_alice_rename->name == "Alice Cooper");
  auto* reloaded_alice_base = reloaded_alice->base.Load().as<Client>();
  CHECK(reloaded_alice_base != nullptr);
  CHECK(reloaded_alice_base->name == "Alice");
  CHECK(reloaded_alice_base->journal.empty());

  auto reloaded_remote_support = reloaded_alice_chat->timeline[2].client;
  CHECK(reloaded_remote_support.is_loaded());
  CHECK(reloaded_remote_support.id() == support_id);
  CHECK(reloaded_remote_support->name == "Support");
  CHECK(reloaded_remote_support->journal.empty());
  auto* reloaded_remote_support_base =
      reloaded_remote_support->base.Load().as<Client>();
  CHECK(reloaded_remote_support_base != nullptr);
  CHECK(reloaded_remote_support_base->name == "Support");

  CHECK(!reloaded_alice_chat->timeline[1].client.is_loaded());
  CHECK(!reloaded_alice_chat->timeline[3].client.is_loaded());
  auto* reloaded_alice_msg =
      reloaded_alice_chat->journal[1].event.Load().as<SendMessageEvent>();
  auto* reloaded_support_join =
      reloaded_alice_chat->journal[2].event.Load().as<ClientJoinedEvent>();
  auto* reloaded_support_msg =
      reloaded_alice_chat->journal[3].event.Load().as<SendMessageEvent>();
  CHECK(reloaded_alice_msg != nullptr);
  CHECK(reloaded_support_join != nullptr);
  CHECK(reloaded_support_msg != nullptr);
  CHECK(!reloaded_alice_msg->sender.is_loaded());
  CHECK(!reloaded_support_msg->sender.is_loaded());

  reloaded_alice_chat->timeline[0].client.Load();
  reloaded_alice_chat->timeline[1].client.Load();
  reloaded_alice_msg->sender.Load();
  reloaded_alice_chat->timeline[3].client.Load();
  reloaded_support_msg->sender.Load();
  CHECK(reloaded_alice_chat->timeline[0].client.Load().get() ==
        reloaded_alice.Load().get());
  CHECK(reloaded_alice_chat->timeline[1].client.Load().get() ==
        reloaded_alice.Load().get());
  CHECK(reloaded_alice_msg->sender.Load().get() == reloaded_alice.Load().get());
  CHECK(reloaded_alice_chat->timeline[2].client.Load().get() ==
        reloaded_remote_support.Load().get());
  CHECK(reloaded_alice_chat->timeline[3].client.Load().get() ==
        reloaded_remote_support.Load().get());
  CHECK(reloaded_support_join->client.Load().get() ==
        reloaded_remote_support.Load().get());
  CHECK(reloaded_support_msg->sender.Load().get() ==
        reloaded_remote_support.Load().get());
  CHECK(reloaded_alice_chat->timeline[0].client->name == "Alice Cooper");
  CHECK(reloaded_alice_chat->timeline[1].client->name == "Alice Cooper");

  CHECK(reloaded_remote_support->presenter.is_valid());
  CHECK(reloaded_remote_support->presenter.id() == support_presenter_id);
  CHECK(!reloaded_remote_support->presenter.is_loaded());
  CHECK(!ContainsObj(alice_storage, support_presenter_id));

  CHECK(reloaded_alice_chat->presenter->IsLocal(
      reloaded_alice_chat->timeline[0]));
  CHECK(reloaded_alice_chat->presenter->IsLocal(
      reloaded_alice_chat->timeline[1]));
  CHECK(!reloaded_alice_chat->presenter->IsLocal(
      reloaded_alice_chat->timeline[2]));
  CHECK(!reloaded_alice_chat->presenter->IsLocal(
      reloaded_alice_chat->timeline[3]));

  CHECK(reloaded_alice->presenter.is_valid());
  CHECK(reloaded_alice->presenter.id() == alice_presenter_id);
  CHECK(!reloaded_alice->presenter.is_loaded());
  CHECK(ContainsObj(alice_storage, alice_presenter_id));
  reloaded_alice->presenter.Load();
  auto* reloaded_alice_presenter =
      reloaded_alice->presenter.Load().as<ClientPresenter>();
  CHECK(reloaded_alice_presenter != nullptr);
  CHECK(reloaded_alice->presenter.is_loaded());
  CHECK(reloaded_alice_presenter->client.Load().get() ==
        reloaded_alice.Load().get());
  CHECK(reloaded_alice->presenter.id() !=
        reloaded_alice_runtime->client_prefab->presenter.id());

  ae::Domain support_reload_domain{ae::Now(), support_storage};
  Runtime::ptr reloaded_support_runtime =
      Runtime::ptr::Declare(ae::CreateWith{support_reload_domain}.with_id(1));
  reloaded_support_runtime.Load();
  CHECK(reloaded_support_runtime.is_loaded());

  auto reloaded_support_chat = reloaded_support_runtime->chat;
  CHECK(reloaded_support_chat->timeline.size() == 4);
  CHECK(reloaded_support_chat->journal.size() == 4);
  CHECK(reloaded_support_chat->timeline[0].kind ==
        ChatEntryKind::kClientJoined);
  CHECK(reloaded_support_chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(reloaded_support_chat->timeline[1].text == "Hello from Alice");
  CHECK(reloaded_support_chat->timeline[2].kind ==
        ChatEntryKind::kClientJoined);
  CHECK(reloaded_support_chat->timeline[3].kind == ChatEntryKind::kMessage);
  CHECK(reloaded_support_chat->timeline[3].text == "Hello from Support");
  CHECK(reloaded_support_chat->journal[0].event.id().id() == 200);
  CHECK(reloaded_support_chat->journal[1].event.id().id() == 201);
  CHECK(reloaded_support_chat->journal[2].event.id().id() == 203);
  CHECK(reloaded_support_chat->journal[3].event.id().id() == 204);
  CHECK(reloaded_support_chat->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_support_chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_support_chat->journal[2].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_support_chat->journal[3].delivery_status ==
        DeliveryStatus::kDelivered);

  auto reloaded_support = reloaded_support_chat->presenter->client;
  CHECK(reloaded_support.is_loaded());
  CHECK(reloaded_support.id() == support_id);
  CHECK(reloaded_support->name == "Support");
  CHECK(reloaded_support->journal.empty());

  auto reloaded_remote_alice = reloaded_support_chat->timeline[0].client;
  CHECK(reloaded_remote_alice.is_loaded());
  CHECK(reloaded_remote_alice.id() == alice_id);
  CHECK(reloaded_remote_alice->name == "Alice Cooper");
  CHECK(reloaded_remote_alice->journal.size() == 1);
  CHECK(reloaded_remote_alice->journal[0].event.id().id() == 202);
  CHECK(reloaded_remote_alice->journal[0].delivery_status ==
        DeliveryStatus::kDelivered);
  auto* reloaded_remote_rename =
      reloaded_remote_alice->journal[0].event.Load().as<RenameClientEvent>();
  CHECK(reloaded_remote_rename != nullptr);
  CHECK(reloaded_remote_rename->name == "Alice Cooper");
  auto* reloaded_remote_base = reloaded_remote_alice->base.Load().as<Client>();
  CHECK(reloaded_remote_base != nullptr);
  CHECK(reloaded_remote_base->name == "Alice");
  CHECK(reloaded_remote_base->journal.empty());

  CHECK(!reloaded_support_chat->timeline[1].client.is_loaded());
  CHECK(!reloaded_support_chat->timeline[3].client.is_loaded());
  auto* reloaded_receiver_event =
      reloaded_support_chat->journal[1].event.Load().as<SendMessageEvent>();
  auto* reloaded_local_support_join =
      reloaded_support_chat->journal[2].event.Load().as<ClientJoinedEvent>();
  auto* reloaded_local_support_msg =
      reloaded_support_chat->journal[3].event.Load().as<SendMessageEvent>();
  CHECK(reloaded_receiver_event != nullptr);
  CHECK(reloaded_local_support_join != nullptr);
  CHECK(reloaded_local_support_msg != nullptr);
  CHECK(!reloaded_receiver_event->sender.is_loaded());
  CHECK(!reloaded_local_support_msg->sender.is_loaded());

  reloaded_support_chat->timeline[0].client.Load();
  reloaded_support_chat->timeline[1].client.Load();
  reloaded_receiver_event->sender.Load();
  reloaded_support_chat->timeline[2].client.Load();
  reloaded_support_chat->timeline[3].client.Load();
  reloaded_local_support_msg->sender.Load();
  auto* reloaded_join =
      reloaded_support_chat->journal[0].event.Load().as<ClientJoinedEvent>();
  CHECK(reloaded_join != nullptr);
  CHECK(reloaded_support_chat->timeline[0].client.Load().get() ==
        reloaded_remote_alice.Load().get());
  CHECK(reloaded_support_chat->timeline[1].client.Load().get() ==
        reloaded_remote_alice.Load().get());
  CHECK(reloaded_receiver_event->sender.Load().get() ==
        reloaded_remote_alice.Load().get());
  CHECK(reloaded_join->client.Load().get() ==
        reloaded_remote_alice.Load().get());
  CHECK(reloaded_support_chat->timeline[2].client.Load().get() ==
        reloaded_support.Load().get());
  CHECK(reloaded_support_chat->timeline[3].client.Load().get() ==
        reloaded_support.Load().get());
  CHECK(reloaded_local_support_join->client.Load().get() ==
        reloaded_support.Load().get());
  CHECK(reloaded_local_support_msg->sender.Load().get() ==
        reloaded_support.Load().get());
  CHECK(reloaded_support_chat->presenter->client.Load().get() ==
        reloaded_support.Load().get());
  CHECK(reloaded_support_chat->timeline[0].client->name == "Alice Cooper");
  CHECK(reloaded_support_chat->timeline[1].client->name == "Alice Cooper");

  CHECK(!reloaded_support_chat->presenter->IsLocal(
      reloaded_support_chat->timeline[0]));
  CHECK(!reloaded_support_chat->presenter->IsLocal(
      reloaded_support_chat->timeline[1]));
  CHECK(reloaded_support_chat->presenter->IsLocal(
      reloaded_support_chat->timeline[2]));
  CHECK(reloaded_support_chat->presenter->IsLocal(
      reloaded_support_chat->timeline[3]));

  CHECK(reloaded_remote_alice->presenter.is_valid());
  CHECK(reloaded_remote_alice->presenter.id() == alice_presenter_id);
  CHECK(!reloaded_remote_alice->presenter.is_loaded());
  CHECK(!ContainsObj(support_storage, alice_presenter_id));
  CHECK(reloaded_support.Load().get() != reloaded_remote_alice.Load().get());

  CHECK(reloaded_alice.Load().get() != reloaded_remote_alice.Load().get());
  CHECK(reloaded_alice.id() == reloaded_remote_alice.id());
  CHECK(reloaded_support.Load().get() != reloaded_remote_support.Load().get());
  CHECK(reloaded_support.id() == reloaded_remote_support.id());
  CHECK(reloaded_alice_rename != reloaded_remote_rename);
  CHECK(reloaded_alice_rename->obj_id.id() == 202);
  CHECK(reloaded_remote_rename->obj_id.id() == 202);
  CHECK(reloaded_alice_base->obj_id == reloaded_remote_base->obj_id);
  CHECK(reloaded_alice_base != reloaded_remote_base);
  CHECK(reloaded_support_msg != reloaded_local_support_msg);
  CHECK(reloaded_support_msg->obj_id.id() == 204);
  CHECK(reloaded_local_support_msg->obj_id.id() == 204);
  CHECK(reloaded_alice_chat.Load().get() != reloaded_support_chat.Load().get());
  CHECK(reloaded_alice_runtime.Load().get() !=
        reloaded_support_runtime.Load().get());
  CHECK(reloaded_alice_chat.id().id() == 100);
  CHECK(reloaded_support_chat.id().id() == 100);
  CHECK(reloaded_alice.id() == alice_id);
  CHECK(reloaded_remote_alice.id() == alice_id);
  CHECK(reloaded_support.id() == support_id);
  CHECK(reloaded_remote_support.id() == support_id);
  CHECK(reloaded_alice_runtime.Load().get() != alice_runtime_address);
  CHECK(reloaded_support_runtime.Load().get() != support_runtime_address);
  CHECK(reloaded_alice_chat.Load().get() != alice_chat_address);
  CHECK(reloaded_support_chat.Load().get() != support_chat_address);

  return EXIT_SUCCESS;
}
