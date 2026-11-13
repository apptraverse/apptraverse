#include "apptraverse/chat_session.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
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
#include "aether/ae_actions/select_client.h"
#include "aether/aether_app.h"
#include "apptraverse/aether_link.h"

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
  AE_REFLECT_MEMBERS(author_name, text)
};

class TextEvent;
class JoinEvent;
class Room : public NodeFor<Room, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::Room", Room, SharedNode, 0)
 protected:
  Room() = default;

 public:
  explicit Room(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(host_uid), AE_MMBR(participants), AE_MMBR(messages))
  template <typename Dnv>
  void Load(ae::Version<0>, Dnv& d) {
    d(base_, host_uid, participants, messages);
  }
  template <typename Dnv>
  void Save(ae::Version<0>, Dnv& d) const {
    d(base_, host_uid, participants, messages);
  }
  bool CanApply(TextEvent const&) const;
  bool CanApply(JoinEvent const&) const;
  void Apply(TextEvent const&);
  void Apply(JoinEvent const&);
  std::string host_uid;
  std::vector<Participant::ptr> participants;
  std::vector<ChatMessage> messages;
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
  messages.push_back({event.author->name, event.text});
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
  ChatLink(AetherByteLink& native, Join on_join, Resolve on_resolve)
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
  AetherByteLink& native_;
  Join on_join_;
  Resolve on_resolve_;
  Receiver receiver_;
};

}  // namespace

struct ChatSession::Impl {
  Impl(std::filesystem::path value, std::string user_name, Changed changed,
       PrepareProfile prepare)
      : profile{std::filesystem::absolute(std::move(value))},
        name{Trim(std::move(user_name))},
        on_changed{std::move(changed)},
        prepare_profile{std::move(prepare)},
        proxy{[this](ModelObjectProxy::ModelWork work) {
          std::lock_guard lock{command_mutex};
          if (!stop_requested.load()) {
            commands.push_back(std::move(work));
          }
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
  mutable std::mutex view_mutex;
  ChatView view;
  std::mutex lifecycle_mutex;
  std::mutex command_mutex;
  std::deque<ModelObjectProxy::ModelWork> commands;
  ModelObjectProxy proxy;
  std::atomic<bool> stop_requested{true};
  std::thread thread;

  // Everything below is owned and accessed exclusively by the model thread.
  std::unique_ptr<ae::AetherApp> sdk;
  ae::Client::ptr client;
  ae::Subscription selection;
  bool selected{false};
  bool selection_ok{false};
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
  std::unique_ptr<AetherByteLink> native;
  std::unique_ptr<ChatLink> link;
  std::unique_ptr<SharedSyncRuntime> sync;
  std::uint64_t next_join_request{0};
  std::uint64_t next_progress{0};
  std::uint64_t next_presence_publication{0};

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
  }

  void BindExpected() {
    if (!app->expected.is_valid()) {
      return;
    }
    native->WatchPeer(app->source);
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
        !native->WatchPeer(host)) {
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
    if (!sync->CommitLocal(room->obj_id, std::move(event))) {
      SetError("Could not commit the message");
      return false;
    }
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
    if (!native->WatchPeer(source)) {
      return;
    }
    auto event = JoinEvent::ptr::Create(ae::CreateWith{*domain});
    event->person = MakeParticipant(*domain, source, std::move(person_name));
    if (!sync->AdmitPeer(room_id, std::move(event), source)) {
      SetError("Could not save the participant admission");
    }
  }

  void DrainCommands() {
    std::deque<ModelObjectProxy::ModelWork> pending;
    {
      std::lock_guard lock{command_mutex};
      pending.swap(commands);
    }
    for (auto& work : pending) {
      work(*domain);
    }
  }

  void Publish() {
    view_dirty = false;
    ChatView next;
    next.status = status;
    next.ready = sync && !failed && !finished && !sync->failed();
    next.locally_online = client.is_valid() && client->IsLocallyOnline();
    if (native) {
      next.local_uid = native->EndpointId();
    }
    if (auto* room = CurrentRoom()) {
      next.in_room = true;
      next.retained_events = room->journal.size();
      for (auto const& peer : room->local_state->peers)
        next.pending_packets += peer->pending_packets.size();
      if (room->links.size() > 1) {
        next.tcp_path_verified = next.udp_path_verified = true;
        for (auto const& peer : room->links) {
          if (peer->endpoint_uid == next.local_uid) continue;
          auto const path = native->PeerStats(peer->endpoint_uid);
          next.tcp_path_verified = next.tcp_path_verified &&
                                   path.linked_tcp_endpoint &&
                                   !path.linked_udp_endpoint;
          next.udp_path_verified = next.udp_path_verified &&
                                   path.linked_udp_endpoint &&
                                   !path.linked_tcp_endpoint;
        }
      }
      next.invite = room->host_uid;
      for (auto const& person : room->participants) {
        auto const& uid = person->link->endpoint_uid;
        auto const availability =
            uid == next.local_uid
                ? (next.locally_online ? LinkAvailability::Online
                                       : LinkAvailability::Offline)
                : native->ObservedPresence(uid);
        auto const presence =
            availability == LinkAvailability::Online    ? Presence::Online
            : availability == LinkAvailability::Offline ? Presence::Offline
                                                        : Presence::Unknown;
        next.participants.push_back({person->name, uid, presence});
      }
      for (auto const& message : room->messages) {
        next.messages.push_back(message.author_name + ": " + message.text);
      }
    } else if (app.is_valid() && app->expected.is_valid()) {
      next.invite = app->source;
    } else if (!resolving_host.empty()) {
      next.invite = resolving_host;
    }
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
    sdk = ae::AetherApp::Construct(
        ae::AetherAppContext{[this]() -> std::unique_ptr<ae::IDomainStorage> {
          return std::make_unique<DirectoryDomainStorage>(profile / "sdk");
        }});
    if (!sdk) {
      SetError("Could not start Aether");
      Publish();
      return;
    }
    selected = false;
    selection_ok = false;
    SetStatus("Connecting to Aether...");
    selection =
        sdk->aether()
            ->SelectClient(
                ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"),
                "shared-node-chat")
            .result_event()
            .Subscribe([this](auto const& result) {
              selected = true;
              selection_ok = static_cast<bool>(result);
              if (selection_ok) {
                client = result.value();
              }
            });
    auto const started = MonotonicMicros();
    while (!stop_requested.load() && !sdk->IsExited()) {
      auto const wake = sdk->Update(ae::Now());
      auto const now = MonotonicMicros();
      if (!sync && !failed) {
        if ((selected && !selection_ok) ||
            (!selected && now - started > 120000000ULL)) {
          SetError("Aether registration failed or timed out");
        } else if (selection_ok) {
          client->connectivity_policy()
              .Load()
              ->ConfigureRxTimings()
              .ForAllPriorities(
                  ae::RxTimingConf::Every(std::chrono::milliseconds{1000}));
          // Identity storage is separate from the application's one USER file.
          sdk->aether().Save();
          AetherLinkOptions link_options;
          link_options.use_presence = true;
          link_options.gate_delivery_on_presence = false;
          native = std::make_unique<AetherByteLink>(*sdk->aether(), client,
                                                    link_options);
          if (!InitializeModel()) {
            SetError("Could not initialize the chat");
          }
        }
      }
      if (sync && !failed) {
        DrainCommands();
        if (auto* room = CurrentRoom()) {
          for (auto const& peer : room->links) {
            if (peer->endpoint_uid != native->EndpointId()) {
              native->WatchPeer(peer->endpoint_uid);
            }
          }
        }
        native->Service(now);
        if (now >= next_presence_publication) {
          view_dirty = true;
          next_presence_publication = now + 250000ULL;
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
      sdk->WaitUntil(std::min(wake, ae::Now() + std::chrono::milliseconds{10}));
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
    native.reset();
    app = {};
    domain.reset();
    model_storage.reset();
    selection.Reset();
    client = {};
    sdk.reset();
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
                         Changed changed, PrepareProfile prepare_profile)
    : impl_{std::make_unique<Impl>(std::move(profile), std::move(name),
                                   std::move(changed),
                                   std::move(prepare_profile))} {}
ChatSession::~ChatSession() { Stop(); }
void ChatSession::Start() {
  std::lock_guard lock{impl_->lifecycle_mutex};
  if (impl_->thread.joinable()) {
    return;
  }
  impl_->stop_requested.store(false);
  impl_->thread = std::thread{[impl = impl_.get()] { impl->Run(); }};
}
void ChatSession::Stop() {
  std::lock_guard lock{impl_->lifecycle_mutex};
  impl_->stop_requested.store(true);
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  std::lock_guard commands_lock{impl_->command_mutex};
  impl_->commands.clear();
}
void ChatSession::Checkpoint(std::function<void(bool)> completion) {
  impl_->proxy.EnqueueModelWork(
      [impl = impl_.get(), completion = std::move(completion)](ae::Domain&) {
        bool const ok = impl->CommitMemory();
        if (!ok) impl->SetError("Could not persist chat checkpoint");
        completion(ok);
      });
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
  impl_->proxy.EnqueueModelWork(
      [impl = impl_.get(), text = std::move(text),
       committed = std::move(committed)](ae::Domain&) {
        committed(impl->Send(text));
      });
}
ChatView ChatSession::View() const {
  std::lock_guard lock{impl_->view_mutex};
  return impl_->view;
}

}  // namespace apptraverse::chat
