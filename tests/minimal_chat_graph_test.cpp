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

  Client::ptr Instantiate(std::string instance_name,
                          ae::ObjPtr<Chat> instance_chat);
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

static_assert(!std::is_base_of_v<apptraverse::Node, ChatPresenter>);
static_assert(!std::is_base_of_v<apptraverse::Node, ClientPresenter>);

Client::ptr Client::Instantiate(std::string instance_name,
                                ae::ObjPtr<Chat> instance_chat) {
  assert(domain != nullptr);
  assert(obj_id.IsValid());
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(presenter.is_valid());
  assert(presenter.is_loaded());
  assert(resource.is_valid());
  assert(resource.is_loaded());
  assert(journal.empty());
  assert(instance_chat.is_valid());
  assert(instance_chat.is_loaded());
  assert(instance_chat.domain() == domain);

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
  instance->chat = instance_chat;
  instance->name = std::move(instance_name);
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

  ClientJoinedEvent::ptr event =
      ClientJoinedEvent::ptr::Create(ae::CreateWith{*domain}.with_id(event_id));
  event->client = client;
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
  event->text = std::move(text);
  chat->CommitFromPresenter(event, time);
}

void ClientPresenter::Rename(std::string name, ae::ObjId event_id,
                             ae::TimePoint time) {
  assert(domain != nullptr);
  assert(client.is_valid());
  assert(client.is_loaded());
  assert(client.domain() == domain);
  assert(event_id.IsValid());

  RenameClientEvent::ptr event =
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

    auto result =
        client_prefab->Instantiate(std::move(name), chat);
    chat->presenter->SetClient(result);
    return result;
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

using PendingKey = std::pair<ae::ObjId::Type, ae::ObjId::Type>;

}  // namespace

int main() {
  using apptraverse::DeliveryStatus;
  using apptraverse::GraphJournalScanner;
  using apptraverse::test::Chat;
  using apptraverse::test::ChatEntryKind;
  using apptraverse::test::ChatPresenter;
  using apptraverse::test::Client;
  using apptraverse::test::ClientJoinedEvent;
  using apptraverse::test::ClientPresenter;
  using apptraverse::test::RenameClientEvent;
  using apptraverse::test::Runtime;
  using apptraverse::test::SendMessageEvent;
  using apptraverse::test::SharedResource;

  ae::RamDomainStorage storage;
  ae::Domain distillation_domain{ae::Now(), storage};
  CHECK(storage.state.empty());

  ChatPresenter::ptr chat_presenter = ChatPresenter::ptr::Create(
      ae::CreateWith{distillation_domain}.with_id(102));
  CHECK(static_cast<bool>(chat_presenter));
  chat_presenter->caption = "Chat presenter";

  Chat::ptr chat_base =
      Chat::ptr::Create(ae::CreateWith{distillation_domain}.with_id(101));
  CHECK(static_cast<bool>(chat_base));
  CHECK(!chat_base->base.is_valid());
  CHECK(chat_base->journal.empty());
  CHECK(chat_base->timeline.empty());
  CHECK(!chat_base->presenter.is_valid());

  Chat::ptr chat =
      Chat::ptr::Create(ae::CreateWith{distillation_domain}.with_id(100));
  CHECK(static_cast<bool>(chat));
  chat->base = chat_base;
  chat->presenter = chat_presenter;
  chat_presenter->chat = chat;
  CHECK(!chat_presenter->client.is_valid());

  chat->CaptureBaseStateForTest();

  auto* captured_chat_base = chat->base.Load().as<Chat>();
  CHECK(captured_chat_base != nullptr);
  CHECK(captured_chat_base->timeline.empty());
  CHECK(captured_chat_base->journal.empty());
  CHECK(captured_chat_base->presenter.Load().get() ==
        chat_presenter.Load().get());
  CHECK(chat_presenter->chat.Load().get() == chat.Load().get());
  CHECK(!chat_presenter->client.is_valid());

  SharedResource::ptr resource = SharedResource::ptr::Create(
      ae::CreateWith{distillation_domain}.with_id(50));
  CHECK(static_cast<bool>(resource));
  resource->value = "Default client resource";

  ClientPresenter::ptr client_presenter_prefab = ClientPresenter::ptr::Create(
      ae::CreateWith{distillation_domain}.with_id(12));
  CHECK(static_cast<bool>(client_presenter_prefab));
  client_presenter_prefab->caption = "Client presenter";

  Client::ptr client_base_prefab =
      Client::ptr::Create(ae::CreateWith{distillation_domain}.with_id(11));
  CHECK(static_cast<bool>(client_base_prefab));
  client_base_prefab->name = "";
  CHECK(!client_base_prefab->base.is_valid());
  CHECK(client_base_prefab->journal.empty());
  CHECK(!client_base_prefab->chat.is_valid());
  CHECK(!client_base_prefab->presenter.is_valid());
  CHECK(!client_base_prefab->resource.is_valid());

  Client::ptr client_prefab =
      Client::ptr::Create(ae::CreateWith{distillation_domain}.with_id(10));
  CHECK(static_cast<bool>(client_prefab));
  client_prefab->name = "";
  client_prefab->base = client_base_prefab;
  client_prefab->presenter = client_presenter_prefab;
  client_prefab->resource = resource;
  CHECK(!client_prefab->chat.is_valid());
  client_presenter_prefab->client = client_prefab;

  client_prefab->CapturePrefabBaseForTest();

  auto* captured_client_base = client_prefab->base.Load().as<Client>();
  CHECK(captured_client_base != nullptr);
  CHECK(captured_client_base->name.empty());
  CHECK(!captured_client_base->chat.is_valid());
  CHECK(captured_client_base->presenter.id().id() == 12);
  CHECK(captured_client_base->resource.id().id() == 50);
  CHECK(client_presenter_prefab->client.Load().get() ==
        client_prefab.Load().get());
  CHECK(client_prefab->journal.empty());
  CHECK(captured_client_base->journal.empty());

  Runtime::ptr runtime =
      Runtime::ptr::Create(ae::CreateWith{distillation_domain}.with_id(1));
  CHECK(static_cast<bool>(runtime));
  runtime->chat = chat;
  runtime->client_prefab = client_prefab;
  runtime.Save();

  CHECK(storage.state.size() == 8);
  CHECK(ContainsObj(storage, 1));
  CHECK(ContainsObj(storage, 10));
  CHECK(ContainsObj(storage, 11));
  CHECK(ContainsObj(storage, 12));
  CHECK(ContainsObj(storage, 50));
  CHECK(ContainsObj(storage, 100));
  CHECK(ContainsObj(storage, 101));
  CHECK(ContainsObj(storage, 102));

  ae::Domain runtime_domain{ae::Now(), storage};
  Runtime::ptr loaded_runtime =
      Runtime::ptr::Declare(ae::CreateWith{runtime_domain}.with_id(1));
  loaded_runtime.Load();

  CHECK(loaded_runtime.is_loaded());
  CHECK(loaded_runtime->chat.is_loaded());
  CHECK(loaded_runtime->chat.id().id() == 100);
  CHECK(loaded_runtime->chat->base.id().id() == 101);
  CHECK(loaded_runtime->chat->base.is_loaded());
  CHECK(loaded_runtime->chat->presenter.id().id() == 102);
  CHECK(loaded_runtime->chat->presenter.is_loaded());
  CHECK(loaded_runtime->chat->presenter->chat.Load().get() ==
        loaded_runtime->chat.Load().get());
  CHECK(!loaded_runtime->chat->presenter->client.is_valid());
  CHECK(loaded_runtime->chat->timeline.empty());
  CHECK(loaded_runtime->chat->journal.empty());

  CHECK(loaded_runtime->client_prefab.is_loaded());
  CHECK(loaded_runtime->client_prefab.id().id() == 10);
  CHECK(loaded_runtime->client_prefab->name.empty());
  CHECK(loaded_runtime->client_prefab->base.id().id() == 11);
  CHECK(loaded_runtime->client_prefab->presenter.id().id() == 12);
  CHECK(loaded_runtime->client_prefab->resource.id().id() == 50);
  CHECK(!loaded_runtime->client_prefab->chat.is_valid());
  CHECK(loaded_runtime->client_prefab->journal.empty());
  CHECK(loaded_runtime->client_prefab->presenter->client.Load().get() ==
        loaded_runtime->client_prefab.Load().get());

  auto* runtime_chat_address = loaded_runtime->chat.Load().get();
  auto* runtime_chat_presenter_address =
      loaded_runtime->chat->presenter.Load().get();
  auto* prefab_address = loaded_runtime->client_prefab.Load().get();
  auto* prefab_resource_address =
      loaded_runtime->client_prefab->resource.Load().get();
  auto* runtime_address = loaded_runtime.Load().get();
  CHECK(runtime_chat_address != nullptr);
  CHECK(runtime_chat_presenter_address != nullptr);
  CHECK(prefab_address != nullptr);
  CHECK(prefab_resource_address != nullptr);

  auto alice = loaded_runtime->CreateLocalClient("Alice");
  CHECK(alice.is_valid());
  CHECK(alice.is_loaded());
  CHECK(alice->name == "Alice");
  CHECK(alice->journal.empty());
  CHECK(alice->chat.id().id() == 100);
  CHECK(alice->chat.Load().get() == runtime_chat_address);
  CHECK(alice->presenter->client.Load().get() == alice.Load().get());
  CHECK(loaded_runtime->chat->presenter->client.Load().get() ==
        alice.Load().get());

  auto* alice_address = alice.Load().get();
  auto const alice_id = alice.id();
  auto const alice_base_id = alice->base.id();
  auto const alice_presenter_id = alice->presenter.id();

  auto* alice_base = alice->base.Load().as<Client>();
  CHECK(alice_base != nullptr);
  CHECK(alice_base->name == "Alice");
  CHECK(alice_base->chat.Load().get() == runtime_chat_address);
  CHECK(alice_base->presenter.Load().get() == alice->presenter.Load().get());
  CHECK(alice->resource.id().id() == 50);
  CHECK(alice->resource.Load().get() == prefab_resource_address);

  loaded_runtime->chat->presenter->Join(
      ae::ObjId{200}, ae::TimePoint{std::chrono::microseconds{100}});

  CHECK(loaded_runtime->chat->timeline.size() == 1);
  CHECK(loaded_runtime->chat->timeline[0].kind == ChatEntryKind::kClientJoined);
  CHECK(loaded_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[0].text.empty());
  CHECK(loaded_runtime->chat->journal.size() == 1);
  CHECK(loaded_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(loaded_runtime->chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(loaded_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kPending);
  auto* join_event =
      loaded_runtime->chat->journal[0].event.Load().as<ClientJoinedEvent>();
  CHECK(join_event != nullptr);
  CHECK(join_event->client.Load().get() == alice_address);

  loaded_runtime->chat->presenter->Send(
      "second", ae::ObjId{201},
      ae::TimePoint{std::chrono::microseconds{300}});

  CHECK(loaded_runtime->chat->timeline.size() == 2);
  CHECK(loaded_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(loaded_runtime->chat->timeline[1].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[1].text == "second");
  CHECK(loaded_runtime->chat->journal.size() == 2);
  CHECK(loaded_runtime->chat->journal[1].event.id().id() == 201);
  CHECK(loaded_runtime->chat->journal[1].time ==
        ae::TimePoint{std::chrono::microseconds{300}});
  CHECK(loaded_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kPending);

  SendMessageEvent::ptr remote_first_message = SendMessageEvent::ptr::Create(
      ae::CreateWith{runtime_domain}.with_id(203));
  CHECK(static_cast<bool>(remote_first_message));
  remote_first_message->sender = loaded_runtime->chat->presenter->client;
  remote_first_message->text = "first";
  ae::TimePoint const remote_time{std::chrono::microseconds{200}};

  apptraverse::Node::ptr generic_chat = loaded_runtime->chat;
  generic_chat->AcceptRemoteEvent(remote_first_message, remote_time);

  CHECK(loaded_runtime->chat->timeline.size() == 3);
  CHECK(loaded_runtime->chat->timeline[0].kind == ChatEntryKind::kClientJoined);
  CHECK(loaded_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[0].text.empty());
  CHECK(loaded_runtime->chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(loaded_runtime->chat->timeline[1].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[1].text == "first");
  CHECK(loaded_runtime->chat->timeline[2].kind == ChatEntryKind::kMessage);
  CHECK(loaded_runtime->chat->timeline[2].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[2].text == "second");

  CHECK(loaded_runtime->chat->journal.size() == 3);
  CHECK(loaded_runtime->chat->journal[0].event.id().id() == 200);
  CHECK(loaded_runtime->chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(loaded_runtime->chat->journal[0].delivery_status ==
        DeliveryStatus::kPending);
  CHECK(loaded_runtime->chat->journal[1].event.id().id() == 203);
  CHECK(loaded_runtime->chat->journal[1].time == remote_time);
  CHECK(loaded_runtime->chat->journal[1].delivery_status ==
        DeliveryStatus::kDelivered);
  CHECK(loaded_runtime->chat->journal[2].event.id().id() == 201);
  CHECK(loaded_runtime->chat->journal[2].time ==
        ae::TimePoint{std::chrono::microseconds{300}});
  CHECK(loaded_runtime->chat->journal[2].delivery_status ==
        DeliveryStatus::kPending);

  CHECK(loaded_runtime->chat->presenter.Load().get() ==
        runtime_chat_presenter_address);
  CHECK(loaded_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->presenter->chat.Load().get() ==
        runtime_chat_address);

  auto* chat_base_after_rebuild = loaded_runtime->chat->base.Load().as<Chat>();
  CHECK(chat_base_after_rebuild != nullptr);
  CHECK(chat_base_after_rebuild->timeline.empty());
  CHECK(chat_base_after_rebuild->journal.empty());

  CHECK(loaded_runtime->chat->presenter->IsLocal(
      loaded_runtime->chat->timeline[1]));
  CHECK(loaded_runtime->chat->presenter->IsLocal(
      loaded_runtime->chat->timeline[2]));

  alice->presenter->Rename("Alice Cooper", ae::ObjId{202},
                           ae::TimePoint{std::chrono::microseconds{100}});

  CHECK(alice->name == "Alice Cooper");
  CHECK(alice->journal.size() == 1);
  CHECK(alice->journal[0].event.id().id() == 202);
  CHECK(alice->journal[0].delivery_status == DeliveryStatus::kPending);
  CHECK(alice_base->name == "Alice");
  CHECK(loaded_runtime->chat->journal.size() == 3);
  CHECK(loaded_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[1].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[2].client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[0].client->name == "Alice Cooper");
  CHECK(loaded_runtime->chat->timeline[1].client->name == "Alice Cooper");
  CHECK(loaded_runtime->chat->timeline[2].client->name == "Alice Cooper");

  GraphJournalScanner scanner;
  std::set<PendingKey> pending;
  scanner.VisitPending(loaded_runtime, [&](apptraverse::Node& node,
                                           apptraverse::EventRecord& record) {
    pending.emplace(node.obj_id.id(), record.event.id().id());
  });
  CHECK(pending.size() == 3);
  CHECK(pending.count(PendingKey{100, 200}) == 1);
  CHECK(pending.count(PendingKey{100, 201}) == 1);
  CHECK(pending.count(PendingKey{alice_id.id(), 202}) == 1);
  CHECK(pending.count(PendingKey{100, 203}) == 0);

  alice.Reset();
  CHECK(!alice.is_loaded());
  CHECK(loaded_runtime->chat->presenter->client.is_loaded());
  CHECK(loaded_runtime->chat->timeline[0].client.is_loaded());
  CHECK(join_event->client.is_loaded());
  CHECK(loaded_runtime->chat->presenter->client.Load().get() == alice_address);
  CHECK(loaded_runtime->chat->timeline[0].client.Load().get() == alice_address);
  CHECK(join_event->client.Load().get() == alice_address);

  CHECK(!ContainsObj(storage, alice_id.id()));
  CHECK(ContainsObj(storage, alice_base_id.id()));
  CHECK(ContainsObj(storage, alice_presenter_id.id()));
  CHECK(!ContainsObj(storage, 200));
  CHECK(!ContainsObj(storage, 201));
  CHECK(!ContainsObj(storage, 202));
  CHECK(!ContainsObj(storage, 203));

  loaded_runtime.Save();

  CHECK(storage.state.size() == 15);
  CHECK(ContainsObj(storage, 1));
  CHECK(ContainsObj(storage, 100));
  CHECK(ContainsObj(storage, 101));
  CHECK(ContainsObj(storage, 102));
  CHECK(ContainsObj(storage, 10));
  CHECK(ContainsObj(storage, 11));
  CHECK(ContainsObj(storage, 12));
  CHECK(ContainsObj(storage, 50));
  CHECK(ContainsObj(storage, alice_id.id()));
  CHECK(ContainsObj(storage, alice_base_id.id()));
  CHECK(ContainsObj(storage, alice_presenter_id.id()));
  CHECK(ContainsObj(storage, 200));
  CHECK(ContainsObj(storage, 201));
  CHECK(ContainsObj(storage, 202));
  CHECK(ContainsObj(storage, 203));

  ae::Domain reload_domain{ae::Now(), storage};
  Runtime::ptr reloaded_runtime =
      Runtime::ptr::Declare(ae::CreateWith{reload_domain}.with_id(1));
  reloaded_runtime.Load();

  CHECK(reloaded_runtime.is_loaded());
  CHECK(reloaded_runtime->chat.id().id() == 100);
  CHECK(reloaded_runtime->client_prefab.id().id() == 10);

  auto loaded_chat = reloaded_runtime->chat;
  CHECK(loaded_chat.is_loaded());
  CHECK(loaded_chat->timeline.size() == 3);
  CHECK(loaded_chat->journal.size() == 3);
  CHECK(loaded_chat->timeline[0].kind == ChatEntryKind::kClientJoined);
  CHECK(loaded_chat->timeline[0].text.empty());
  CHECK(loaded_chat->timeline[1].kind == ChatEntryKind::kMessage);
  CHECK(loaded_chat->timeline[1].text == "first");
  CHECK(loaded_chat->timeline[2].kind == ChatEntryKind::kMessage);
  CHECK(loaded_chat->timeline[2].text == "second");
  CHECK(loaded_chat->journal[0].event.id().id() == 200);
  CHECK(loaded_chat->journal[1].event.id().id() == 203);
  CHECK(loaded_chat->journal[2].event.id().id() == 201);
  CHECK(loaded_chat->journal[0].time ==
        ae::TimePoint{std::chrono::microseconds{100}});
  CHECK(loaded_chat->journal[1].time ==
        ae::TimePoint{std::chrono::microseconds{200}});
  CHECK(loaded_chat->journal[2].time ==
        ae::TimePoint{std::chrono::microseconds{300}});
  CHECK(loaded_chat->journal[0].delivery_status == DeliveryStatus::kPending);
  CHECK(loaded_chat->journal[1].delivery_status == DeliveryStatus::kDelivered);
  CHECK(loaded_chat->journal[2].delivery_status == DeliveryStatus::kPending);

  auto loaded_alice = loaded_chat->presenter->client;
  CHECK(loaded_alice.is_valid());
  CHECK(loaded_alice.is_loaded());
  CHECK(loaded_alice.id() == alice_id);
  CHECK(loaded_alice->name == "Alice Cooper");

  auto* loaded_alice_address = loaded_alice.Load().get();
  CHECK(loaded_alice_address != nullptr);
  CHECK(loaded_chat->timeline[0].client.Load().get() == loaded_alice_address);
  CHECK(loaded_chat->timeline[1].client.Load().get() == loaded_alice_address);
  CHECK(loaded_chat->timeline[2].client.Load().get() == loaded_alice_address);

  auto* loaded_join =
      loaded_chat->journal[0].event.Load().as<ClientJoinedEvent>();
  auto* loaded_first =
      loaded_chat->journal[1].event.Load().as<SendMessageEvent>();
  auto* loaded_second =
      loaded_chat->journal[2].event.Load().as<SendMessageEvent>();
  CHECK(loaded_join != nullptr);
  CHECK(loaded_first != nullptr);
  CHECK(loaded_second != nullptr);
  CHECK(loaded_join->client.Load().get() == loaded_alice_address);
  CHECK(loaded_first->sender.Load().get() == loaded_alice_address);
  CHECK(loaded_second->sender.Load().get() == loaded_alice_address);
  CHECK(loaded_join->client.id() == alice_id);
  CHECK(loaded_first->sender.id() == alice_id);
  CHECK(loaded_second->sender.id() == alice_id);
  CHECK(loaded_chat->timeline[0].client.id() == alice_id);
  CHECK(loaded_chat->timeline[1].client.id() == alice_id);
  CHECK(loaded_chat->timeline[2].client.id() == alice_id);
  CHECK(loaded_chat->presenter->client.id() == alice_id);

  CHECK(loaded_alice->name == "Alice Cooper");
  CHECK(loaded_alice->base.id() == alice_base_id);
  auto* loaded_alice_base = loaded_alice->base.Load().as<Client>();
  CHECK(loaded_alice_base != nullptr);
  CHECK(loaded_alice_base->name == "Alice");
  CHECK(loaded_alice->journal.size() == 1);
  CHECK(loaded_alice->journal[0].event.id().id() == 202);
  CHECK(loaded_alice->journal[0].delivery_status == DeliveryStatus::kPending);
  auto* loaded_rename =
      loaded_alice->journal[0].event.Load().as<RenameClientEvent>();
  CHECK(loaded_rename != nullptr);
  CHECK(loaded_rename->name == "Alice Cooper");
  CHECK(loaded_alice->chat.Load().get() == loaded_chat.Load().get());
  CHECK(loaded_alice_base->chat.Load().get() == loaded_chat.Load().get());
  CHECK(loaded_alice->presenter.id() == alice_presenter_id);
  CHECK(loaded_alice->presenter->client.Load().get() == loaded_alice_address);
  CHECK(loaded_alice_base->presenter.Load().get() ==
        loaded_alice->presenter.Load().get());
  CHECK(loaded_alice->resource.id().id() == 50);
  CHECK(loaded_alice->resource.Load().get() ==
        reloaded_runtime->client_prefab->resource.Load().get());

  CHECK(loaded_chat->presenter->IsLocal(loaded_chat->timeline[1]));
  CHECK(loaded_chat->presenter->IsLocal(loaded_chat->timeline[2]));

  auto* loaded_chat_base = loaded_chat->base.Load().as<Chat>();
  CHECK(loaded_chat_base != nullptr);
  CHECK(loaded_chat_base->timeline.empty());
  CHECK(loaded_chat_base->journal.empty());
  CHECK(loaded_chat_base->presenter.id().id() == 102);
  CHECK(loaded_chat_base->presenter.Load().get() ==
        loaded_chat->presenter.Load().get());
  CHECK(loaded_chat_base->presenter->client.Load().get() ==
        loaded_alice_address);
  CHECK(loaded_chat_base->presenter->chat.Load().get() ==
        loaded_chat.Load().get());
  CHECK(loaded_chat_base->presenter->chat.Load().get() != loaded_chat_base);

  CHECK(reloaded_runtime->client_prefab.id().id() == 10);
  CHECK(reloaded_runtime->client_prefab->name.empty());
  CHECK(reloaded_runtime->client_prefab->base.id().id() == 11);
  CHECK(reloaded_runtime->client_prefab->presenter.id().id() == 12);
  CHECK(reloaded_runtime->client_prefab->resource.id().id() == 50);
  CHECK(!reloaded_runtime->client_prefab->chat.is_valid());
  CHECK(reloaded_runtime->client_prefab->journal.empty());
  CHECK(reloaded_runtime->client_prefab->presenter->client.Load().get() ==
        reloaded_runtime->client_prefab.Load().get());
  CHECK(reloaded_runtime->client_prefab->presenter->client.Load().get() !=
        loaded_alice_address);

  CHECK(loaded_alice_address != alice_address);
  CHECK(loaded_chat.Load().get() != runtime_chat_address);
  CHECK(reloaded_runtime.Load().get() != runtime_address);
  CHECK(loaded_chat->presenter->client.Load().get() == loaded_alice_address);
  CHECK(loaded_chat->timeline[0].client.Load().get() ==
        loaded_chat->presenter->client.Load().get());

  return EXIT_SUCCESS;
}
