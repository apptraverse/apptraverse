#include <optional>
#include "apptraverse/chat_session.h"
#include "apptraverse/push_protocol.h"
#include <iostream>
#include <cctype>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <map>
#include <thread>
#include <utility>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/embedded_distilled_domain.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/overlay_domain_storage.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync.h"
#include "apptraverse/single_file_user_storage.h"
#if APPTRAVERSE_CHAT_DISTILLATION
#  include "apptraverse/chat_supply_export.h"
#  include "apptraverse/distill.h"
#  include "apptraverse/distilled_domain_export.h"
#else
#  ifndef APPTRAVERSE_CHAT_EMBEDDED_HEADER
#    error \
        "Prepare the chat supply with the Debug exporter and set APPTRAVERSE_CHAT_SUPPLY_HEADER"
#  endif
#  include APPTRAVERSE_CHAT_EMBEDDED_HEADER
#endif
#include "aether-miscpp/serialization/binary_archive.h"
#include "apptraverse/aether_connection.h"

#ifdef RegisterClass
#  undef RegisterClass
#endif

namespace apptraverse::chat {
namespace {

constexpr ae::ObjId kApplicationId{0x7a000001};

std::uint64_t MonotonicMicros() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

std::string Trim(std::string value) {
  auto const whitespace = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  auto begin = std::find_if_not(value.begin(), value.end(), whitespace);
  auto end = std::find_if_not(value.rbegin(), value.rend(), whitespace).base();
  return begin < end ? std::string{begin, end} : std::string{};
}

class Participant : public NodeFor<Participant> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::Participant", Participant, Node,
                           0)
 protected:
  Participant() = default;

 public:
  explicit Participant(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(link))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, name, link);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, name, link);
  }
  std::string name;
  SharedLinkDescriptor::ptr link;
};

struct ChatMessage {
  std::string author_name;
  std::string text;
  std::string id, author_uid;
  AE_REFLECT_MEMBERS(author_name, text, id, author_uid)
};

class TextEvent;
class JoinEvent;
class Room : public NodeFor<Room, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::Room", Room, SharedNode, 0)
 protected:
  Room() = default;

 public:
  explicit Room(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(host_uid), AE_MMBR(participants), AE_MMBR(messages), AE_MMBR(message_times))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, host_uid, participants, messages, message_times);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, host_uid, participants, messages, message_times);
  }
  bool CanApply(TextEvent const&) const;
  bool CanApply(JoinEvent const&) const;
  void Apply(TextEvent const&);
  void Apply(JoinEvent const&);
  std::string host_uid;
  std::vector<Participant::ptr> participants;
  std::vector<ChatMessage> messages;
  std::map<std::string,std::uint64_t> message_times;
};

class TextEvent : public EventFor<Room, TextEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::TextEvent", TextEvent, Event, 0)
 protected:
  TextEvent() = default;

 public:
  explicit TextEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(author), AE_MMBR(text))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, author, text);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, author, text);
  }
  Participant::ptr author;
  std::string text;
};

class JoinEvent : public EventFor<Room, JoinEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::JoinEvent", JoinEvent, Event, 0)
 protected:
  JoinEvent() = default;

 public:
  explicit JoinEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(person))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, person);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, person);
  }
  Participant::ptr person;
};

bool Room::CanApply(TextEvent const& event) const {
  return !event.text.empty() && event.author.is_valid() &&
         std::any_of(participants.begin(), participants.end(),
                     [&](auto const& person) {
                       return person.id() == event.author.id();
                     });
}
bool Room::CanApply(JoinEvent const& event) const {
  return event.person.is_valid() && event.person->link.is_valid() &&
         !event.person->name.empty() &&
         std::none_of(links.begin(), links.end(), [&](auto const& link) {
           return link->endpoint_uid == event.person->link->endpoint_uid;
         });
}
void Room::Apply(TextEvent const& event) {
  auto const& author_uid = event.author->link->endpoint_uid;
  messages.push_back({event.author->name, event.text,
                      author_uid + ":" + std::to_string(event.obj_id.id()),
                      author_uid});
  // Read the retained event record while it is being applied, including replay.
  // Persist the timestamp in materialized state before the journal is compacted.
  auto record=std::find_if(journal.begin(),journal.end(),[&](auto const& entry) {
    return entry.event.id()==event.obj_id;
  });
  if(record!=journal.end()) message_times[messages.back().id]=record->order.timestamp_us;
  NoteMaterializedChange();
}
void Room::Apply(JoinEvent const& event) {
  participants.push_back(event.person);
  links.push_back(event.person->link);
  NoteMaterializedChange();
}

class AttachEvent;
class ExpectEvent;
class IdentityEvent;
class Application : public NodeFor<Application> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::Application", Application, Node,
                           0)
 protected:
  Application() = default;

 public:
  explicit Application(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(endpoint), AE_MMBR(name), AE_MMBR(room),
                    AE_MMBR(expected), AE_MMBR(source))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, endpoint, name, room, expected, source);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, endpoint, name, room, expected, source);
  }
  void Attach(SharedNode::ptr value);
  void Expect(ae::ObjId id, std::string uid);
  void Apply(AttachEvent const&);
  void Apply(ExpectEvent const&);
  void Apply(IdentityEvent const&);
  std::string endpoint;
  std::string name;
  SharedNode::ptr room;
  ae::ObjId expected;
  std::string source;
};

class AttachEvent : public EventFor<Application, AttachEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::AttachEvent", AttachEvent, Event,
                           0)
 protected:
  AttachEvent() = default;

 public:
  explicit AttachEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(room))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, room);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, room);
  }
  SharedNode::ptr room;
};

class IdentityEvent : public EventFor<Application, IdentityEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::IdentityEvent", IdentityEvent,
                           Event, 0)
 protected:
  IdentityEvent() = default;

 public:
  explicit IdentityEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(endpoint), AE_MMBR(name))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, endpoint, name);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, endpoint, name);
  }
  std::string endpoint, name;
};
class ExpectEvent : public EventFor<Application, ExpectEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::ExpectEvent", ExpectEvent, Event,
                           0)
 protected:
  ExpectEvent() = default;

 public:
  explicit ExpectEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(expected), AE_MMBR(source))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, expected, source);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, expected, source);
  }
  ae::ObjId expected;
  std::string source;
};
void Application::Attach(SharedNode::ptr value) {
  auto event = AttachEvent::ptr::Create(ae::CreateWith{*domain});
  event->room = std::move(value);
  Commit(std::move(event));
}
void Application::Expect(ae::ObjId id, std::string uid) {
  auto event = ExpectEvent::ptr::Create(ae::CreateWith{*domain});
  event->expected = id;
  event->source = std::move(uid);
  Commit(std::move(event));
}
void Application::Apply(AttachEvent const& event) {
  room = event.room;
  expected = {};
  source.clear();
  NoteMaterializedChange();
}
void Application::Apply(ExpectEvent const& event) {
  expected = event.expected;
  source = event.source;
  NoteMaterializedChange();
}
void Application::Apply(IdentityEvent const& event) {
  endpoint = event.endpoint;
  name = event.name;
  NoteMaterializedChange();
}
APPTRAVERSE_REGISTER(Participant);
APPTRAVERSE_REGISTER(Room);
APPTRAVERSE_REGISTER(TextEvent);
APPTRAVERSE_REGISTER(JoinEvent);
APPTRAVERSE_REGISTER(Application);
APPTRAVERSE_REGISTER(AttachEvent);
APPTRAVERSE_REGISTER(ExpectEvent);
APPTRAVERSE_REGISTER(IdentityEvent);

Participant::ptr MakeParticipant(ae::Domain& domain, std::string uid,
                                 std::string name) {
  auto descriptor = SharedLinkDescriptor::ptr::Create(ae::CreateWith{domain});
  descriptor->endpoint_uid = std::move(uid);
  InitializeRuntimeNode(*descriptor);
  auto person = Participant::ptr::Create(ae::CreateWith{domain});
  person->name = std::move(name);
  person->link = descriptor;
  InitializeRuntimeNode(*person);
  return person;
}

enum class ChatFrameKind : std::uint8_t {
  Sync = 1, JoinRequest = 2, ResolveRequest = 3, ResolveReply = 4
};
// Application routing, not an alternative graph format: both this frame and
// the enclosed SharedNode messages use the SDK's ordinary BinaryArchive.
struct ChatFrame {
  std::uint16_t version{1};
  ChatFrameKind kind{ChatFrameKind::Sync};
  ae::ObjId room;
  std::string name;
  std::vector<std::uint8_t> bytes;
  AE_REFLECT_MEMBERS(version, kind, room, name, bytes)
};
bool EncodeFrame(ChatFrame const& frame, std::vector<std::uint8_t>& bytes) {
  ae::seri::BinaryVectorBuffer buffer{bytes};
  ae::seri::BinaryArchive archive{buffer};
  return archive.Save(frame).IsOk();
}
bool DecodeFrame(std::vector<std::uint8_t> const& bytes, ChatFrame& frame) {
  auto copy = bytes;
  ae::seri::BinaryVectorBuffer buffer{copy};
  ae::seri::BinaryArchive archive{buffer};
  return archive.Load(frame).IsOk() &&
         archive.buffer().read_position() == copy.size() &&
         frame.version == 1 &&
         ((frame.kind == ChatFrameKind::Sync && !frame.bytes.empty() &&
           frame.name.empty()) ||
          (frame.kind == ChatFrameKind::JoinRequest && frame.room.is_valid() &&
           !frame.name.empty() && frame.name.size() <= 256 &&
           frame.bytes.empty()) ||
          (frame.kind == ChatFrameKind::ResolveRequest &&
           !frame.room.is_valid() && frame.name.empty() && frame.bytes.empty()) ||
          (frame.kind == ChatFrameKind::ResolveReply &&
           frame.room.is_valid() && frame.name.empty() && frame.bytes.empty()));
}

class ChatLink final : public IByteLink {
 public:
  using Join = std::function<void(std::string const&, ae::ObjId, std::string)>;
  using Resolve = std::function<void(std::string const&, ae::ObjId)>;
  ChatLink(IByteLink& native, Join on_join, Resolve on_resolve)
      : native_{native}, on_join_{std::move(on_join)},
        on_resolve_{std::move(on_resolve)} {
    native_.SetReceiver([this](auto const& source, auto const& bytes) {
      ChatFrame frame;
      if (!DecodeFrame(bytes, frame)) {
        return;
      }
      if (frame.kind == ChatFrameKind::JoinRequest) {
        on_join_(source, frame.room, std::move(frame.name));
      } else if (frame.kind == ChatFrameKind::ResolveRequest ||
                 frame.kind == ChatFrameKind::ResolveReply) {
        on_resolve_(source, frame.room);
      } else if (receiver_) {
        receiver_(source, frame.bytes);
      }
    });
  }
  ~ChatLink() override { native_.SetReceiver({}); }
  void SetReceiver(Receiver receiver) override {
    receiver_ = std::move(receiver);
  }
  bool Send(std::string const& destination,
            std::vector<std::uint8_t> const& bytes) override {
    ChatFrame frame;
    frame.bytes = bytes;
    std::vector<std::uint8_t> encoded;
    return EncodeFrame(frame, encoded) && native_.Send(destination, encoded);
  }
  LinkAvailability Availability(std::string const& peer) const override {
    return native_.Availability(peer);
  }
  bool RequestJoin(std::string const& host, ae::ObjId room,
                   std::string const& name) {
    ChatFrame frame;
    frame.kind = ChatFrameKind::JoinRequest;
    frame.room = room;
    frame.name = name;
    std::vector<std::uint8_t> encoded;
    return EncodeFrame(frame, encoded) && native_.Send(host, encoded);
  }
  bool ResolveRoom(std::string const& peer, ae::ObjId room = {}) {
    ChatFrame frame;
    frame.kind = room.is_valid() ? ChatFrameKind::ResolveReply
                                : ChatFrameKind::ResolveRequest;
    frame.room = room;
    std::vector<std::uint8_t> encoded;
    return EncodeFrame(frame, encoded) && native_.Send(peer, encoded);
  }

 private:
  IByteLink& native_;
  Join on_join_;
  Resolve on_resolve_;
  Receiver receiver_;
};

}  // namespace

struct ChatSession::Impl {
  std::string push_service, push_token;
  std::string registered_token;
  std::uint64_t push_request{1}, registration_request{}, next_registration{};
  push::Reader push_reader;
  // A short burst must not lose every request after the transport's single
  // occupied frame. This is only an in-memory queue; accepted writes are not
  // retried, because v1 Notify has no deduplication contract.
  std::deque<push::Packet> push_notifications;
  int push_notify_status{-1};
  std::uint64_t push_notify_request{};
  void ServicePush(std::uint64_t now) {
    if(push_service.empty()||!native)return;
    if(!push_token.empty()&&registered_token!=push_token&&now>=next_registration){
      if(!registration_request)registration_request=push_request++;
      if(native->Send(push_service,push::Encode({1,registration_request,push_token})))
        next_registration=now+5000000;
    }
    if(!push_notifications.empty()&&native->Send(push_service,push::Encode(push_notifications.front())))
      push_notifications.pop_front();
  }
  void NotifyPush() {
    auto* room=CurrentRoom(); if(!room||push_service.empty()||!native)return;
    for(auto const& peer:room->participants) {
      auto const& uid=peer->link->endpoint_uid;
      if(uid==native->EndpointId())continue;
      if(push_notifications.size()>=64){
        std::cerr<<"Push request not queued (64 pending requests)\n";
        continue;
      }
      push_notifications.push_back({2,push_request++,uid});
    }
  }

  Impl(std::filesystem::path value, std::string user_name, Changed changed,
       PrepareProfile prepare, FlushProfile flush, AdaptersFactory adapters)
      : profile{std::filesystem::absolute(std::move(value))},
        name{Trim(std::move(user_name))},
        on_changed{std::move(changed)},
        prepare_profile{std::move(prepare)},
        flush_profile{std::move(flush)},
        adapters_factory{std::move(adapters)},
        proxy{[this](ModelObjectProxy::ModelWork work) {
          Enqueue(std::move(work), {});
        }} {
    if (name.empty()) {
      name = "User";
    }
    view.status = "Starting Aether...";
  }

  std::filesystem::path profile;
  std::string name;
  Changed on_changed;
  PrepareProfile prepare_profile;
  FlushProfile flush_profile;
  AdaptersFactory adapters_factory;
  std::mutex network_mutex;
  NetworkObservation pending_network;
  mutable std::mutex view_mutex;
  ChatView view;
  std::mutex lifecycle_mutex;
  std::mutex command_mutex;
  struct Command {
    ModelObjectProxy::ModelWork work;
    std::function<void()> rejected;
  };
  bool accepting_commands{false};  // protected by command_mutex
  std::deque<Command> commands;
  ModelObjectProxy proxy;
  std::atomic<bool> stop_requested{true};
  std::thread thread;

  // Everything below is owned and accessed exclusively by the model thread.
  std::unique_ptr<ConnectionRuntime> connection;
  SessionObservation last_connection;
  bool failed{false};
  bool finished{false};

  bool view_dirty{true};
  std::string status;
  std::string resolving_host;
  ae::RamDomainStorage storage;
  ae::RamDomainStorage backing;
  std::unique_ptr<OverlayDomainStorage> model_storage;
  std::unique_ptr<ae::Domain> domain;
  Application::ptr app;
  IConnectionTransport* native{nullptr};
  std::unique_ptr<ChatLink> link;
  std::unique_ptr<SharedSyncRuntime> sync;
  std::uint64_t next_join_request{0};
  std::uint64_t next_progress{0};
  std::map<std::string, std::uint64_t> local_message_start_us;
  std::map<std::string, std::uint64_t> local_message_ack_elapsed_us;
  std::uint64_t current_monotonic_us{0};

  bool WatchPeer(std::string const& endpoint){
    if(!native->WatchPeer(endpoint))return false;
    connection->WatchPresence({PresenceScope::Endpoint,endpoint});return true;
  }

  Room* CurrentRoom() const {
    if (!app.is_valid() || !app->room.is_valid()) {
      return nullptr;
    }
    auto held = domain->Find(app->room.id());
    if (!held || held->GetClassId() != Room::kClassId) {
      return nullptr;
    }
    return held.as<Room>();
  }

  void Observe(Node& node) {
    node.BindMaterializedChangeNotifier(this, [](void* ctx, Node&) {
      static_cast<Impl*>(ctx)->view_dirty = true;
    });
  }

  bool CommitMemory() {
    // ACK only after the complete native graph and delivery obligations are
    // durable. A single USER file also keeps the application root stable.
    ae::RamDomainStorage snapshot;
    SavePersistedObjectGraphToRam(*app, snapshot);
    if (!SaveUserStorageFile(snapshot, profile)) {
      return false;
    }
    if (flush_profile && !flush_profile()) return false;
    storage = std::move(snapshot);
    return true;
  }

  void SetStatus(std::string message) {
    if (status == message) {
      return;
    }
    status = std::move(message);
    view_dirty = true;
  }

  void SetError(std::string message) {
    SetStatus(std::move(message));
    failed = true;
    RejectPendingCommands();
  }

  void Enqueue(ModelObjectProxy::ModelWork work, std::function<void()> rejected) {
    {
      std::lock_guard lock{command_mutex};
      if (accepting_commands && !stop_requested.load()) {
        commands.push_back({std::move(work), std::move(rejected)});
        return;
      }
    }
    if (rejected) rejected();
  }

  void RejectPendingCommands() {
    std::deque<Command> pending;
    {
      std::lock_guard lock{command_mutex};
      accepting_commands = false;
      pending.swap(commands);
    }
    for (auto& command : pending) {
      if (command.rejected) command.rejected();
    }
  }

  void BindExpected() {
    if (!app->expected.is_valid()) {
      return;
    }
    WatchPeer(app->source);
    sync->ExpectInitialState(app->expected, app->source, Room::kClassId,
                             [this](SharedNode::ptr value) {
                               Observe(*value);
                               app->Attach(std::move(value));
                               SetStatus("Connected");
                             });
    SetStatus("Waiting for the host...");
  }

  bool InitializeModel() {
    storage = {};
    if (!LoadUserStorageFile(storage, profile)) {
      return false;
    }
    bool const first_launch = storage.state.empty();
    backing = {};
#if !APPTRAVERSE_CHAT_DISTILLATION
    InstallEmbeddedDistilledBacking(backing,
                                    embedded_distilled::chat::kEmbeddedGraph);
#endif
    model_storage = std::make_unique<OverlayDomainStorage>(storage, backing);
    domain = std::make_unique<ae::Domain>(*model_storage);
#if APPTRAVERSE_CHAT_DISTILLATION
    if (storage.state.empty()) {
      app = Application::ptr::Create(
          ae::CreateWith{*domain}.with_id(kApplicationId));
      app->endpoint = native->EndpointId();
      app->name = name;
      InitializeRuntimeNode(*app);
    } else {
      ae::DomainGraph graph{domain.get()};
      auto held = graph.LoadRoot(kApplicationId);
      if (!held || held->GetClassId() != Application::kClassId) {
        return false;
      }
      app = Application::ptr{domain.get(), kApplicationId, {}, held};
      if (app->endpoint != native->EndpointId()) {
        return false;
      }
    }
#else
    ae::DomainGraph graph{domain.get()};
    auto held = graph.LoadRoot(kApplicationId);
    if (!held || held->GetClassId() != Application::kClassId) {
      return false;
    }
    app = Application::ptr{domain.get(), kApplicationId, {}, held};
    if (first_launch) {
      if (!app->endpoint.empty() || !app->name.empty() ||
          app->room.is_valid() || app->expected.is_valid()) {
        return false;
      }
      auto event = IdentityEvent::ptr::Create(ae::CreateWith{*domain});
      event->endpoint = native->EndpointId();
      event->name = name;
      app->Commit(std::move(event));
    } else if (app->endpoint != native->EndpointId()) {
      return false;
    }
#endif
    (void)first_launch;
    Observe(*app);
    link = std::make_unique<ChatLink>(
        *native,
        [this](auto const& source, ae::ObjId room, std::string person_name) {
          OnJoinRequest(source, room, std::move(person_name));
        },
        [this](auto const& source, ae::ObjId room) {
          OnResolveRoom(source, room);
        });
    sync = std::make_unique<SharedSyncRuntime>(
        *domain, *model_storage, *link,
        // Rebuild runtime retries from the persisted SharedNode obligations.
        native->EndpointId(), [this] { return CommitMemory(); });
    if (app->room.is_valid()) {
      sync->RegisterNode(app->room);
      Observe(*app->room);
    }
    if (!CommitMemory()) {
      return false;
    }
    SetStatus(app->room.is_valid() ? "Session restored"
                                   : "Enter the host Aether ID");
    BindExpected();
    return true;
  }

  void CreateRoom() {
    if (failed || !sync || CurrentRoom() || app->expected.is_valid()) {
      return;
    }
    auto host = MakeParticipant(*domain, native->EndpointId(), app->name);
    auto room = Room::ptr::Create(ae::CreateWith{*domain});
    room->host_uid = native->EndpointId();
    room->participants = {host};
    room->links = {host->link};
    SharedSyncRuntime::InitializeLocalState(*room);
    InitializeRuntimeNode(*room);
    Observe(*room);
    app->Attach(room);
    sync->RegisterNode(room);
    if (!CommitMemory()) {
      SetError("Could not commit the room in memory");
      return;
    }
    SetStatus("Room created. Share your Aether ID");
  }

  void JoinRoom(std::string invite) {
    if (failed || !sync || CurrentRoom() || app->expected.is_valid()) {
      return;
    }
    auto host = Trim(std::move(invite));
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) {
      return c >= 'A' && c <= 'F' ? static_cast<char>(c + ('a' - 'A'))
                                : static_cast<char>(c);
    });
    if (host.size() != 36 || host == native->EndpointId() ||
        !WatchPeer(host)) {
      SetStatus("Invalid host Aether ID");
      return;
    }
    resolving_host = std::move(host);
    next_join_request = 0;
    SetStatus("Finding the host room...");
  }

  void OnResolveRoom(std::string const& source, ae::ObjId room_id) {
    if (failed || !sync) return;
    if (!room_id.is_valid()) {
      auto* room = CurrentRoom();
      if (room && room->host_uid == native->EndpointId()) {
        link->ResolveRoom(source, room->obj_id);
      }
      return;
    }
    // Only the selected host can supply the room to expect. Do not accept
    // unsolicited snapshots or discovery replies from other participants.
    if (source != resolving_host || CurrentRoom() || app->expected.is_valid())
      return;
    app->Expect(room_id, source);
    resolving_host.clear();
    if (!CommitMemory()) {
      SetError("Could not commit the host selection");
      return;
    }
    BindExpected();
    next_join_request = 0;
  }

  bool Send(std::string text) {
    text = Trim(std::move(text));
    auto* room = CurrentRoom();
    if (failed || !sync || !room || text.empty()) {
      return false;
    }
    if (text.size() > 8192) {
      SetStatus("Message is too long (8192 bytes maximum)");
      return false;
    }
    auto const author = std::find_if(
        room->participants.begin(), room->participants.end(),
        [this](auto const& person) {
          return person->link->endpoint_uid == native->EndpointId();
        });
    if (author == room->participants.end()) {
      SetError("This client is not a room participant");
      return false;
    }
    auto event = TextEvent::ptr::Create(ae::CreateWith{*domain});
    event->author = *author;
    event->text = std::move(text);
    auto const message_id = native->EndpointId() + ":" +
        std::to_string(event->obj_id.id());
    if (!sync->CommitLocal(room->obj_id, std::move(event))) {
      SetError("Could not commit the message");
      return false;
    }
    local_message_start_us.emplace(message_id, current_monotonic_us);
    SetStatus("Connected");
    return true;
  }

  void OnJoinRequest(std::string const& source, ae::ObjId room_id,
                     std::string person_name) {
    auto* room = CurrentRoom();
    person_name = Trim(std::move(person_name));
    if (failed || !room || !sync || room->obj_id != room_id ||
        room->host_uid != native->EndpointId() || person_name.empty()) {
      return;
    }
    // A repeated request leaves the original frozen admission/snapshot intact.
    // The runtime retries it until the joining replica acknowledges its RAM
    // state.
    auto const known = std::any_of(
        room->links.begin(), room->links.end(),
        [&](auto const& peer) { return peer->endpoint_uid == source; });
    if (known) {
      return;
    }
    if (!WatchPeer(source)) {
      return;
    }
    auto event = JoinEvent::ptr::Create(ae::CreateWith{*domain});
    event->person = MakeParticipant(*domain, source, std::move(person_name));
    if (!sync->AdmitPeer(room_id, std::move(event), source)) {
      SetError("Could not save the participant admission");
    }
  }

  void DrainCommands() {
    std::deque<Command> pending;
    {
      std::lock_guard lock{command_mutex};
      pending.swap(commands);
    }
    for (auto& command : pending) {
      command.work(*domain);
    }
  }

  void Publish() {
    view_dirty = false;
    ChatView next;
    next.push_registered=!push_token.empty()&&registered_token==push_token;
    next.push_notify_status=push_notify_status;
    next.push_notify_request=push_notify_request;
    next.status = status;
    next.ready = sync && !failed && !finished && !sync->failed();
    if(connection)next.connection=connection->Observation();
    if(finished)next.connection.phase=TransportPhase::Stopped;
    next.locally_online=next.connection.phase==TransportPhase::Connected;
    if (native) {
      next.local_uid = native->EndpointId();
    }
    if (auto* room = CurrentRoom()) {
      next.in_room = true;
      next.retained_events = room->journal.size();
      for (auto const& peer : room->local_state->peers) {
        next.pending_packets += peer->pending_packets.size();
        next.pending_progress_packets += std::count_if(
            peer->pending_packets.begin(), peer->pending_packets.end(),
            [](auto const& packet) { return packet.progress; });
      }
      if (room->links.size() > 1) {
        next.tcp_path_verified = next.udp_path_verified = true;
        for (auto const& peer : room->links) {
          if (peer->endpoint_uid == next.local_uid) continue;
          auto const path = native->PeerPath(peer->endpoint_uid);
          next.tcp_path_verified = next.tcp_path_verified &&
                                   path.tcp &&
                                   !path.udp;
          next.udp_path_verified = next.udp_path_verified &&
                                   path.udp &&
                                   !path.tcp;
        }
      }
      next.invite = room->host_uid;
      for (auto const& person : room->participants) {
        auto const& uid = person->link->endpoint_uid;
        auto observation=std::find_if(next.connection.presence.begin(),next.connection.presence.end(),[&](auto const& p){return p.target==PresenceTarget{PresenceScope::Endpoint,uid};});
        auto remote=observation==next.connection.presence.end()?PresenceState::Unknown:observation->state;
        auto const availability =
            uid == next.local_uid
                ? (next.locally_online ? LinkAvailability::Online
                                       : LinkAvailability::Offline)
                : (remote==PresenceState::Online?LinkAvailability::Online:remote==PresenceState::Offline?LinkAvailability::Offline:LinkAvailability::Unknown);
        auto const presence =
            availability == LinkAvailability::Online    ? Presence::Online
            : availability == LinkAvailability::Offline ? Presence::Offline
                                                        : Presence::Unknown;
        next.participants.push_back({person->name, uid, presence});
      }
      for (auto const& message : room->messages) {
        bool delivered = false;
        auto separator = message.id.rfind(':');
        std::uint32_t raw_id = 0;
        if (separator != std::string::npos) {
          auto digits = message.id.substr(separator + 1);
          auto parsed = std::from_chars(digits.data(), digits.data()+digits.size(), raw_id);
          if (parsed.ec == std::errc{} && parsed.ptr == digits.data()+digits.size()) {
            bool has_recipient = false;
            delivered = true;
            for (auto const& peer : room->local_state->peers) {
              if (peer->link->endpoint_uid == message.author_uid) continue;
              has_recipient = true;
              delivered = delivered && peer->HasDelivered(ae::ObjId{raw_id});
            }
            delivered = delivered && has_recipient;
          }
        }
        if (delivered && message.author_uid == next.local_uid) {
          auto started = local_message_start_us.find(message.id);
          if (started != local_message_start_us.end() &&
              current_monotonic_us >= started->second) {
            local_message_ack_elapsed_us.emplace(
                message.id, current_monotonic_us - started->second);
            local_message_start_us.erase(started);
          }
        }
        next.messages.push_back(message.author_name + ": " + message.text);
        next.message_records.push_back({message.id, message.author_uid,
                                        message.author_name, message.text,
                                        room->message_times.contains(message.id)?room->message_times.at(message.id):0,delivered,
                                        local_message_ack_elapsed_us.contains(message.id)
                                            ? std::optional<std::uint64_t>{local_message_ack_elapsed_us.at(message.id)}
                                            : std::nullopt});
      }
    } else if (app.is_valid() && app->expected.is_valid()) {
      next.invite = app->source;
    } else if (!resolving_host.empty()) {
      next.invite = resolving_host;
    }
    next.connection.graph_attached=next.in_room;
    next.connection.pending_data=next.pending_packets-next.pending_progress_packets;
    next.connection.pending_progress=next.pending_progress_packets;
    {
      std::lock_guard lock{view_mutex};
      if (view == next) {
        return;
      }
      view = std::move(next);
    }
    if (on_changed) {
      on_changed();
    }
  }

  void Run() {
    struct CompleteCommands {
      Impl& owner;
      ~CompleteCommands() { owner.RejectPendingCommands(); }
    } complete_commands{*this};
    failed = false;
    finished = false;
    view_dirty = true;
    if (prepare_profile && !prepare_profile()) {
      SetError("Could not open the chat profile");
      finished = true;
      Publish();
      return;
    }
    if (name.size() > 256) {
      SetError("Name is too long (256 bytes maximum)");
      Publish();
      return;
    }
    EnsureSharedNodeRegistration();
    std::error_code directory_error;
    std::filesystem::create_directories(profile, directory_error);
    if (directory_error) {
      SetError("Could not create the chat profile");
      Publish();
      return;
    }
    connection=std::make_unique<ConnectionRuntime>(adapters_factory(profile / "sdk"));
    native=&connection->Transport();
    SetStatus("Connecting...");
    while (!stop_requested.load()) {
      auto time=ReadConnectionTime();
      auto const now=time.monotonic_us;
      current_monotonic_us = now;
      NetworkObservation network;
      {std::lock_guard lock{network_mutex};network=pending_network;}
      connection->NetworkChanged(network);
      connection->Service(time);
      if(native->Phase()==TransportPhase::Failed&&!failed)SetError("Transport connection failed");
      if(!sync&&!failed&&native->Ready()){
        if (!push_service.empty()) {
          native->ConfigureRawService(push_service,
              [this](auto const&, auto const& bytes) {
                bool const valid = push_reader.Feed(bytes, [this](push::Packet const& p) {
                  if (p.method == 0x81 && p.request == registration_request && p.status == 0)
                    registered_token = push_token;
                  if (p.method == 0x82 && p.request > push_notify_request && p.request < push_request) {
                    push_notify_status = p.status;
                    push_notify_request = p.request;
                  }
                  view_dirty = true;
                });
                if (!valid) std::cerr << "Rejected malformed push response\n";
              }, [this] { push_reader.Reset(); });
        }
        if(!InitializeModel())SetError("Could not initialize the chat");
      }
      auto observed=connection->Observation();
      if(observed!=last_connection){last_connection=std::move(observed);view_dirty=true;}
      if (sync && !failed) {
        DrainCommands();
        if (auto* room = CurrentRoom()) {
          for (auto const& peer : room->links) {
            if (peer->endpoint_uid != native->EndpointId()) {
              WatchPeer(peer->endpoint_uid);
            }
          }
        }

        ServicePush(now);
        if(auto* room=CurrentRoom()) {
          Observe(*room->local_state);
          for(auto const& peer:room->local_state->peers) Observe(*peer);
        }
        if (!resolving_host.empty() && now >= next_join_request) {
          link->ResolveRoom(resolving_host);
          next_join_request = now + 1000000ULL;
        } else if (app->expected.is_valid() && now >= next_join_request) {
          link->RequestJoin(app->source, app->expected, app->name);
          next_join_request = now + 1000000ULL;
        }
        sync->Service(now);
        if (auto* room = CurrentRoom(); room && now >= next_progress) {
          if (!sync->PublishProgress(room->obj_id)) {
            SetError("Could not persist delivery progress");
          } else {
            // Refusal is normal until every member has confirmed the prefix.
            (void)sync->CompactConfirmed(room->obj_id);
          }
          next_progress = now + 1000000ULL;
        }
        if (sync->failed()) {
          SetError("SharedNode could not commit its state");
        }
      }
      if (view_dirty) {
        Publish();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    // Complete commands already accepted by the proxy before shutdown.
    if (!failed && sync) {
      DrainCommands();
    }
    finished = true;
    if (!failed) {
      SetStatus("Stopped");
    }
    Publish();
    // Cancel callbacks and transport before their domain and SDK dependencies.
    sync.reset();
    link.reset();
    native=nullptr;
    connection.reset();
    app = {};
    domain.reset();
    model_storage.reset();

  }
};

#if APPTRAVERSE_CHAT_DISTILLATION
bool ExportChatInitialSupply(std::filesystem::path const& output) {
  EnsureSharedNodeRegistration();
  ae::RamDomainStorage supply;
  ae::Domain domain{supply};
  auto root =
      Application::ptr::Create(ae::CreateWith{domain}.with_id(kApplicationId));
  root->endpoint = {};
  root->name = {};
  root->room = {};
  root->expected = {};
  root->source = {};
  InitializeRuntimeNode(*root);
  FinalizeDistilledGraph(*root);
  StabilizeDistilledPersistentSnapshot(*root);
  SaveDistilledRoot(*root);
  return WriteEmbeddedDistilledSupplyHeader(
      output, "chat", "APPTRAVERSE_EMBEDDED_CHAT_SUPPLY_H_", kApplicationId, {},
      supply, CollectStoredObjectIds(supply));
}
#endif

ChatSession::ChatSession(std::filesystem::path profile, std::string name,
                         Changed changed)
    : ChatSession{std::move(profile), std::move(name), std::move(changed), {}} {
}
ChatSession::ChatSession(std::filesystem::path profile, std::string name,
                         Changed changed, PrepareProfile prepare_profile,
                         FlushProfile flush_profile, AdaptersFactory adapters)
    : impl_{std::make_unique<Impl>(std::move(profile), std::move(name),
                                   std::move(changed),
                                   std::move(prepare_profile),
                                   std::move(flush_profile), adapters?std::move(adapters):AdaptersFactory{[](auto const& path){return MakeAetherConnectionAdapters(path,"shared-node-chat");}})} {}
ChatSession::~ChatSession() { Stop(); }
void ChatSession::ConfigurePush(std::string uid) {
  std::transform(uid.begin(),uid.end(),uid.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
  impl_->push_service=std::move(uid);
}
void ChatSession::RegisterPushToken(std::string token) {
  if(token.empty()||token.size()>push::kMaxValue||!push::ValidUtf8(token)){
    std::cerr<<"Rejected invalid push token\n";
    return;
  }
  impl_->proxy.EnqueueModelWork([impl=impl_.get(),token=std::move(token)](ae::Domain&){
    // onResume fetches the current token again. Preserve an existing ACK or
    // in-flight request when it has not actually changed.
    if(impl->push_token==token)return;
    impl->push_token=token;impl->registered_token.clear();impl->registration_request=0;impl->next_registration=0;
    impl->view_dirty=true;
  });
}
void ChatSession::NotifyPushPeers() {
  impl_->proxy.EnqueueModelWork([impl=impl_.get()](ae::Domain&){impl->NotifyPush();});
}
void ChatSession::Start() {
  std::lock_guard lock{impl_->lifecycle_mutex};
  if (impl_->thread.joinable()) {
    return;
  }
  impl_->stop_requested.store(false);
  {
    std::lock_guard commands_lock{impl_->command_mutex};
    impl_->accepting_commands = true;
  }
  impl_->thread = std::thread{[impl = impl_.get()] { impl->Run(); }};
}
void ChatSession::Stop() {
  std::lock_guard lock{impl_->lifecycle_mutex};
  impl_->stop_requested.store(true);
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_->RejectPendingCommands();
}
void ChatSession::NetworkChanged(NetworkObservation value) {
  std::lock_guard lock{impl_->network_mutex};impl_->pending_network=value;
}
void ChatSession::Checkpoint(std::function<void(bool)> completion) {
  auto reject = [completion] { completion(false); };
  impl_->Enqueue(
      [impl = impl_.get(), completion = std::move(completion)](ae::Domain&) {
        bool const ok = impl->CommitMemory();
        if (!ok) impl->SetError("Could not persist chat checkpoint");
        completion(ok);
      }, std::move(reject));
}
void ChatSession::CreateRoom() {
  impl_->proxy.EnqueueModelWork(
      [impl = impl_.get()](ae::Domain&) { impl->CreateRoom(); });
}
void ChatSession::JoinRoom(std::string invite) {
  impl_->proxy.EnqueueModelWork(
      [impl = impl_.get(), invite = std::move(invite)](ae::Domain&) {
        impl->JoinRoom(invite);
      });
}
void ChatSession::Send(std::string text) {
  impl_->proxy.EnqueueModelWork([impl = impl_.get(), text = std::move(text)](
                                    ae::Domain&) { impl->Send(text); });
}
void ChatSession::Send(std::string text, std::function<void(bool)> committed) {
  auto const queued_at=MonotonicMicros();
  auto reject = [committed] { committed(false); };
  impl_->Enqueue(
      [impl = impl_.get(), text = std::move(text),
       committed = std::move(committed), queued_at](ae::Domain&) {
        auto const started=MonotonicMicros();
        bool const accepted=impl->Send(text);
        auto const completed=MonotonicMicros();
        // No message content or credentials. Distinguish a delayed local commit
        // from transport queueing; Online/FCM ACKs do not settle this operation.
        std::clog<<"Chat send accepted="<<accepted
                 <<" queue_ms="<<(started-queued_at)/1000
                 <<" commit_ms="<<(completed-started)/1000<<'\n';
        committed(accepted);
      }, std::move(reject));
}
ChatView ChatSession::View() const {
  std::lock_guard lock{impl_->view_mutex};
  return impl_->view;
}

}  // namespace apptraverse::chat
