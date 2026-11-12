#include "chat_session.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/model_object_proxy.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync.h"
#include "apptraverse/single_file_user_storage.h"
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether/aether_app.h"
#include "aether/ae_actions/select_client.h"
#include "apptraverse/aether_link.h"

#ifdef RegisterClass
#undef RegisterClass
#endif

namespace apptraverse::chat {
namespace {

std::uint64_t MonotonicMicros() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
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
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::Participant", Participant, Node, 0)
 protected:
  Participant() = default;
 public:
  explicit Participant(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(link))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, name, link); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, name, link); }
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
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) {
    d(base_, host_uid, participants, messages);
  }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const {
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
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, author, text); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, author, text); }
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
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, person); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, person); }
  Participant::ptr person;
};

bool Room::CanApply(TextEvent const& event) const {
  return !event.text.empty() && event.author.is_valid() &&
      std::any_of(participants.begin(), participants.end(), [&](auto const& person) {
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
}
void Room::Apply(JoinEvent const& event) {
  participants.push_back(event.person);
  links.push_back(event.person->link);
}

class AttachEvent;
class ExpectEvent;
class Application : public NodeFor<Application> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::Application", Application, Node, 0)
 protected:
  Application() = default;
 public:
  explicit Application(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(endpoint), AE_MMBR(name), AE_MMBR(room),
                    AE_MMBR(expected), AE_MMBR(source))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) {
    d(base_, endpoint, name, room, expected, source);
  }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const {
    d(base_, endpoint, name, room, expected, source);
  }
  void Attach(SharedNode::ptr value);
  void Expect(ae::ObjId id, std::string uid);
  void Apply(AttachEvent const&);
  void Apply(ExpectEvent const&);
  std::string endpoint;
  std::string name;
  SharedNode::ptr room;
  ae::ObjId expected;
  std::string source;
};

class AttachEvent : public EventFor<Application, AttachEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::AttachEvent", AttachEvent, Event, 0)
 protected:
  AttachEvent() = default;
 public:
  explicit AttachEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(room))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, room); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, room); }
  SharedNode::ptr room;
};
class ExpectEvent : public EventFor<Application, ExpectEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::chat::ExpectEvent", ExpectEvent, Event, 0)
 protected:
  ExpectEvent() = default;
 public:
  explicit ExpectEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(expected), AE_MMBR(source))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, expected, source); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, expected, source); }
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
}
void Application::Apply(ExpectEvent const& event) {
  expected = event.expected;
  source = event.source;
}
APPTRAVERSE_REGISTER(Participant);
APPTRAVERSE_REGISTER(Room);
APPTRAVERSE_REGISTER(TextEvent);
APPTRAVERSE_REGISTER(JoinEvent);
APPTRAVERSE_REGISTER(Application);
APPTRAVERSE_REGISTER(AttachEvent);
APPTRAVERSE_REGISTER(ExpectEvent);

Participant::ptr MakeParticipant(ae::Domain& domain, std::string uid, std::string name) {
  auto descriptor = SharedLinkDescriptor::ptr::Create(ae::CreateWith{domain});
  descriptor->endpoint_uid = std::move(uid);
  InitializeRuntimeNode(*descriptor);
  auto person = Participant::ptr::Create(ae::CreateWith{domain});
  person->name = std::move(name);
  person->link = descriptor;
  InitializeRuntimeNode(*person);
  return person;
}

enum class ChatFrameKind : std::uint8_t { Sync = 1, JoinRequest = 2 };
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
  return archive.Load(frame).IsOk() && archive.buffer().read_position() == copy.size() &&
      frame.version == 1 &&
      ((frame.kind == ChatFrameKind::Sync && !frame.bytes.empty() && frame.name.empty()) ||
       (frame.kind == ChatFrameKind::JoinRequest && frame.room.is_valid() &&
        !frame.name.empty() && frame.name.size() <= 256 && frame.bytes.empty()));
}

class ChatLink final : public IByteLink {
 public:
  using Join = std::function<void(std::string const&, ae::ObjId, std::string)>;
  ChatLink(AetherByteLink& native, Join on_join) : native_{native}, on_join_{std::move(on_join)} {
    native_.SetReceiver([this](auto const& source, auto const& bytes) {
      ChatFrame frame;
      if (!DecodeFrame(bytes, frame)) { return; }
      if (frame.kind == ChatFrameKind::JoinRequest) {
        on_join_(source, frame.room, std::move(frame.name));
      } else if (receiver_) {
        receiver_(source, frame.bytes);
      }
    });
  }
  ~ChatLink() override { native_.SetReceiver({}); }
  void SetReceiver(Receiver receiver) override { receiver_ = std::move(receiver); }
  bool Send(std::string const& destination, std::vector<std::uint8_t> const& bytes) override {
    ChatFrame frame;
    frame.bytes = bytes;
    std::vector<std::uint8_t> encoded;
    return EncodeFrame(frame, encoded) && native_.Send(destination, encoded);
  }
  LinkAvailability Availability(std::string const& peer) const override {
    return native_.Availability(peer);
  }
  bool RequestJoin(std::string const& host, ae::ObjId room, std::string const& name) {
    ChatFrame frame;
    frame.kind = ChatFrameKind::JoinRequest;
    frame.room = room;
    frame.name = name;
    std::vector<std::uint8_t> encoded;
    return EncodeFrame(frame, encoded) && native_.Send(host, encoded);
  }
 private:
  AetherByteLink& native_;
  Join on_join_;
  Receiver receiver_;
};

}  // namespace

struct ChatSession::Impl {
  Impl(std::filesystem::path value, std::string user_name)
      : profile{std::filesystem::absolute(std::move(value))}, name{Trim(std::move(user_name))},
        proxy{[this](ModelObjectProxy::ModelWork work) {
          std::lock_guard lock{command_mutex};
          if (!stop_requested.load()) { commands.push_back(std::move(work)); }
        }} {
    if (name.empty()) { name = "User"; }
    view.status = "Starting Aether...";
  }

  std::filesystem::path profile;
  std::string name;
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
  std::string status;
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  Application::ptr app;
  std::unique_ptr<AetherByteLink> native;
  std::unique_ptr<ChatLink> link;
  std::unique_ptr<SharedSyncRuntime> sync;
  std::uint64_t next_join_request{0};

  Room* CurrentRoom() const {
    if (!app.is_valid() || !app->room.is_valid()) { return nullptr; }
    auto held = domain->Find(app->room.id());
    if (!held || held->GetClassId() != Room::kClassId) { return nullptr; }
    return held.as<Room>();
  }

  bool Persist() {
    ae::DomainGraph graph{domain.get()};
    auto const saved = graph.SaveRoot(domain->Find(app.id()), app.id());
    if (saved.IsErr() || !SaveUserStorageFile(storage, profile / "native")) { return false; }
    // This sidecar is the native Application root identity, not a checkpoint.
    // It is created once; updates replace only the native user.bin.
    auto const root_path = profile / "root.id";
    if (!std::filesystem::exists(root_path)) {
      std::ofstream output{root_path, std::ios::binary | std::ios::trunc};
      output << app.id().id();
      output.flush();
      if (!output) { return false; }
    }
    return true;
  }

  void SetError(std::string message) {
    status = std::move(message);
    failed = true;
  }

  void BindExpected() {
    if (!app->expected.is_valid()) { return; }
    native->WatchPeer(app->source);
    sync->ExpectInitialState(app->expected, app->source, Room::kClassId,
        [this](SharedNode::ptr value) {
          app->Attach(std::move(value));
          status = "Connected";
        });
    status = "Waiting for the host...";
  }

  bool InitializeModel() {
    if (!LoadUserStorageFile(storage, profile / "native")) { return false; }
    domain = std::make_unique<ae::Domain>(storage);
    auto const root_path = profile / "root.id";
    if (std::filesystem::exists(root_path)) {
      std::uint32_t root = 0;
      std::ifstream input{root_path};
      input >> root;
      if (!input || root == 0) { return false; }
      ae::DomainGraph graph{domain.get()};
      auto held = graph.LoadRoot(ae::ObjId{root});
      if (!held || held->GetClassId() != Application::kClassId) { return false; }
      app = Application::ptr{domain.get(), ae::ObjId{root}, {}, held};
      if (app->endpoint != native->EndpointId()) { return false; }
      if (app->room.is_valid() && !CurrentRoom()) { return false; }
    } else {
      if (!storage.state.empty()) { return false; }
      app = Application::ptr::Create(ae::CreateWith{*domain});
      app->endpoint = native->EndpointId();
      app->name = name;
      InitializeRuntimeNode(*app);
      if (!Persist()) { return false; }
    }
    link = std::make_unique<ChatLink>(*native,
        [this](auto const& source, ae::ObjId room, std::string person_name) {
          OnJoinRequest(source, room, std::move(person_name));
        });
    sync = std::make_unique<SharedSyncRuntime>(*domain, storage, *link,
        native->EndpointId(), [this] { return Persist(); });
    if (app->room.is_valid()) { sync->RegisterNode(app->room); }
    status = CurrentRoom() ? "Connected" : "Create a room or paste an invitation";
    BindExpected();
    return true;
  }

  void CreateRoom() {
    if (failed || !sync || CurrentRoom() || app->expected.is_valid()) { return; }
    auto host = MakeParticipant(*domain, native->EndpointId(), app->name);
    auto room = Room::ptr::Create(ae::CreateWith{*domain});
    room->host_uid = native->EndpointId();
    room->participants = {host};
    room->links = {host->link};
    SharedSyncRuntime::InitializeLocalState(*room);
    InitializeRuntimeNode(*room);
    app->Attach(room);
    sync->RegisterNode(room);
    if (!Persist()) { SetError("Could not save the room"); return; }
    status = "Room created. Share the invitation";
  }

  void JoinRoom(std::string invite) {
    if (failed || !sync || CurrentRoom() || app->expected.is_valid()) { return; }
    invite = Trim(std::move(invite));
    auto const slash = invite.find('/');
    std::uint32_t id = 0;
    if (slash != 36 || slash + 1 >= invite.size()) { status = "Invalid invitation"; return; }
    auto const host = invite.substr(0, slash);
    auto const number = invite.substr(slash + 1);
    auto const parsed = std::from_chars(number.data(), number.data() + number.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || id == 0 ||
        host == native->EndpointId() || !native->WatchPeer(host)) {
      status = "Invalid invitation";
      return;
    }
    app->Expect(ae::ObjId{id}, host);
    if (!Persist()) { SetError("Could not save the invitation"); return; }
    BindExpected();
    next_join_request = 0;
  }

  void Send(std::string text) {
    text = Trim(std::move(text));
    auto* room = CurrentRoom();
    if (failed || !sync || !room || text.empty()) { return; }
    if (text.size() > 8192) { status = "Message is too long (8192 bytes maximum)"; return; }
    auto const author = std::find_if(room->participants.begin(), room->participants.end(),
        [this](auto const& person) { return person->link->endpoint_uid == native->EndpointId(); });
    if (author == room->participants.end()) { SetError("This client is not a room participant"); return; }
    auto event = TextEvent::ptr::Create(ae::CreateWith{*domain});
    event->author = *author;
    event->text = std::move(text);
    if (!sync->CommitLocal(room->obj_id, std::move(event))) {
      SetError("Could not save the message");
      return;
    }
    status = "Connected";
  }

  void OnJoinRequest(std::string const& source, ae::ObjId room_id, std::string person_name) {
    auto* room = CurrentRoom();
    person_name = Trim(std::move(person_name));
    if (failed || !room || !sync || room->obj_id != room_id ||
        room->host_uid != native->EndpointId() || person_name.empty()) { return; }
    // A repeated request leaves the original frozen admission/snapshot intact.
    // The runtime retries it until the joining replica durably acknowledges it.
    auto const known = std::any_of(room->links.begin(), room->links.end(),
        [&](auto const& peer) { return peer->endpoint_uid == source; });
    if (known) { return; }
    if (!native->WatchPeer(source)) { return; }
    auto event = JoinEvent::ptr::Create(ae::CreateWith{*domain});
    event->person = MakeParticipant(*domain, source, std::move(person_name));
    if (!sync->AdmitPeer(room_id, std::move(event), source)) {
      SetError("Could not save the participant admission");
    }
  }

  void DrainCommands() {
    std::deque<ModelObjectProxy::ModelWork> pending;
    { std::lock_guard lock{command_mutex}; pending.swap(commands); }
    for (auto& work : pending) { work(*domain); }
  }

  void Publish() {
    ChatView next;
    next.status = status;
    next.ready = sync && !failed && !sync->failed();
    if (native) { next.local_uid = native->EndpointId(); }
    if (auto* room = CurrentRoom()) {
      next.in_room = true;
      next.invite = room->host_uid + "/" + std::to_string(room->obj_id.id());
      for (auto const& person : room->participants) {
        auto const& uid = person->link->endpoint_uid;
        next.participants.push_back({person->name, uid});
      }
      for (auto const& message : room->messages) {
        next.messages.push_back(message.author_name + ": " + message.text);
      }
    } else if (app.is_valid() && app->expected.is_valid()) {
      next.invite = app->source + "/" + std::to_string(app->expected.id());
    }
    std::lock_guard lock{view_mutex};
    view = std::move(next);
  }

  void Run() {
    failed = false;
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
    sdk = ae::AetherApp::Construct(ae::AetherAppContext{
        [this]() -> std::unique_ptr<ae::IDomainStorage> {
          return std::make_unique<DirectoryDomainStorage>(profile / "sdk");
        }});
    if (!sdk) {
      SetError("Could not start Aether");
      Publish();
      return;
    }
    selected = false;
    selection_ok = false;
    status = "Connecting to Aether...";
    selection = sdk->aether()->SelectClient(
        ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), "shared-node-chat")
        .result_event().Subscribe([this](auto const& result) {
          selected = true;
          selection_ok = static_cast<bool>(result);
          if (selection_ok) { client = result.value(); }
        });
    auto const started = MonotonicMicros();
    std::uint64_t next_publication = 0;
    while (!stop_requested.load() && !sdk->IsExited()) {
      auto const wake = sdk->Update(ae::Now());
      auto const now = MonotonicMicros();
      if (!sync && !failed) {
        if ((selected && !selection_ok) || (!selected && now - started > 120000000ULL)) {
          SetError("Aether registration failed or timed out");
        } else if (selection_ok) {
          client->connectivity_policy().Load()->ConfigureRxTimings().ForAllPriorities(
              ae::RxTimingConf::Every(std::chrono::milliseconds{1000}));
          // The native Application binds its identity to this SDK client. Save
          // credentials before that binding can be written or acknowledged.
          sdk->aether().Save();
          native = std::make_unique<AetherByteLink>(*sdk->aether(), client);
          if (!InitializeModel()) { SetError("Could not restore this chat profile"); }
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
        if (app->expected.is_valid() && now >= next_join_request) {
          link->RequestJoin(app->source, app->expected, app->name);
          next_join_request = now + 1000000ULL;
        }
        sync->Service(now);
        if (sync->failed()) { SetError("SharedNode could not save its state"); }
      }
      if (now >= next_publication) {
        Publish();
        next_publication = now + 100000ULL;
      }
      sdk->WaitUntil(std::min(wake, ae::Now() + std::chrono::milliseconds{10}));
    }
    // Closing immediately after Send must retain commands already accepted by
    // the proxy. Their durable packets will resume delivery on the next start.
    if (!failed && sync) { DrainCommands(); }
    if (!failed && app.is_valid() && !Persist()) { SetError("Could not save the chat profile"); }
    if (!failed) { status = "Stopped"; }
    Publish();
    // Cancel callbacks and transport before their domain and SDK dependencies.
    sync.reset();
    link.reset();
    native.reset();
    app = {};
    domain.reset();
    selection.Reset();
    client = {};
    sdk.reset();
    { std::lock_guard lock{view_mutex}; view.ready = false; }
  }
};

ChatSession::ChatSession(std::filesystem::path profile, std::string name)
    : impl_{std::make_unique<Impl>(std::move(profile), std::move(name))} {}
ChatSession::~ChatSession() { Stop(); }
void ChatSession::Start() {
  std::lock_guard lock{impl_->lifecycle_mutex};
  if (impl_->thread.joinable()) { return; }
  impl_->stop_requested.store(false);
  impl_->thread = std::thread{[impl = impl_.get()] { impl->Run(); }};
}
void ChatSession::Stop() {
  std::lock_guard lock{impl_->lifecycle_mutex};
  impl_->stop_requested.store(true);
  if (impl_->thread.joinable()) { impl_->thread.join(); }
  std::lock_guard commands_lock{impl_->command_mutex};
  impl_->commands.clear();
}
void ChatSession::CreateRoom() {
  impl_->proxy.EnqueueModelWork([impl = impl_.get()](ae::Domain&) { impl->CreateRoom(); });
}
void ChatSession::JoinRoom(std::string invite) {
  impl_->proxy.EnqueueModelWork([impl = impl_.get(), invite = std::move(invite)](ae::Domain&) {
    impl->JoinRoom(invite);
  });
}
void ChatSession::Send(std::string text) {
  impl_->proxy.EnqueueModelWork([impl = impl_.get(), text = std::move(text)](ae::Domain&) {
    impl->Send(text);
  });
}
ChatView ChatSession::View() const {
  std::lock_guard lock{impl_->view_mutex};
  return impl_->view;
}

}  // namespace apptraverse::chat
