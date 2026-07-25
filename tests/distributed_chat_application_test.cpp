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
#include "apptraverse/event_record.h"
#include "apptraverse/graph_journal_scanner.h"
#include "apptraverse/graph_synchronizer.h"
#include "apptraverse/journal_event_message.h"
#include "apptraverse/journal_message_receiver.h"
#include "apptraverse/journal_message_transport.h"
#include "apptraverse/journal_transport_message.h"
#include "apptraverse/node_for.h"

namespace apptraverse::test {

enum class TestObjectId : std::uint32_t {
  kApplication = 1,
  kChatUserPrefab = 10,
  kChatUserBasePrefab = 11,
  kChatUserPresenterPrefab = 12,
  kChat = 100,
  kChatBase = 101,
  kChatPresenter = 102,
  kAliceJoinEvent = 200,
  kSupportJoinEvent = 201,
  kBobJoinEvent = 202,
  kAliceMessageEvent = 300,
  kSupportReplyEvent = 301,
};

enum class TestRouteId : std::uint32_t {
  kAlice = 7001,
  kSupport = 7002,
  kBob = 7003,
};

constexpr ae::ObjId ToObjId(TestObjectId id) {
  return ae::ObjId{static_cast<ae::ObjId::Type>(id)};
}
constexpr ae::ObjId ToObjId(TestRouteId id) {
  return ae::ObjId{static_cast<ae::ObjId::Type>(id)};
}

class Application;
class Chat;
class ChatPresenter;
class ChatUser;
class ChatUserPresenter;
class UserJoinedEvent;
class MessageEvent;

enum class ChatEntryKind : std::uint8_t {
  kUserJoined,
  kMessage,
};

struct ChatEntry {
  ChatEntryKind kind{ChatEntryKind::kUserJoined};
  ae::Obj::ptr sender;
  std::string text;

  AE_REFLECT_MEMBERS(kind, sender, text)
};

class ChatUserPresenter : public ae::Obj {
  AE_OBJECT(ChatUserPresenter, Obj, 0)

 protected:
  ChatUserPresenter() = default;

 public:
  explicit ChatUserPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(user), AE_MMBR(caption))

  ae::ObjPtr<ChatUser> user;
  std::string caption;
};

static_assert(!std::is_base_of_v<apptraverse::Node, ChatUserPresenter>);

class ChatUser : public apptraverse::NodeFor<ChatUser> {
  AE_OBJECT(ChatUser, Node, 0)

 protected:
  ChatUser() = default;

 public:
  explicit ChatUser(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(chat), AE_MMBR(presenter))

  std::string name;
  ae::ObjPtr<Chat> chat;
  ChatUserPresenter::ptr presenter;

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  ChatUser::ptr Instantiate(std::string name, ae::ObjPtr<Chat> chat);

  void PrepareForIntroduction();
};

class ChatPresenter : public ae::Obj {
  AE_OBJECT(ChatPresenter, Obj, 0)

 protected:
  ChatPresenter() = default;

 public:
  explicit ChatPresenter(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat), AE_MMBR(title))

  ae::ObjPtr<Chat> chat;
  std::string title;

  void Join(ChatUser::ptr user, ae::ObjId event_id, ae::TimePoint time,
            std::vector<ae::ObjId> recipients);
  void SendMessage(ChatUser::ptr sender, std::string text, ae::ObjId event_id,
                   ae::TimePoint time, std::vector<ae::ObjId> recipients);
};

static_assert(!std::is_base_of_v<apptraverse::Node, ChatPresenter>);

class Chat : public apptraverse::NodeFor<Chat> {
  AE_OBJECT(Chat, Node, 0)

 protected:
  Chat() = default;

 public:
  explicit Chat(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(entries), AE_MMBR(presenter))

  std::vector<ChatEntry> entries;
  ChatPresenter::ptr presenter;

  void Apply(UserJoinedEvent const& event);
  void Apply(MessageEvent const& event);

  void CaptureBaseStateForTest() { CaptureBaseState(); }

  void CommitEventForTest(apptraverse::Event::ptr event, ae::TimePoint time,
                          std::vector<ae::ObjId> recipients) {
    CommitEvent(std::move(event), time, std::move(recipients));
  }
};

class UserJoinedEvent
    : public apptraverse::EventFor<Chat, UserJoinedEvent> {
  AE_OBJECT(UserJoinedEvent, Event, 0)

 protected:
  UserJoinedEvent() = default;

 public:
  explicit UserJoinedEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(user))

  ChatUser::ptr user;
};

class MessageEvent : public apptraverse::EventFor<Chat, MessageEvent> {
  AE_OBJECT(MessageEvent, Event, 0)

 protected:
  MessageEvent() = default;

 public:
  explicit MessageEvent(ae::ObjProp prop) : EventFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(text))

  std::string text;
};

void Chat::Apply(UserJoinedEvent const& event) {
  assert(event.user.is_valid());
  entries.push_back(ChatEntry{
      ChatEntryKind::kUserJoined,
      event.user,
      {},
  });
}

void Chat::Apply(MessageEvent const& event) {
  assert(event.sender.is_valid());
  entries.push_back(ChatEntry{
      ChatEntryKind::kMessage,
      event.sender,
      event.text,
  });
}

ChatUser::ptr ChatUser::Instantiate(std::string name, ae::ObjPtr<Chat> chat) {
  assert(domain != nullptr);
  assert(obj_id.IsValid());
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(presenter.is_valid());
  assert(presenter.is_loaded());
  assert(journal.empty());
  assert(chat.is_valid());
  assert(chat.is_loaded());

  auto prefab = ChatUser::ptr::MakeFromThis(this);
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
  instance_presenter->user = instance;
  instance->chat = chat;
  instance->name = std::move(name);
  assert(instance->journal.empty());

  instance->CaptureBaseState();
  return instance;
}

void ChatUser::PrepareForIntroduction() {
  assert(base.is_valid());
  assert(base.is_loaded());
  assert(presenter.is_valid());
  assert(presenter.is_loaded());
  assert(chat.is_valid());
  assert(journal.empty());

  ChatUser* concrete_base = const_cast<ChatUser*>(base.Load().as<ChatUser>());
  assert(concrete_base != nullptr);

  chat.Reset();
  chat.SetFlags(ae::ObjFlags::kUnloadedByDefault);
  concrete_base->chat.Reset();
  concrete_base->chat.SetFlags(ae::ObjFlags::kUnloadedByDefault);

  assert(chat.is_valid());
  assert(!chat.is_loaded());
  assert(concrete_base->chat.is_valid());
  assert(!concrete_base->chat.is_loaded());
}

void ChatPresenter::Join(ChatUser::ptr user, ae::ObjId event_id, ae::TimePoint time,
                         std::vector<ae::ObjId> recipients) {
  assert(domain != nullptr);
  assert(chat.is_valid());
  assert(chat.is_loaded());
  assert(user.is_valid());
  assert(user.is_loaded());
  assert(event_id.IsValid());
  for (auto const& recipient : recipients) {
    assert(recipient.IsValid());
  }

  user->PrepareForIntroduction();

  UserJoinedEvent::ptr event =
      UserJoinedEvent::ptr::Create(ae::CreateWith{*domain}.with_id(event_id));
  event->sender = user;
  event->user = user;

  assert(event->sender.is_loaded());
  assert(event->user.is_loaded());
  assert(event->sender.id() == event->user.id());
  assert(event->sequence == 0);

  chat->CommitEventForTest(event, time, std::move(recipients));

  assert(event->sender.is_valid());
  assert(!event->sender.is_loaded());
  assert(event->user.is_loaded());
  assert(event->sender.id() == event->user.id());
  assert(event->sequence != 0);
}

void ChatPresenter::SendMessage(ChatUser::ptr sender, std::string text,
                                ae::ObjId event_id, ae::TimePoint time,
                                std::vector<ae::ObjId> recipients) {
  assert(domain != nullptr);
  assert(chat.is_valid());
  assert(chat.is_loaded());
  assert(sender.is_valid());
  assert(sender.is_loaded());
  assert(event_id.IsValid());
  for (auto const& recipient : recipients) {
    assert(recipient.IsValid());
  }

  MessageEvent::ptr event =
      MessageEvent::ptr::Create(ae::CreateWith{*domain}.with_id(event_id));
  event->sender = sender;
  event->text = std::move(text);

  chat->CommitEventForTest(event, time, std::move(recipients));

  assert(event->sender.is_valid());
  assert(!event->sender.is_loaded());
  assert(event->sequence != 0);
}

class Application : public ae::Obj {
  AE_OBJECT(Application, Obj, 0)

 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(chat), AE_MMBR(user_prefab))

  Chat::ptr chat;
  ChatUser::ptr user_prefab;

  ChatUser::ptr CreateAndJoinLocalUser(std::string name, ae::ObjId event_id,
                                       ae::TimePoint time,
                                       std::vector<ae::ObjId> recipients) {
    assert(chat.is_valid());
    assert(chat.is_loaded());
    assert(user_prefab.is_valid());
    assert(user_prefab.is_loaded());
    assert(chat->presenter.is_valid());
    assert(chat->presenter.is_loaded());

    auto user = user_prefab->Instantiate(std::move(name), chat);
    chat->presenter->Join(user, event_id, time, std::move(recipients));
    return user;
  }
};

static_assert(!std::is_base_of_v<apptraverse::Node, Application>);

Application::ptr BuildInitialApplication(ae::Domain& domain) {
  ChatPresenter::ptr chat_presenter = ChatPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(TestObjectId::kChatPresenter)));
  chat_presenter->title = "Chat";

  Chat::ptr chat_base = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(TestObjectId::kChatBase)));

  Chat::ptr chat = Chat::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(TestObjectId::kChat)));
  chat->base = chat_base;
  chat->presenter = chat_presenter;
  chat_presenter->chat = chat;
  chat->CaptureBaseStateForTest();

  ChatUserPresenter::ptr user_presenter = ChatUserPresenter::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(TestObjectId::kChatUserPresenterPrefab)));
  user_presenter->caption = "ChatUser presenter";

  ChatUser::ptr user_base = ChatUser::ptr::Create(
      ae::CreateWith{domain}.with_id(
          ToObjId(TestObjectId::kChatUserBasePrefab)));
  user_base->name = "";

  ChatUser::ptr user_prefab = ChatUser::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(TestObjectId::kChatUserPrefab)));
  user_prefab->name = "";
  user_prefab->base = user_base;
  user_prefab->presenter = user_presenter;
  user_presenter->user = user_prefab;
  assert(!user_prefab->chat.is_valid());
  assert(!user_base->chat.is_valid());
  user_prefab->CaptureBaseStateForTest();

  Application::ptr application = Application::ptr::Create(
      ae::CreateWith{domain}.with_id(ToObjId(TestObjectId::kApplication)));
  application->chat = chat;
  application->user_prefab = user_prefab;
  application.Save();
  return application;
}

Application::ptr LoadApplication(ae::Domain& domain) {
  auto application = Application::ptr::Declare(
      ae::CreateWith{domain}.with_id(ToObjId(TestObjectId::kApplication)));
  application.Load();
  return application;
}

ChatUser::ptr FindJoinedUser(Chat& chat, ae::ObjId user_id) {
  for (auto& entry : chat.entries) {
    if (entry.kind != ChatEntryKind::kUserJoined) {
      continue;
    }
    if (!entry.sender.is_valid() || entry.sender.id() != user_id) {
      continue;
    }
    entry.sender.Load();
    assert(entry.sender.Load().as<ChatUser>() != nullptr);
    ChatUser::ptr found = ChatUser::ptr::Declare(
        ae::CreateWith{*entry.sender.domain()}.with_id(entry.sender.id()));
    found.Load();
    assert(found.is_loaded());
    return found;
  }
  return {};
}

apptraverse::EventRecord* FindRecord(Chat& chat, ae::ObjId event_id) {
  for (auto& record : chat.journal) {
    if (record.event.id() == event_id) {
      return &record;
    }
  }
  return nullptr;
}

apptraverse::EventRecord const* FindRecord(Chat const& chat,
                                           ae::ObjId event_id) {
  for (auto const& record : chat.journal) {
    if (record.event.id() == event_id) {
      return &record;
    }
  }
  return nullptr;
}

struct DeliveryResult {
  std::size_t send_count{0};
  std::vector<ae::ObjId> transferred_ids;
};

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

  void Send(apptraverse::JournalTransportMessage::ptr message) override {
    assert(message.is_valid());
    assert(message.is_loaded());
    assert(message.domain() == message_domain_);

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

template <typename RootPtr>
DeliveryResult DeliverPending(RootPtr& sender_root, ae::ObjId recipient,
                              ae::Domain& receiver_domain,
                              ae::RamDomainStorage& receiver_storage) {
  ae::RamDomainStorage transfer_storage;
  ae::Domain message_domain{ae::Now(), transfer_storage};
  apptraverse::JournalMessageReceiver receiver;
  LoopbackJournalMessageTransport transport{
      message_domain, transfer_storage, receiver_domain, receiver_storage,
      receiver};
  apptraverse::GraphSynchronizer synchronizer{recipient, message_domain,
                                              transport};
  synchronizer.Synchronize(sender_root);

  DeliveryResult result;
  result.send_count = transport.send_count;
  for (auto const& item : transfer_storage.state) {
    result.transferred_ids.push_back(item.first);
  }
  return result;
}

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

int CheckLocalJoin(apptraverse::test::Application::ptr& app, ae::ObjId local_id,
                   apptraverse::test::TestObjectId join_event_id) {
  using apptraverse::test::ChatUser;
  using apptraverse::test::FindJoinedUser;
  using apptraverse::test::FindRecord;
  using apptraverse::test::ToObjId;
  using apptraverse::test::UserJoinedEvent;

  auto local_user = FindJoinedUser(*app->chat, local_id);
  CHECK(local_user.is_valid());
  CHECK(app->chat->entries[0].sender.id() == local_id);
  app->chat->entries[0].sender.Load();
  CHECK(app->chat->entries[0].sender.Load().get() == local_user.Load().get());
  auto* join_record = FindRecord(*app->chat, ToObjId(join_event_id));
  CHECK(join_record != nullptr);
  auto* join_event = join_record->event.Load().as<UserJoinedEvent>();
  CHECK(join_event != nullptr);
  CHECK(join_event->user.is_loaded());
  CHECK(join_event->user.Load().get() == local_user.Load().get());
  CHECK(join_event->sender.id() == local_id);
  CHECK(local_user->presenter->user.Load().get() == local_user.Load().get());
  return EXIT_SUCCESS;
}

bool ContainsObj(ae::RamDomainStorage const& storage, ae::ObjId id) {
  return storage.state.find(id) != storage.state.end();
}

bool ContainsAny(std::vector<ae::ObjId> const& ids, ae::ObjId id) {
  for (auto const& item : ids) {
    if (item == id) {
      return true;
    }
  }
  return false;
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
  using apptraverse::GraphJournalScanner;
  using apptraverse::JournalMessageReceiver;
  using apptraverse::test::Application;
  using apptraverse::test::BuildInitialApplication;
  using apptraverse::test::Chat;
  using apptraverse::test::ChatEntryKind;
  using apptraverse::test::ChatUser;
  using apptraverse::test::DeliverPending;
  using apptraverse::test::DeliveryResult;
  using apptraverse::test::FindJoinedUser;
  using apptraverse::test::FindRecord;
  using apptraverse::test::LoadApplication;
  using apptraverse::test::LoopbackJournalMessageTransport;
  using apptraverse::test::MessageEvent;
  using apptraverse::test::TestObjectId;
  using apptraverse::test::TestRouteId;
  using apptraverse::test::ToObjId;
  using apptraverse::test::UserJoinedEvent;

  ae::ObjId const route_alice = ToObjId(TestRouteId::kAlice);
  ae::ObjId const route_support = ToObjId(TestRouteId::kSupport);
  ae::ObjId const route_bob = ToObjId(TestRouteId::kBob);

  ae::RamDomainStorage alice_storage;
  ae::RamDomainStorage support_storage;
  ae::RamDomainStorage bob_storage;

  ae::ObjId alice_user_id;
  ae::ObjId alice_user_base_id;
  ae::ObjId alice_user_presenter_id;
  ae::ObjId support_user_id;
  ae::ObjId support_user_base_id;
  ae::ObjId support_user_presenter_id;
  ae::ObjId bob_user_id;
  ae::ObjId bob_user_base_id;
  ae::ObjId bob_user_presenter_id;

  DeliveryResult alice_to_support_intro;
  DeliveryResult alice_to_bob_intro;

  // Phase 1: create three applications, join all three.
  {
    ae::Domain alice_domain{ae::Now(), alice_storage};
    ae::Domain support_domain{ae::Now(), support_storage};
    ae::Domain bob_domain{ae::Now(), bob_storage};

    auto alice_app = BuildInitialApplication(alice_domain);
    auto alice = alice_app->CreateAndJoinLocalUser(
        "Alice", ToObjId(TestObjectId::kAliceJoinEvent),
        ae::TimePoint{std::chrono::microseconds{100}},
        {route_support, route_bob});

    auto support_app = BuildInitialApplication(support_domain);
    auto support = support_app->CreateAndJoinLocalUser(
        "Support", ToObjId(TestObjectId::kSupportJoinEvent),
        ae::TimePoint{std::chrono::microseconds{200}},
        {route_alice, route_bob});

    auto bob_app = BuildInitialApplication(bob_domain);
    auto bob = bob_app->CreateAndJoinLocalUser(
        "Bob", ToObjId(TestObjectId::kBobJoinEvent),
        ae::TimePoint{std::chrono::microseconds{300}},
        {route_alice, route_support});

    alice_user_id = alice.id();
    alice_user_base_id = alice->base.id();
    alice_user_presenter_id = alice->presenter.id();
    support_user_id = support.id();
    support_user_base_id = support->base.id();
    support_user_presenter_id = support->presenter.id();
    bob_user_id = bob.id();
    bob_user_base_id = bob->base.id();
    bob_user_presenter_id = bob->presenter.id();

    CHECK(alice_user_id != support_user_id);
    CHECK(alice_user_id != bob_user_id);
    CHECK(support_user_id != bob_user_id);
    CHECK(alice_user_base_id != support_user_base_id);
    CHECK(alice_user_base_id != bob_user_base_id);
    CHECK(support_user_base_id != bob_user_base_id);
    CHECK(alice_user_presenter_id != support_user_presenter_id);
    CHECK(alice_user_presenter_id != bob_user_presenter_id);
    CHECK(support_user_presenter_id != bob_user_presenter_id);

    CHECK(alice_app->chat->presenter->chat.Load().get() ==
          alice_app->chat.Load().get());
    CHECK(support_app->chat->presenter->chat.Load().get() ==
          support_app->chat.Load().get());
    CHECK(bob_app->chat->presenter->chat.Load().get() ==
          bob_app->chat.Load().get());
    CHECK(alice->presenter->user.Load().get() == alice.Load().get());
    CHECK(support->presenter->user.Load().get() == support.Load().get());
    CHECK(bob->presenter->user.Load().get() == bob.Load().get());

    // Each domain has exactly its own local join before network sync.
    for (auto* app : {&alice_app, &support_app, &bob_app}) {
      CHECK((*app)->chat->entries.size() == 1);
      CHECK((*app)->chat->journal.size() == 1);
      CHECK((*app)->chat->entries[0].kind == ChatEntryKind::kUserJoined);
    }
    CHECK(alice_app->chat->entries[0].sender.id() == alice_user_id);
    CHECK(support_app->chat->entries[0].sender.id() == support_user_id);
    CHECK(bob_app->chat->entries[0].sender.id() == bob_user_id);
    CHECK(alice->journal.empty());
    CHECK(support->journal.empty());
    CHECK(bob->journal.empty());

    // Physical address stability via FindJoinedUser.
    {
      void* alice_address = alice.Load().get();
      alice_user_id = alice.id();
      alice.Reset();
      auto found_alice = FindJoinedUser(*alice_app->chat, alice_user_id);
      CHECK(found_alice.is_valid());
      CHECK(found_alice.Load().get() == alice_address);
      alice = found_alice;
    }
    {
      void* support_address = support.Load().get();
      support_user_id = support.id();
      support.Reset();
      auto found_support = FindJoinedUser(*support_app->chat, support_user_id);
      CHECK(found_support.is_valid());
      CHECK(found_support.Load().get() == support_address);
      support = found_support;
    }
    {
      void* bob_address = bob.Load().get();
      bob_user_id = bob.id();
      bob.Reset();
      auto found_bob = FindJoinedUser(*bob_app->chat, bob_user_id);
      CHECK(found_bob.is_valid());
      CHECK(found_bob.Load().get() == bob_address);
      bob = found_bob;
    }

    CHECK(CheckLocalJoin(alice_app, alice_user_id,
                         apptraverse::test::TestObjectId::kAliceJoinEvent) ==
          EXIT_SUCCESS);
    CHECK(CheckLocalJoin(support_app, support_user_id,
                         apptraverse::test::TestObjectId::kSupportJoinEvent) ==
          EXIT_SUCCESS);
    CHECK(CheckLocalJoin(bob_app, bob_user_id,
                         apptraverse::test::TestObjectId::kBobJoinEvent) ==
          EXIT_SUCCESS);

    // Fan-out all joins.
    alice_to_support_intro =
        DeliverPending(alice_app, route_support, support_domain, support_storage);
    alice_to_bob_intro =
        DeliverPending(alice_app, route_bob, bob_domain, bob_storage);
    CHECK(alice_to_support_intro.send_count == 1);
    CHECK(alice_to_bob_intro.send_count == 1);

    CHECK(ContainsAny(alice_to_support_intro.transferred_ids,
                      ToObjId(TestObjectId::kAliceJoinEvent)));
    CHECK(ContainsAny(alice_to_support_intro.transferred_ids, alice_user_id));
    CHECK(ContainsAny(alice_to_support_intro.transferred_ids, alice_user_base_id));
    CHECK(ContainsAny(alice_to_support_intro.transferred_ids,
                      alice_user_presenter_id));
    CHECK(!ContainsAny(alice_to_support_intro.transferred_ids,
                       ToObjId(TestObjectId::kApplication)));
    CHECK(!ContainsAny(alice_to_support_intro.transferred_ids,
                       ToObjId(TestObjectId::kChatUserPrefab)));
    CHECK(!ContainsAny(alice_to_support_intro.transferred_ids,
                       ToObjId(TestObjectId::kChatUserBasePrefab)));
    CHECK(!ContainsAny(alice_to_support_intro.transferred_ids,
                       ToObjId(TestObjectId::kChatUserPresenterPrefab)));
    CHECK(!ContainsAny(alice_to_support_intro.transferred_ids,
                       ToObjId(TestObjectId::kChat)));
    CHECK(!ContainsAny(alice_to_support_intro.transferred_ids,
                       ToObjId(TestObjectId::kChatBase)));
    CHECK(!ContainsAny(alice_to_support_intro.transferred_ids,
                       ToObjId(TestObjectId::kChatPresenter)));

    CHECK(ContainsAny(alice_to_bob_intro.transferred_ids,
                    ToObjId(TestObjectId::kAliceJoinEvent)));
    CHECK(ContainsAny(alice_to_bob_intro.transferred_ids, alice_user_id));
    CHECK(ContainsAny(alice_to_bob_intro.transferred_ids, alice_user_base_id));
    CHECK(ContainsAny(alice_to_bob_intro.transferred_ids, alice_user_presenter_id));
    CHECK(!ContainsAny(alice_to_bob_intro.transferred_ids,
                       ToObjId(TestObjectId::kChat)));
    CHECK(!ContainsAny(alice_to_bob_intro.transferred_ids,
                       ToObjId(TestObjectId::kApplication)));

    CHECK(DeliverPending(support_app, route_alice, alice_domain, alice_storage)
              .send_count == 1);
    CHECK(DeliverPending(support_app, route_bob, bob_domain, bob_storage)
              .send_count == 1);
    CHECK(DeliverPending(bob_app, route_alice, alice_domain, alice_storage)
              .send_count == 1);
    CHECK(DeliverPending(bob_app, route_support, support_domain, support_storage)
              .send_count == 1);

    for (auto* app : {&alice_app, &support_app, &bob_app}) {
      CHECK((*app)->chat->entries.size() == 3);
      CHECK((*app)->chat->journal.size() == 3);
      CHECK((*app)->chat->entries[0].kind == ChatEntryKind::kUserJoined);
      CHECK((*app)->chat->entries[0].sender.id() == alice_user_id);
      CHECK((*app)->chat->entries[1].kind == ChatEntryKind::kUserJoined);
      CHECK((*app)->chat->entries[1].sender.id() == support_user_id);
      CHECK((*app)->chat->entries[2].kind == ChatEntryKind::kUserJoined);
      CHECK((*app)->chat->entries[2].sender.id() == bob_user_id);
      CHECK((*app)->chat->journal[0].time ==
            ae::TimePoint{std::chrono::microseconds{100}});
      CHECK((*app)->chat->journal[1].time ==
            ae::TimePoint{std::chrono::microseconds{200}});
      CHECK((*app)->chat->journal[2].time ==
            ae::TimePoint{std::chrono::microseconds{300}});
    }

    auto alice_on_alice = FindJoinedUser(*alice_app->chat, alice_user_id);
    auto support_on_alice = FindJoinedUser(*alice_app->chat, support_user_id);
    auto bob_on_alice = FindJoinedUser(*alice_app->chat, bob_user_id);
    CHECK(alice_on_alice.is_valid());
    CHECK(support_on_alice.is_valid());
    CHECK(bob_on_alice.is_valid());

    auto alice_on_support = FindJoinedUser(*support_app->chat, alice_user_id);
    auto support_on_support =
        FindJoinedUser(*support_app->chat, support_user_id);
    auto bob_on_support = FindJoinedUser(*support_app->chat, bob_user_id);
    CHECK(alice_on_support.is_valid());
    CHECK(support_on_support.is_valid());
    CHECK(bob_on_support.is_valid());

    auto alice_on_bob = FindJoinedUser(*bob_app->chat, alice_user_id);
    auto support_on_bob = FindJoinedUser(*bob_app->chat, support_user_id);
    auto bob_on_bob = FindJoinedUser(*bob_app->chat, bob_user_id);
    CHECK(alice_on_bob.is_valid());
    CHECK(support_on_bob.is_valid());
    CHECK(bob_on_bob.is_valid());

    CHECK(alice_on_alice.id() == alice_on_support.id());
    CHECK(alice_on_alice.id() == alice_on_bob.id());
    CHECK(alice_on_alice.Load().get() != alice_on_support.Load().get());
    CHECK(alice_on_alice.Load().get() != alice_on_bob.Load().get());
    CHECK(alice_on_support.Load().get() != alice_on_bob.Load().get());

    CHECK(alice_on_alice->presenter.is_loaded());
    CHECK(alice_on_alice->presenter->user.Load().get() ==
          alice_on_alice.Load().get());
    alice_on_alice->chat.Load();
    CHECK(alice_on_alice->chat.Load().get() == alice_app->chat.Load().get());
    alice_on_support->chat.Load();
    CHECK(alice_on_support->chat.Load().get() ==
          support_app->chat.Load().get());
    alice_on_bob->chat.Load();
    CHECK(alice_on_bob->chat.Load().get() == bob_app->chat.Load().get());

    alice_app.Save();
    support_app.Save();
    bob_app.Save();
  }

  // Phase 2: Support offline; Alice sends message; deliver only to Bob.
  {
    ae::Domain alice_domain{ae::Now(), alice_storage};
    ae::Domain bob_domain{ae::Now(), bob_storage};
    // Intentionally no Support Domain: Support remains offline.

    auto alice_app = LoadApplication(alice_domain);
    auto bob_app = LoadApplication(bob_domain);

    CHECK(alice_app->chat->entries.size() == 3);
    CHECK(bob_app->chat->entries.size() == 3);
    CHECK(alice_app->chat->presenter->chat.Load().get() ==
          alice_app->chat.Load().get());
    CHECK(bob_app->chat->presenter->chat.Load().get() ==
          bob_app->chat.Load().get());

    auto alice = FindJoinedUser(*alice_app->chat, alice_user_id);
    CHECK(alice.is_valid());
    CHECK(alice->presenter->user.Load().get() == alice.Load().get());
    alice->chat.Load();
    CHECK(alice->chat.Load().get() == alice_app->chat.Load().get());

    alice_app->chat->presenter->SendMessage(
        alice, "Message from Alice", ToObjId(TestObjectId::kAliceMessageEvent),
        ae::TimePoint{std::chrono::microseconds{400}},
        {route_support, route_bob});

    CHECK(alice_app->chat->entries.size() == 4);
    CHECK(alice_app->chat->journal.size() == 4);
    auto* alice_message =
        FindRecord(*alice_app->chat, ToObjId(TestObjectId::kAliceMessageEvent));
    CHECK(alice_message != nullptr);
    CHECK(alice_message->event.Load().as<MessageEvent>() != nullptr);
    CHECK(alice_message->event->sender.id() == alice_user_id);
    CHECK(!alice_message->event->sender.is_loaded());
    CHECK(alice_message->event->sequence == 2);
    CHECK(alice_app->chat->next_local_sequence == 3);
    CHECK(alice_message->FindRecipient(route_support)->delivery_status ==
          DeliveryStatus::kPending);
    CHECK(alice_message->FindRecipient(route_bob)->delivery_status ==
          DeliveryStatus::kPending);

    auto bob_delivery =
        DeliverPending(alice_app, route_bob, bob_domain, bob_storage);
    CHECK(bob_delivery.send_count == 1);
    CHECK(ContainsAny(bob_delivery.transferred_ids,
                      ToObjId(TestObjectId::kAliceMessageEvent)));
    CHECK(!ContainsAny(bob_delivery.transferred_ids, alice_user_id));
    CHECK(!ContainsAny(bob_delivery.transferred_ids, alice_user_base_id));
    CHECK(!ContainsAny(bob_delivery.transferred_ids, alice_user_presenter_id));
    CHECK(!ContainsAny(bob_delivery.transferred_ids,
                       ToObjId(TestObjectId::kChat)));
    CHECK(!ContainsAny(bob_delivery.transferred_ids,
                       ToObjId(TestObjectId::kAliceJoinEvent)));
    CHECK(!ContainsAny(bob_delivery.transferred_ids,
                       ToObjId(TestObjectId::kSupportJoinEvent)));
    CHECK(!ContainsAny(bob_delivery.transferred_ids,
                       ToObjId(TestObjectId::kBobJoinEvent)));

    CHECK(alice_message->FindRecipient(route_bob)->delivery_status ==
          DeliveryStatus::kDelivered);
    CHECK(alice_message->FindRecipient(route_support)->delivery_status ==
          DeliveryStatus::kPending);

    CHECK(bob_app->chat->entries.size() == 4);
    CHECK(bob_app->chat->journal.size() == 4);
    auto* bob_remote_message = FindRecord(
        *bob_app->chat, ToObjId(TestObjectId::kAliceMessageEvent));
    CHECK(bob_remote_message != nullptr);
    CHECK(bob_remote_message->recipients.empty());
    CHECK(bob_app->chat->entries[3].kind == ChatEntryKind::kMessage);
    CHECK(bob_app->chat->entries[3].text == "Message from Alice");

    auto bob_side_alice = FindJoinedUser(*bob_app->chat, alice_user_id);
    CHECK(bob_side_alice.is_valid());
    bob_app->chat->entries[3].sender.Load();
    CHECK(bob_app->chat->entries[3].sender.Load().get() ==
          bob_side_alice.Load().get());
    bob_remote_message->event->sender.Load();
    CHECK(bob_remote_message->event->sender.Load().get() ==
          bob_side_alice.Load().get());

    GraphJournalScanner scanner;
    CHECK(CountPending(scanner, bob_app, route_support) == 0);

    alice_app.Save();
    bob_app.Save();
  }

  // Phase 3: Alice offline; Support sends reply; deliver only to Bob.
  {
    ae::Domain support_domain{ae::Now(), support_storage};
    ae::Domain bob_domain{ae::Now(), bob_storage};
    // Intentionally no Alice Domain: Alice remains offline.

    auto support_app = LoadApplication(support_domain);
    auto bob_app = LoadApplication(bob_domain);

    CHECK(support_app->chat->entries.size() == 3);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent)) == nullptr);
    auto support = FindJoinedUser(*support_app->chat, support_user_id);
    CHECK(support.is_valid());
    CHECK(FindJoinedUser(*support_app->chat, alice_user_id).is_valid());

    support_app->chat->presenter->SendMessage(
        support, "Reply from Support",
        ToObjId(TestObjectId::kSupportReplyEvent),
        ae::TimePoint{std::chrono::microseconds{500}},
        {route_alice, route_bob});

    CHECK(support_app->chat->entries.size() == 4);
    CHECK(support_app->chat->journal.size() == 4);
    auto* support_reply =
        FindRecord(*support_app->chat, ToObjId(TestObjectId::kSupportReplyEvent));
    CHECK(support_reply != nullptr);
    CHECK(support_reply->event->sender.id() == support_user_id);
    CHECK(support_reply->event->sequence == 2);
    CHECK(support_app->chat->next_local_sequence == 3);
    CHECK(support_reply->FindRecipient(route_alice)->delivery_status ==
          DeliveryStatus::kPending);
    CHECK(support_reply->FindRecipient(route_bob)->delivery_status ==
          DeliveryStatus::kPending);

    auto bob_delivery =
        DeliverPending(support_app, route_bob, bob_domain, bob_storage);
    CHECK(bob_delivery.send_count == 1);
    CHECK(ContainsAny(bob_delivery.transferred_ids,
                      ToObjId(TestObjectId::kSupportReplyEvent)));
    CHECK(!ContainsAny(bob_delivery.transferred_ids, support_user_id));

    CHECK(support_reply->FindRecipient(route_bob)->delivery_status ==
          DeliveryStatus::kDelivered);
    CHECK(support_reply->FindRecipient(route_alice)->delivery_status ==
          DeliveryStatus::kPending);

    CHECK(bob_app->chat->entries.size() == 5);
    CHECK(bob_app->chat->journal.size() == 5);
    CHECK(bob_app->chat->entries[3].text == "Message from Alice");
    CHECK(bob_app->chat->entries[4].text == "Reply from Support");
    CHECK(bob_app->chat->journal[3].time ==
          ae::TimePoint{std::chrono::microseconds{400}});
    CHECK(bob_app->chat->journal[4].time ==
          ae::TimePoint{std::chrono::microseconds{500}});

    auto* bob_remote_reply =
        FindRecord(*bob_app->chat, ToObjId(TestObjectId::kSupportReplyEvent));
    CHECK(bob_remote_reply != nullptr);
    CHECK(bob_remote_reply->recipients.empty());
    auto bob_side_support = FindJoinedUser(*bob_app->chat, support_user_id);
    CHECK(bob_side_support.is_valid());
    bob_app->chat->entries[4].sender.Load();
    CHECK(bob_app->chat->entries[4].sender.Load().get() ==
          bob_side_support.Load().get());
    bob_remote_reply->event->sender.Load();
    CHECK(bob_remote_reply->event->sender.Load().get() ==
          bob_side_support.Load().get());

    GraphJournalScanner scanner;
    CHECK(CountPending(scanner, bob_app, route_alice) == 0);

    support_app.Save();
    bob_app.Save();
  }

  // Phase 4: Alice and Support sync missed messages; Bob already converged.
  void* alice_chat_address = nullptr;
  void* alice_presenter_address = nullptr;
  void* support_chat_address = nullptr;
  void* support_presenter_address = nullptr;
  void* support_user_address = nullptr;
  void* support_user_presenter_address = nullptr;

  {
    ae::Domain alice_domain{ae::Now(), alice_storage};
    ae::Domain support_domain{ae::Now(), support_storage};
    ae::Domain bob_domain{ae::Now(), bob_storage};

    auto alice_app = LoadApplication(alice_domain);
    auto support_app = LoadApplication(support_domain);
    auto bob_app = LoadApplication(bob_domain);

    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent)) != nullptr);
    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent)) == nullptr);
    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->FindRecipient(route_bob)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->FindRecipient(route_support)
              ->delivery_status == DeliveryStatus::kPending);

    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent)) != nullptr);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent)) == nullptr);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_bob)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_alice)
              ->delivery_status == DeliveryStatus::kPending);

    CHECK(FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent)) != nullptr);
    CHECK(FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent)) != nullptr);
    CHECK(FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->recipients.empty());
    CHECK(FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->recipients.empty());

    alice_chat_address = alice_app->chat.Load().get();
    alice_presenter_address = alice_app->chat->presenter.Load().get();
    support_chat_address = support_app->chat.Load().get();
    support_presenter_address = support_app->chat->presenter.Load().get();
    {
      auto support_local = FindJoinedUser(*support_app->chat, support_user_id);
      CHECK(support_local.is_valid());
      support_user_address = support_local.Load().get();
      support_user_presenter_address = support_local->presenter.Load().get();
    }

    // Phase 4A: Alice → Support (insert Event#300 before local Event#301)
    CHECK(DeliverPending(alice_app, route_support, support_domain,
                         support_storage)
              .send_count == 1);

    CHECK(support_app->chat->entries.size() == 5);
    CHECK(support_app->chat->journal.size() == 5);
    CHECK(support_app->chat->journal[3].event.id() ==
          ToObjId(TestObjectId::kAliceMessageEvent));
    CHECK(support_app->chat->journal[4].event.id() ==
          ToObjId(TestObjectId::kSupportReplyEvent));
    CHECK(support_app->chat->journal[3].time ==
          ae::TimePoint{std::chrono::microseconds{400}});
    CHECK(support_app->chat->journal[4].time ==
          ae::TimePoint{std::chrono::microseconds{500}});
    CHECK(support_app->chat->entries[3].text == "Message from Alice");
    CHECK(support_app->chat->entries[4].text == "Reply from Support");
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->recipients.empty());
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_alice)
              ->delivery_status == DeliveryStatus::kPending);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_bob)
              ->delivery_status == DeliveryStatus::kDelivered);

    CHECK(support_app->chat.Load().get() == support_chat_address);
    CHECK(support_app->chat->presenter.Load().get() ==
          support_presenter_address);
    {
      auto support_after = FindJoinedUser(*support_app->chat, support_user_id);
      CHECK(support_after.is_valid());
      CHECK(support_after.Load().get() == support_user_address);
      CHECK(support_after->presenter.Load().get() ==
            support_user_presenter_address);
    }
    CHECK(support_app->chat->presenter->chat.Load().get() ==
          support_chat_address);

    // Phase 4B: Support → Alice
    CHECK(DeliverPending(support_app, route_alice, alice_domain, alice_storage)
              .send_count == 1);

    CHECK(alice_app->chat->entries.size() == 5);
    CHECK(alice_app->chat->journal.size() == 5);
    CHECK(alice_app->chat->entries[4].text == "Reply from Support");
    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->recipients.empty());
    CHECK(alice_app->chat.Load().get() == alice_chat_address);
    CHECK(alice_app->chat->presenter.Load().get() == alice_presenter_address);
    CHECK(alice_app->chat->presenter->chat.Load().get() == alice_chat_address);

    auto check_final = [&](Application::ptr& app) -> int {
      CHECK(app->chat->entries.size() == 5);
      CHECK(app->chat->journal.size() == 5);
      CHECK(app->chat->entries[0].kind == ChatEntryKind::kUserJoined);
      CHECK(app->chat->entries[0].sender.id() == alice_user_id);
      CHECK(app->chat->entries[1].kind == ChatEntryKind::kUserJoined);
      CHECK(app->chat->entries[1].sender.id() == support_user_id);
      CHECK(app->chat->entries[2].kind == ChatEntryKind::kUserJoined);
      CHECK(app->chat->entries[2].sender.id() == bob_user_id);
      CHECK(app->chat->entries[3].kind == ChatEntryKind::kMessage);
      CHECK(app->chat->entries[3].text == "Message from Alice");
      CHECK(app->chat->entries[3].sender.id() == alice_user_id);
      CHECK(app->chat->entries[4].kind == ChatEntryKind::kMessage);
      CHECK(app->chat->entries[4].text == "Reply from Support");
      CHECK(app->chat->entries[4].sender.id() == support_user_id);

      CHECK(app->chat->journal[0].event.id() ==
            ToObjId(TestObjectId::kAliceJoinEvent));
      CHECK(app->chat->journal[1].event.id() ==
            ToObjId(TestObjectId::kSupportJoinEvent));
      CHECK(app->chat->journal[2].event.id() ==
            ToObjId(TestObjectId::kBobJoinEvent));
      CHECK(app->chat->journal[3].event.id() ==
            ToObjId(TestObjectId::kAliceMessageEvent));
      CHECK(app->chat->journal[4].event.id() ==
            ToObjId(TestObjectId::kSupportReplyEvent));
      CHECK(app->chat->journal[0].event->sender.id() == alice_user_id);
      CHECK(app->chat->journal[1].event->sender.id() == support_user_id);
      CHECK(app->chat->journal[2].event->sender.id() == bob_user_id);
      CHECK(app->chat->journal[3].event->sender.id() == alice_user_id);
      CHECK(app->chat->journal[4].event->sender.id() == support_user_id);
      CHECK(app->chat->journal[0].event->sequence == 1);
      CHECK(app->chat->journal[1].event->sequence == 1);
      CHECK(app->chat->journal[2].event->sequence == 1);
      CHECK(app->chat->journal[3].event->sequence == 2);
      CHECK(app->chat->journal[4].event->sequence == 2);
      return EXIT_SUCCESS;
    };

    CHECK(check_final(alice_app) == EXIT_SUCCESS);
    CHECK(check_final(support_app) == EXIT_SUCCESS);
    CHECK(check_final(bob_app) == EXIT_SUCCESS);

    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->FindRecipient(route_support)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->FindRecipient(route_bob)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_alice)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_bob)
              ->delivery_status == DeliveryStatus::kDelivered);

    for (auto event_id : {ToObjId(TestObjectId::kAliceJoinEvent),
                          ToObjId(TestObjectId::kSupportJoinEvent),
                          ToObjId(TestObjectId::kBobJoinEvent)}) {
      auto* alice_record = FindRecord(*alice_app->chat, event_id);
      auto* support_record = FindRecord(*support_app->chat, event_id);
      auto* bob_record = FindRecord(*bob_app->chat, event_id);
      CHECK(alice_record != nullptr);
      CHECK(support_record != nullptr);
      CHECK(bob_record != nullptr);
    }

    // Remote copies only: local Join#200/#201/#202 keep recipient states.
    for (auto* remote_record :
         {FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kSupportJoinEvent)),
          FindRecord(*alice_app->chat, ToObjId(TestObjectId::kBobJoinEvent)),
          FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent)),
          FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kAliceJoinEvent)),
          FindRecord(*support_app->chat, ToObjId(TestObjectId::kBobJoinEvent)),
          FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent)),
          FindRecord(*bob_app->chat, ToObjId(TestObjectId::kAliceJoinEvent)),
          FindRecord(*bob_app->chat, ToObjId(TestObjectId::kSupportJoinEvent)),
          FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent)),
          FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))}) {
      CHECK(remote_record != nullptr);
      CHECK(remote_record->recipients.empty());
    }
    CHECK(FindRecord(*alice_app->chat, ToObjId(TestObjectId::kAliceJoinEvent))
              ->recipients.size() == 2);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportJoinEvent))
              ->recipients.size() == 2);
    CHECK(FindRecord(*bob_app->chat, ToObjId(TestObjectId::kBobJoinEvent))
              ->recipients.size() == 2);

    // No forwarding of accepted events.
    CHECK(DeliverPending(bob_app, route_support, support_domain, support_storage)
              .send_count == 0);
    CHECK(DeliverPending(bob_app, route_alice, alice_domain, alice_storage)
              .send_count == 0);
    CHECK(DeliverPending(alice_app, route_bob, bob_domain, bob_storage)
              .send_count == 0);
    CHECK(DeliverPending(support_app, route_bob, bob_domain, bob_storage)
              .send_count == 0);

    // Retention via join entries / journal, not a separate registry.
    {
      auto temporary = FindJoinedUser(*alice_app->chat, alice_user_id);
      CHECK(temporary.is_valid());
      auto id = temporary.id();
      temporary.Reset();
      auto found_again = FindJoinedUser(*alice_app->chat, id);
      CHECK(found_again.is_valid());
      CHECK(found_again.id() == alice_user_id);
    }

    for (auto* app : {&alice_app, &support_app, &bob_app}) {
      auto found_alice = FindJoinedUser(*(*app)->chat, alice_user_id);
      auto found_support = FindJoinedUser(*(*app)->chat, support_user_id);
      auto found_bob = FindJoinedUser(*(*app)->chat, bob_user_id);
      CHECK(found_alice.is_valid());
      CHECK(found_support.is_valid());
      CHECK(found_bob.is_valid());
      (*app)->chat->entries[3].sender.Load();
      (*app)->chat->entries[4].sender.Load();
      CHECK((*app)->chat->entries[3].sender.Load().get() ==
            found_alice.Load().get());
      CHECK((*app)->chat->entries[4].sender.Load().get() ==
            found_support.Load().get());
      found_alice->chat.Load();
      CHECK(found_alice->chat.Load().get() == (*app)->chat.Load().get());
      CHECK(found_alice->presenter->user.Load().get() ==
            found_alice.Load().get());
      CHECK(found_support->presenter->user.Load().get() ==
            found_support.Load().get());
      CHECK(found_bob->presenter->user.Load().get() == found_bob.Load().get());
    }

    // Repeat sync: nothing pending.
    CHECK(DeliverPending(alice_app, route_support, support_domain,
                         support_storage)
              .send_count == 0);
    CHECK(DeliverPending(support_app, route_alice, alice_domain, alice_storage)
              .send_count == 0);
    CHECK(alice_app->chat->entries.size() == 5);
    CHECK(support_app->chat->entries.size() == 5);
    CHECK(alice_app->chat->journal.size() == 5);
    CHECK(support_app->chat->journal.size() == 5);

    CHECK(alice_app->chat->next_local_sequence == 3);
    CHECK(support_app->chat->next_local_sequence == 3);
    CHECK(bob_app->chat->next_local_sequence == 2);

    alice_app.Save();
    support_app.Save();
    bob_app.Save();
  }

  // Final reload after destroying all Domains.
  {
    ae::Domain alice_domain{ae::Now(), alice_storage};
    ae::Domain support_domain{ae::Now(), support_storage};
    ae::Domain bob_domain{ae::Now(), bob_storage};

    auto alice_app = LoadApplication(alice_domain);
    auto support_app = LoadApplication(support_domain);
    auto bob_app = LoadApplication(bob_domain);

    for (auto* app : {&alice_app, &support_app, &bob_app}) {
      CHECK((*app)->chat->entries.size() == 5);
      CHECK((*app)->chat->journal.size() == 5);
      CHECK((*app)->chat->entries[3].text == "Message from Alice");
      CHECK((*app)->chat->entries[4].text == "Reply from Support");
      CHECK((*app)->chat->journal[0].event->sender.id() == alice_user_id);
      CHECK((*app)->chat->journal[0].event->sequence == 1);
      CHECK((*app)->chat->journal[1].event->sender.id() == support_user_id);
      CHECK((*app)->chat->journal[1].event->sequence == 1);
      CHECK((*app)->chat->journal[2].event->sender.id() == bob_user_id);
      CHECK((*app)->chat->journal[2].event->sequence == 1);
      CHECK((*app)->chat->journal[3].event->sender.id() == alice_user_id);
      CHECK((*app)->chat->journal[3].event->sequence == 2);
      CHECK((*app)->chat->journal[4].event->sender.id() == support_user_id);
      CHECK((*app)->chat->journal[4].event->sequence == 2);
      CHECK((*app)->chat->presenter->chat.Load().get() ==
            (*app)->chat.Load().get());

      auto found_alice = FindJoinedUser(*(*app)->chat, alice_user_id);
      auto found_support = FindJoinedUser(*(*app)->chat, support_user_id);
      auto found_bob = FindJoinedUser(*(*app)->chat, bob_user_id);
      CHECK(found_alice.is_valid());
      CHECK(found_support.is_valid());
      CHECK(found_bob.is_valid());
      (*app)->chat->entries[3].sender.Load();
      (*app)->chat->entries[4].sender.Load();
      CHECK((*app)->chat->entries[3].sender.Load().get() ==
            found_alice.Load().get());
      CHECK((*app)->chat->entries[4].sender.Load().get() ==
            found_support.Load().get());
      found_alice->chat.Load();
      CHECK(found_alice->chat.Load().get() == (*app)->chat.Load().get());
      CHECK(found_alice->presenter->user.Load().get() ==
            found_alice.Load().get());
    }

    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->FindRecipient(route_support)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*alice_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->FindRecipient(route_bob)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_alice)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*support_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->FindRecipient(route_bob)
              ->delivery_status == DeliveryStatus::kDelivered);
    CHECK(FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kAliceMessageEvent))
              ->recipients.empty());
    CHECK(FindRecord(*bob_app->chat,
                     ToObjId(TestObjectId::kSupportReplyEvent))
              ->recipients.empty());

    auto alice_on_alice = FindJoinedUser(*alice_app->chat, alice_user_id);
    auto alice_on_support = FindJoinedUser(*support_app->chat, alice_user_id);
    auto alice_on_bob = FindJoinedUser(*bob_app->chat, alice_user_id);
    CHECK(alice_on_alice.Load().get() != alice_on_support.Load().get());
    CHECK(alice_on_alice.Load().get() != alice_on_bob.Load().get());
    CHECK(alice_on_support.Load().get() != alice_on_bob.Load().get());

    CHECK(DeliverPending(bob_app, route_support, support_domain, support_storage)
              .send_count == 0);
    CHECK(DeliverPending(bob_app, route_alice, alice_domain, alice_storage)
              .send_count == 0);

    CHECK(alice_app->chat->next_local_sequence == 3);
    CHECK(support_app->chat->next_local_sequence == 3);
    CHECK(bob_app->chat->next_local_sequence == 2);
  }

  return EXIT_SUCCESS;
}
