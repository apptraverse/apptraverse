#include <cassert>
#include <chrono>
#include <cstdint>
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
class SendMessageEvent;

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
  void Send(std::string text, ae::ObjId event_id, ae::TimePoint time,
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

void ChatPresenter::Send(std::string text, ae::ObjId event_id,
                         ae::TimePoint time,
                         std::vector<ae::ObjId> recipients) {
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
    assert(event_message->identity.IsValid());
    assert(event_message->event.is_valid());
    assert(event_message->event.is_loaded());

    apptraverse::EventIdentity const expected_identity =
        event_message->identity;

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
  scanner.VisitPending(root, recipient,
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
  using apptraverse::test::ChatEntryKind;
  using apptraverse::test::Client;
  using apptraverse::test::ClientJoinedEvent;
  using apptraverse::test::LoopbackJournalMessageTransport;
  using apptraverse::test::Runtime;
  using apptraverse::test::SendMessageEvent;

  ae::RamDomainStorage alice_storage;
  ae::RamDomainStorage support_storage;
  ae::RamDomainStorage bob_storage;

  ae::RamDomainStorage join_support_transfer_storage;
  ae::RamDomainStorage join_bob_transfer_storage;
  ae::RamDomainStorage message_support_transfer_storage;
  ae::RamDomainStorage message_bob_transfer_storage;

  ae::Domain alice_domain{ae::Now(), alice_storage};
  ae::Domain support_domain{ae::Now(), support_storage};
  ae::Domain bob_domain{ae::Now(), bob_storage};

  ae::Domain join_support_message_domain{ae::Now(),
                                         join_support_transfer_storage};
  ae::Domain join_bob_message_domain{ae::Now(), join_bob_transfer_storage};
  ae::Domain message_support_message_domain{ae::Now(),
                                            message_support_transfer_storage};
  ae::Domain message_bob_message_domain{ae::Now(),
                                        message_bob_transfer_storage};

  auto alice_runtime = BuildInitialRuntime(alice_domain);
  auto support_runtime = BuildInitialRuntime(support_domain);
  auto bob_runtime = BuildInitialRuntime(bob_domain);

  CHECK(alice_storage.state.size() == 8);
  CHECK(support_storage.state.size() == 8);
  CHECK(bob_storage.state.size() == 8);

  auto alice = alice_runtime->CreateLocalClient("Alice");
  auto support = support_runtime->CreateLocalClient("Support");
  auto bob = bob_runtime->CreateLocalClient("Bob");

  CHECK(alice.is_loaded());
  CHECK(support.is_loaded());
  CHECK(bob.is_loaded());
  CHECK(alice.id() != support.id());
  CHECK(alice.id() != bob.id());
  CHECK(support.id() != bob.id());

  auto const alice_id = alice.id();
  auto const alice_base_id = alice->base.id();
  auto const alice_presenter_id = alice->presenter.id();
  auto* alice_address = alice.Load().get();
  auto* alice_runtime_address = alice_runtime.Load().get();
  auto* alice_chat_address = alice_runtime->chat.Load().get();

  auto const support_id = support.id();
  auto* support_address = support.Load().get();
  auto* support_runtime_address = support_runtime.Load().get();
  auto* support_chat_address = support_runtime->chat.Load().get();

  auto const bob_id = bob.id();
  auto* bob_address = bob.Load().get();
  auto* bob_runtime_address = bob_runtime.Load().get();
  auto* bob_chat_address = bob_runtime->chat.Load().get();

  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(bob_runtime->chat->presenter->client.Load().get() == bob_address);
  CHECK(alice_runtime->chat->timeline.empty());
  CHECK(support_runtime->chat->timeline.empty());
  CHECK(bob_runtime->chat->timeline.empty());
  CHECK(alice_runtime->chat->journal.empty());
  CHECK(support_runtime->chat->journal.empty());
  CHECK(bob_runtime->chat->journal.empty());

  alice_runtime->chat->presenter->Join(
      ae::ObjId{200}, ae::TimePoint{std::chrono::microseconds{100}},
      {bob_id, support_id});

  CHECK(alice_runtime->chat->timeline.size() == 1);
  CHECK(alice_runtime->chat->journal.size() == 1);
  auto& alice_join_record = alice_runtime->chat->journal[0];
  CHECK(alice_join_record.origin == EventRecordOrigin::kLocal);
  CHECK(alice_join_record.identity.origin == alice_id);
  CHECK(alice_join_record.identity.sequence == 1);
  CHECK(alice_join_record.recipients.size() == 2);
  CHECK(alice_join_record.recipients[0].recipient <
        alice_join_record.recipients[1].recipient);
  auto* join_support_state = alice_join_record.FindRecipient(support_id);
  auto* join_bob_state = alice_join_record.FindRecipient(bob_id);
  CHECK(join_support_state != nullptr);
  CHECK(join_bob_state != nullptr);
  CHECK(join_support_state->delivery_status == DeliveryStatus::kPending);
  CHECK(join_bob_state->delivery_status == DeliveryStatus::kPending);
  auto* join_event = alice_join_record.event.Load().as<ClientJoinedEvent>();
  CHECK(join_event != nullptr);
  CHECK(join_event->client.is_loaded());
  CHECK(join_event->client.Load().get() == alice_address);
  CHECK(alice->base.is_loaded());
  CHECK(!alice->chat.is_loaded());
  CHECK(!alice->presenter.is_loaded());
  CHECK(!alice->resource.is_loaded());
  CHECK(alice_runtime->chat->presenter->client.is_loaded());
  CHECK(alice_runtime->chat->presenter->client.Load().get() == alice_address);

  JournalMessageReceiver support_receiver;
  JournalMessageReceiver bob_receiver;
  LoopbackJournalMessageTransport join_support_transport{
      join_support_message_domain, join_support_transfer_storage,
      support_domain, support_storage, support_receiver};
  GraphSynchronizer join_support_sync{support_id, join_support_message_domain,
                                      join_support_transport};
  join_support_sync.Synchronize(alice_runtime);

  CHECK(join_support_state->delivery_status == DeliveryStatus::kDelivered);
  CHECK(join_bob_state->delivery_status == DeliveryStatus::kPending);

  GraphJournalScanner scanner;
  CHECK(CountPending(scanner, alice_runtime, support_id) == 0);
  CHECK(CountPending(scanner, alice_runtime, bob_id) == 1);

  CHECK(support_runtime->chat->timeline.size() == 1);
  CHECK(support_runtime->chat->journal.size() == 1);
  CHECK(support_runtime->chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(support_runtime->chat->journal[0].identity == alice_join_record.identity);
  CHECK(support_runtime->chat->journal[0].recipients.empty());
  auto support_remote_alice = support_runtime->chat->timeline[0].client;
  CHECK(support_remote_alice.is_loaded());
  CHECK(support_remote_alice.id() == alice_id);
  CHECK(support_remote_alice.Load().get() != alice_address);
  auto* support_remote_alice_address = support_remote_alice.Load().get();
  CHECK(support_remote_alice->base.is_loaded());
  CHECK(support_remote_alice->base.id() == alice_base_id);
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[0]));

  CHECK(bob_runtime->chat->timeline.empty());
  CHECK(bob_runtime->chat->journal.empty());

  CHECK(CountPending(scanner, support_runtime, bob_id) == 0);
  ae::RamDomainStorage forward_join_transfer_storage;
  ae::Domain forward_join_message_domain{ae::Now(),
                                         forward_join_transfer_storage};
  JournalMessageReceiver forward_join_receiver;
  LoopbackJournalMessageTransport forward_join_transport{
      forward_join_message_domain, forward_join_transfer_storage, bob_domain,
      bob_storage, forward_join_receiver};
  GraphSynchronizer forward_join_sync{bob_id, forward_join_message_domain,
                                      forward_join_transport};
  forward_join_sync.Synchronize(support_runtime);
  CHECK(forward_join_transport.send_count == 0);
  CHECK(bob_runtime->chat->timeline.empty());

  CHECK(join_support_transfer_storage.state.size() == 4);
  CHECK(ContainsObj(join_support_transfer_storage,
                     join_support_transport.message_ids[0]));
  CHECK(ContainsObj(join_support_transfer_storage, 200));
  CHECK(ContainsObj(join_support_transfer_storage, alice_id));
  CHECK(ContainsObj(join_support_transfer_storage, alice_base_id));
  CHECK(!ContainsObj(join_support_transfer_storage, 1));
  CHECK(!ContainsObj(join_support_transfer_storage, 10));
  CHECK(!ContainsObj(join_support_transfer_storage, 50));
  CHECK(!ContainsObj(join_support_transfer_storage, 100));
  CHECK(!ContainsObj(join_support_transfer_storage, alice_presenter_id));
  CHECK(!ContainsObj(join_support_transfer_storage, support_id));
  CHECK(!ContainsObj(join_support_transfer_storage, bob_id));

  LoopbackJournalMessageTransport join_bob_transport{
      join_bob_message_domain, join_bob_transfer_storage, bob_domain,
      bob_storage, bob_receiver};
  GraphSynchronizer join_bob_sync{bob_id, join_bob_message_domain,
                                  join_bob_transport};
  join_bob_sync.Synchronize(alice_runtime);

  CHECK(join_support_state->delivery_status == DeliveryStatus::kDelivered);
  CHECK(join_bob_state->delivery_status == DeliveryStatus::kDelivered);
  CHECK(bob_runtime->chat->timeline.size() == 1);
  CHECK(bob_runtime->chat->journal.size() == 1);
  CHECK(bob_runtime->chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(bob_runtime->chat->journal[0].identity == alice_join_record.identity);
  CHECK(bob_runtime->chat->journal[0].recipients.empty());
  auto bob_remote_alice = bob_runtime->chat->timeline[0].client;
  CHECK(bob_remote_alice.is_loaded());
  CHECK(bob_remote_alice.id() == alice_id);
  CHECK(bob_remote_alice.Load().get() != alice_address);
  CHECK(bob_remote_alice.Load().get() != support_remote_alice_address);
  auto* bob_remote_alice_address = bob_remote_alice.Load().get();
  CHECK(bob_runtime->chat->presenter->client.Load().get() == bob_address);
  CHECK(!bob_runtime->chat->presenter->IsLocal(bob_runtime->chat->timeline[0]));
  CHECK(join_bob_transfer_storage.state.size() == 4);
  CHECK(ContainsObj(join_bob_transfer_storage, join_bob_transport.message_ids[0]));
  CHECK(ContainsObj(join_bob_transfer_storage, 200));
  CHECK(ContainsObj(join_bob_transfer_storage, alice_id));
  CHECK(ContainsObj(join_bob_transfer_storage, alice_base_id));

  CHECK(CountPending(scanner, alice_runtime, support_id) == 0);
  CHECK(CountPending(scanner, alice_runtime, bob_id) == 0);
  auto const join_support_send_before = join_support_transport.send_count;
  auto const join_bob_send_before = join_bob_transport.send_count;
  join_support_sync.Synchronize(alice_runtime);
  join_bob_sync.Synchronize(alice_runtime);
  CHECK(join_support_transport.send_count == join_support_send_before);
  CHECK(join_bob_transport.send_count == join_bob_send_before);

  alice->chat.Load();
  alice->resource.Load();
  alice->presenter.Load();
  CHECK(alice->chat.Load().get() == alice_chat_address);
  CHECK(alice->presenter.id() == alice_presenter_id);
  CHECK(alice->presenter->client.Load().get() == alice_address);

  alice_runtime->chat->presenter->Send(
      "Hello everyone", ae::ObjId{201},
      ae::TimePoint{std::chrono::microseconds{200}}, {bob_id, support_id});

  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(alice_runtime->chat->timeline[1].client.id() == alice_id);
  CHECK(!alice_runtime->chat->timeline[1].client.is_loaded());
  CHECK(alice_runtime->chat->timeline[1].text == "Hello everyone");
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[1]));
  CHECK(alice_runtime->chat->journal.size() == 2);
  auto& alice_message_record = alice_runtime->chat->journal[1];
  CHECK(alice_message_record.origin == EventRecordOrigin::kLocal);
  CHECK(alice_message_record.identity.origin == alice_id);
  CHECK(alice_message_record.identity.sequence == 2);
  CHECK(alice_message_record.event.id().id() == 201);
  CHECK(alice_message_record.recipients.size() == 2);
  auto* message_support_state = alice_message_record.FindRecipient(support_id);
  auto* message_bob_state = alice_message_record.FindRecipient(bob_id);
  CHECK(message_support_state != nullptr);
  CHECK(message_bob_state != nullptr);
  CHECK(message_support_state->delivery_status == DeliveryStatus::kPending);
  CHECK(message_bob_state->delivery_status == DeliveryStatus::kPending);
  auto* sender_event =
      alice_message_record.event.Load().as<SendMessageEvent>();
  CHECK(sender_event != nullptr);
  CHECK(sender_event->sender.is_valid());
  CHECK(!sender_event->sender.is_loaded());
  CHECK(sender_event->sender.id() == alice_id);
  CHECK(alice.id() == alice_id);
  CHECK(alice->base.id() == alice_base_id);
  CHECK(alice->presenter.id() == alice_presenter_id);

  LoopbackJournalMessageTransport message_support_transport{
      message_support_message_domain, message_support_transfer_storage,
      support_domain, support_storage, support_receiver};
  GraphSynchronizer message_support_sync{
      support_id, message_support_message_domain, message_support_transport};
  message_support_sync.Synchronize(alice_runtime);

  CHECK(message_support_state->delivery_status == DeliveryStatus::kDelivered);
  CHECK(message_bob_state->delivery_status == DeliveryStatus::kPending);
  CHECK(support_runtime->chat->timeline.size() == 2);
  CHECK(support_runtime->chat->timeline[1].text == "Hello everyone");
  CHECK(support_runtime->chat->journal.size() == 2);
  CHECK(support_runtime->chat->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(support_runtime->chat->journal[1].identity ==
        alice_message_record.identity);
  CHECK(support_runtime->chat->journal[1].recipients.empty());
  CHECK(support_runtime->chat->timeline[1].client.is_valid());
  CHECK(!support_runtime->chat->timeline[1].client.is_loaded());
  auto* support_message_event =
      support_runtime->chat->journal[1].event.Load().as<SendMessageEvent>();
  CHECK(support_message_event != nullptr);
  CHECK(!support_message_event->sender.is_loaded());
  support_message_event->sender.Load();
  support_runtime->chat->timeline[1].client.Load();
  CHECK(support_message_event->sender.Load().get() ==
        support_remote_alice_address);
  CHECK(support_runtime->chat->timeline[1].client.Load().get() ==
        support_remote_alice_address);
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[1]));
  CHECK(bob_runtime->chat->timeline.size() == 1);
  CHECK(message_support_transfer_storage.state.size() == 2);
  CHECK(ContainsObj(message_support_transfer_storage,
                     message_support_transport.message_ids[0]));
  CHECK(ContainsObj(message_support_transfer_storage, 201));
  CHECK(!ContainsObj(message_support_transfer_storage, alice_id));
  CHECK(!ContainsObj(message_support_transfer_storage, alice_base_id));
  CHECK(!ContainsObj(message_support_transfer_storage, alice_presenter_id));
  CHECK(!ContainsObj(message_support_transfer_storage, 100));
  CHECK(!ContainsObj(message_support_transfer_storage, 50));
  CHECK(!ContainsObj(message_support_transfer_storage, 200));

  LoopbackJournalMessageTransport message_bob_transport{
      message_bob_message_domain, message_bob_transfer_storage, bob_domain,
      bob_storage, bob_receiver};
  GraphSynchronizer message_bob_sync{bob_id, message_bob_message_domain,
                                     message_bob_transport};
  message_bob_sync.Synchronize(alice_runtime);

  CHECK(message_support_state->delivery_status == DeliveryStatus::kDelivered);
  CHECK(message_bob_state->delivery_status == DeliveryStatus::kDelivered);
  CHECK(bob_runtime->chat->timeline.size() == 2);
  CHECK(bob_runtime->chat->timeline[1].text == "Hello everyone");
  CHECK(bob_runtime->chat->journal.size() == 2);
  CHECK(bob_runtime->chat->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(bob_runtime->chat->journal[1].identity ==
        alice_message_record.identity);
  CHECK(bob_runtime->chat->journal[1].recipients.empty());
  auto* bob_message_event =
      bob_runtime->chat->journal[1].event.Load().as<SendMessageEvent>();
  CHECK(bob_message_event != nullptr);
  CHECK(!bob_message_event->sender.is_loaded());
  bob_message_event->sender.Load();
  bob_runtime->chat->timeline[1].client.Load();
  CHECK(bob_message_event->sender.Load().get() == bob_remote_alice_address);
  CHECK(bob_runtime->chat->timeline[1].client.Load().get() ==
        bob_remote_alice_address);
  CHECK(!bob_runtime->chat->presenter->IsLocal(bob_runtime->chat->timeline[1]));
  CHECK(message_bob_transfer_storage.state.size() == 2);
  CHECK(ContainsObj(message_bob_transfer_storage,
                     message_bob_transport.message_ids[0]));
  CHECK(ContainsObj(message_bob_transfer_storage, 201));
  CHECK(!ContainsObj(message_bob_transfer_storage, alice_id));

  CHECK(alice_runtime->chat->timeline.size() == 2);
  CHECK(alice_runtime->chat->journal.size() == 2);
  CHECK(support_runtime->chat->timeline.size() == 2);
  CHECK(support_runtime->chat->journal.size() == 2);
  CHECK(bob_runtime->chat->timeline.size() == 2);
  CHECK(bob_runtime->chat->journal.size() == 2);

  CHECK(alice_runtime->chat->timeline[0].kind == ChatEntryKind::kClientJoined);
  CHECK(alice_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(alice_runtime->chat->timeline[1].text == "Hello everyone");
  CHECK(alice_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(alice_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(alice_runtime->chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(alice_runtime->chat->journal[1].time ==
        ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(alice_runtime->chat->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(alice_runtime->chat->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(alice_runtime->chat->journal[0].FindRecipient(support_id)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[0].FindRecipient(bob_id)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[1].FindRecipient(support_id)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(alice_runtime->chat->journal[1].FindRecipient(bob_id)->delivery_status ==
        DeliveryStatus::kDelivered);

  CHECK(support_runtime->chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(support_runtime->chat->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(support_runtime->chat->journal[0].recipients.empty());
  CHECK(support_runtime->chat->journal[1].recipients.empty());
  CHECK(bob_runtime->chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(bob_runtime->chat->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(bob_runtime->chat->journal[0].recipients.empty());
  CHECK(bob_runtime->chat->journal[1].recipients.empty());

  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[0]));
  CHECK(alice_runtime->chat->presenter->IsLocal(
      alice_runtime->chat->timeline[1]));
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[0]));
  CHECK(!support_runtime->chat->presenter->IsLocal(
      support_runtime->chat->timeline[1]));
  CHECK(!bob_runtime->chat->presenter->IsLocal(bob_runtime->chat->timeline[0]));
  CHECK(!bob_runtime->chat->presenter->IsLocal(bob_runtime->chat->timeline[1]));
  CHECK(support_runtime->chat->presenter->client.Load().get() ==
        support_address);
  CHECK(bob_runtime->chat->presenter->client.Load().get() == bob_address);

  CHECK(CountPending(scanner, support_runtime, bob_id) == 0);
  CHECK(CountPending(scanner, bob_runtime, support_id) == 0);
  ae::RamDomainStorage forward_message_transfer_storage;
  ae::Domain forward_message_domain{ae::Now(),
                                    forward_message_transfer_storage};
  JournalMessageReceiver forward_message_receiver;
  LoopbackJournalMessageTransport forward_message_transport{
      forward_message_domain, forward_message_transfer_storage, bob_domain,
      bob_storage, forward_message_receiver};
  GraphSynchronizer forward_message_support_to_bob{
      bob_id, forward_message_domain, forward_message_transport};
  forward_message_support_to_bob.Synchronize(support_runtime);
  CHECK(forward_message_transport.send_count == 0);

  LoopbackJournalMessageTransport forward_message_bob_to_support{
      forward_message_domain, forward_message_transfer_storage, support_domain,
      support_storage, forward_message_receiver};
  GraphSynchronizer forward_message_bob_sync{
      support_id, forward_message_domain, forward_message_bob_to_support};
  auto const support_timeline_before = support_runtime->chat->timeline.size();
  forward_message_bob_sync.Synchronize(bob_runtime);
  CHECK(forward_message_bob_to_support.send_count == 0);
  CHECK(support_runtime->chat->timeline.size() == support_timeline_before);

  CHECK(CountPending(scanner, alice_runtime, support_id) == 0);
  CHECK(CountPending(scanner, alice_runtime, bob_id) == 0);
  CHECK(CountPending(scanner, support_runtime, alice_id) == 0);
  CHECK(CountPending(scanner, support_runtime, bob_id) == 0);
  CHECK(CountPending(scanner, bob_runtime, alice_id) == 0);
  CHECK(CountPending(scanner, bob_runtime, support_id) == 0);

  alice_runtime.Save();
  support_runtime.Save();
  bob_runtime.Save();

  ae::Domain alice_reload_domain{ae::Now(), alice_storage};
  ae::Domain support_reload_domain{ae::Now(), support_storage};
  ae::Domain bob_reload_domain{ae::Now(), bob_storage};

  Runtime::ptr reloaded_alice_runtime =
      Runtime::ptr::Declare(ae::CreateWith{alice_reload_domain}.with_id(1));
  reloaded_alice_runtime.Load();
  Runtime::ptr reloaded_support_runtime =
      Runtime::ptr::Declare(ae::CreateWith{support_reload_domain}.with_id(1));
  reloaded_support_runtime.Load();
  Runtime::ptr reloaded_bob_runtime =
      Runtime::ptr::Declare(ae::CreateWith{bob_reload_domain}.with_id(1));
  reloaded_bob_runtime.Load();

  auto reloaded_alice_chat = reloaded_alice_runtime->chat;
  auto reloaded_alice = reloaded_alice_chat->presenter->client;
  CHECK(reloaded_alice.is_loaded());
  CHECK(reloaded_alice.id() == alice_id);
  CHECK(reloaded_alice->name == "Alice");
  CHECK(reloaded_alice_chat->timeline.size() == 2);
  CHECK(reloaded_alice_chat->journal.size() == 2);
  CHECK(reloaded_alice_chat->journal[0].origin == EventRecordOrigin::kLocal);
  CHECK(reloaded_alice_chat->journal[1].origin == EventRecordOrigin::kLocal);
  CHECK(reloaded_alice_chat->journal[0].recipients.size() == 2);
  CHECK(reloaded_alice_chat->journal[1].recipients.size() == 2);
  CHECK(reloaded_alice_chat->journal[0].FindRecipient(support_id)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(reloaded_alice_chat->journal[0].FindRecipient(bob_id)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_alice_chat->journal[1].FindRecipient(support_id)
            ->delivery_status == DeliveryStatus::kDelivered);
  CHECK(reloaded_alice_chat->journal[1].FindRecipient(bob_id)->delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(reloaded_alice_chat->presenter->IsLocal(
      reloaded_alice_chat->timeline[0]));
  CHECK(reloaded_alice_chat->presenter->IsLocal(
      reloaded_alice_chat->timeline[1]));

  auto reloaded_support_chat = reloaded_support_runtime->chat;
  auto reloaded_support = reloaded_support_chat->presenter->client;
  CHECK(reloaded_support.is_loaded());
  CHECK(reloaded_support.id() == support_id);
  CHECK(reloaded_support->name == "Support");
  CHECK(reloaded_support_chat->timeline.size() == 2);
  CHECK(reloaded_support_chat->journal.size() == 2);
  CHECK(reloaded_support_chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(reloaded_support_chat->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(reloaded_support_chat->journal[0].recipients.empty());
  CHECK(reloaded_support_chat->journal[1].recipients.empty());
  CHECK(!reloaded_support_chat->presenter->IsLocal(
      reloaded_support_chat->timeline[0]));
  CHECK(!reloaded_support_chat->presenter->IsLocal(
      reloaded_support_chat->timeline[1]));

  auto reloaded_bob_chat = reloaded_bob_runtime->chat;
  auto reloaded_bob = reloaded_bob_chat->presenter->client;
  CHECK(reloaded_bob.is_loaded());
  CHECK(reloaded_bob.id() == bob_id);
  CHECK(reloaded_bob->name == "Bob");
  CHECK(reloaded_bob_chat->timeline.size() == 2);
  CHECK(reloaded_bob_chat->journal.size() == 2);
  CHECK(reloaded_bob_chat->journal[0].origin == EventRecordOrigin::kRemote);
  CHECK(reloaded_bob_chat->journal[1].origin == EventRecordOrigin::kRemote);
  CHECK(reloaded_bob_chat->journal[0].recipients.empty());
  CHECK(reloaded_bob_chat->journal[1].recipients.empty());
  CHECK(!reloaded_bob_chat->presenter->IsLocal(reloaded_bob_chat->timeline[0]));
  CHECK(!reloaded_bob_chat->presenter->IsLocal(reloaded_bob_chat->timeline[1]));

  auto reloaded_support_remote_alice = reloaded_support_chat->timeline[0].client;
  CHECK(reloaded_support_remote_alice.is_loaded());
  reloaded_support_chat->timeline[1].client.Load();
  auto* reloaded_support_join =
      reloaded_support_chat->journal[0].event.Load().as<ClientJoinedEvent>();
  auto* reloaded_support_msg =
      reloaded_support_chat->journal[1].event.Load().as<SendMessageEvent>();
  CHECK(reloaded_support_join != nullptr);
  CHECK(reloaded_support_msg != nullptr);
  reloaded_support_msg->sender.Load();
  CHECK(reloaded_support_chat->timeline[0].client.Load().get() ==
        reloaded_support_remote_alice.Load().get());
  CHECK(reloaded_support_chat->timeline[1].client.Load().get() ==
        reloaded_support_remote_alice.Load().get());
  CHECK(reloaded_support_join->client.Load().get() ==
        reloaded_support_remote_alice.Load().get());
  CHECK(reloaded_support_msg->sender.Load().get() ==
        reloaded_support_remote_alice.Load().get());

  auto reloaded_bob_remote_alice = reloaded_bob_chat->timeline[0].client;
  CHECK(reloaded_bob_remote_alice.is_loaded());
  reloaded_bob_chat->timeline[1].client.Load();
  auto* reloaded_bob_join =
      reloaded_bob_chat->journal[0].event.Load().as<ClientJoinedEvent>();
  auto* reloaded_bob_msg =
      reloaded_bob_chat->journal[1].event.Load().as<SendMessageEvent>();
  CHECK(reloaded_bob_join != nullptr);
  CHECK(reloaded_bob_msg != nullptr);
  reloaded_bob_msg->sender.Load();
  CHECK(reloaded_bob_chat->timeline[0].client.Load().get() ==
        reloaded_bob_remote_alice.Load().get());
  CHECK(reloaded_bob_chat->timeline[1].client.Load().get() ==
        reloaded_bob_remote_alice.Load().get());
  CHECK(reloaded_bob_join->client.Load().get() ==
        reloaded_bob_remote_alice.Load().get());
  CHECK(reloaded_bob_msg->sender.Load().get() ==
        reloaded_bob_remote_alice.Load().get());

  CHECK(reloaded_support_remote_alice.id() == alice_id);
  CHECK(reloaded_bob_remote_alice.id() == alice_id);
  CHECK(reloaded_support_remote_alice.Load().get() !=
        reloaded_bob_remote_alice.Load().get());
  CHECK(reloaded_support_remote_alice.Load().get() !=
        reloaded_alice.Load().get());
  CHECK(reloaded_bob_remote_alice.Load().get() != reloaded_alice.Load().get());

  CHECK(reloaded_alice_runtime.Load().get() !=
        reloaded_support_runtime.Load().get());
  CHECK(reloaded_alice_runtime.Load().get() != reloaded_bob_runtime.Load().get());
  CHECK(reloaded_support_runtime.Load().get() !=
        reloaded_bob_runtime.Load().get());
  CHECK(reloaded_alice_chat.Load().get() != reloaded_support_chat.Load().get());
  CHECK(reloaded_alice_chat.Load().get() != reloaded_bob_chat.Load().get());
  CHECK(reloaded_support_chat.Load().get() != reloaded_bob_chat.Load().get());
  CHECK(reloaded_alice.Load().get() != reloaded_support.Load().get());
  CHECK(reloaded_alice.Load().get() != reloaded_bob.Load().get());
  CHECK(reloaded_support.Load().get() != reloaded_bob.Load().get());
  CHECK(reloaded_alice_runtime.Load().get() != alice_runtime_address);
  CHECK(reloaded_support_runtime.Load().get() != support_runtime_address);
  CHECK(reloaded_bob_runtime.Load().get() != bob_runtime_address);
  CHECK(reloaded_alice_chat.Load().get() != alice_chat_address);
  CHECK(reloaded_support_chat.Load().get() != support_chat_address);
  CHECK(reloaded_bob_chat.Load().get() != bob_chat_address);

  auto* alice_join_reload =
      reloaded_alice_chat->journal[0].event.Load().as<ClientJoinedEvent>();
  auto* alice_msg_reload =
      reloaded_alice_chat->journal[1].event.Load().as<SendMessageEvent>();
  CHECK(alice_join_reload != nullptr);
  CHECK(alice_msg_reload != nullptr);
  CHECK(alice_join_reload != reloaded_support_join);
  CHECK(alice_join_reload != reloaded_bob_join);
  CHECK(alice_msg_reload != reloaded_support_msg);
  CHECK(alice_msg_reload != reloaded_bob_msg);
  CHECK(alice_join_reload->obj_id.id() == 200);
  CHECK(reloaded_support_join->obj_id.id() == 200);
  CHECK(reloaded_bob_join->obj_id.id() == 200);
  CHECK(alice_msg_reload->obj_id.id() == 201);
  CHECK(reloaded_support_msg->obj_id.id() == 201);
  CHECK(reloaded_bob_msg->obj_id.id() == 201);

  return EXIT_SUCCESS;
}
