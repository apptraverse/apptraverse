#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#ifdef RegisterClass
#undef RegisterClass
#endif
#endif

#include "aether/aether_app.h"
#include "aether/ae_actions/select_client.h"
#include "aether-miscpp/format/format.h"
#include "apptraverse/aether_link.h"
#include "apptraverse/runtime_node.h"
#include "apptraverse/shared_sync.h"
#include "apptraverse/single_file_user_storage.h"

namespace apptraverse::live_test {
namespace {
constexpr std::size_t kBaseSize = 16384;
std::uint64_t Micros() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::string Quote(std::string const& value) {
  std::string out{"\""};
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c >= 32) { out += static_cast<char>(c); }
    else { out += ' '; }
  }
  return out + '"';
}
bool AtomicText(std::filesystem::path const& path, std::string const& value) {
  auto temporary = path; temporary += ".tmp";
  { std::ofstream out{temporary, std::ios::binary | std::ios::trunc};
    out << value; out.flush(); if (!out) { return false; } }
#ifdef _WIN32
  return MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  std::error_code error; std::filesystem::rename(temporary, path, error);
  return !error;
#endif
}
std::string Arg(int argc, char** argv, std::string const& key, std::string fallback) {
  for (int i = 1; i + 1 < argc; ++i) { if (argv[i] == key) { return argv[i + 1]; } }
  return fallback;
}
std::string Digest(std::string const& text) {
  std::uint64_t value = 14695981039346656037ULL;
  for (unsigned char c : text) { value = (value ^ c) * 1099511628211ULL; }
  std::ostringstream out; out << std::hex << value; return out.str();
}

class Participant : public NodeFor<Participant> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::live_test::Participant", Participant, Node, 0)
 protected: Participant() = default;
 public:
  explicit Participant(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(name), AE_MMBR(link))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, name, link); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, name, link); }
  std::string name;
  SharedLinkDescriptor::ptr link;
};
class TextEvent;
class JoinEvent;
class Room : public NodeFor<Room, SharedNode> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::live_test::Room", Room, SharedNode, 0)
 protected: Room() = default;
 public:
  explicit Room(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(participants))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, text, participants); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, text, participants); }
  bool CanApply(TextEvent const&) const;
  bool CanApply(JoinEvent const&) const;
  void Apply(TextEvent const&);
  void Apply(JoinEvent const&);
  std::string text;
  std::vector<Participant::ptr> participants;
};
class TextEvent : public EventFor<Room, TextEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::live_test::TextEvent", TextEvent, Event, 0)
 protected: TextEvent() = default;
 public:
  explicit TextEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(text), AE_MMBR(author))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, text, author); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, text, author); }
  std::string text;
  Participant::ptr author;
};
class JoinEvent : public EventFor<Room, JoinEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::live_test::JoinEvent", JoinEvent, Event, 0)
 protected: JoinEvent() = default;
 public:
  explicit JoinEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(person))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, person); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, person); }
  Participant::ptr person;
};
bool Room::CanApply(TextEvent const& event) const {
  return event.author.is_valid() && std::any_of(participants.begin(), participants.end(),
      [&](auto const& person) { return person.id() == event.author.id(); });
}
bool Room::CanApply(JoinEvent const& event) const {
  return event.person.is_valid() && event.person->link.is_valid() &&
      std::none_of(links.begin(), links.end(), [&](auto const& link) {
        return link->endpoint_uid == event.person->link->endpoint_uid;
      });
}
void Room::Apply(TextEvent const& event) { text += event.text; }
void Room::Apply(JoinEvent const& event) {
  participants.push_back(event.person); links.push_back(event.person->link);
}

class AttachEvent;
class ExpectEvent;
class Application : public NodeFor<Application> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::live_test::Application", Application, Node, 0)
 protected: Application() = default;
 public:
  explicit Application(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(endpoint), AE_MMBR(room), AE_MMBR(expected), AE_MMBR(source))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, endpoint, room, expected, source); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, endpoint, room, expected, source); }
  void Attach(SharedNode::ptr value);
  void Expect(ae::ObjId id, std::string value);
  void Apply(AttachEvent const&);
  void Apply(ExpectEvent const&);
  std::string endpoint;
  SharedNode::ptr room;
  ae::ObjId expected;
  std::string source;
};
class AttachEvent : public EventFor<Application, AttachEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::live_test::AttachEvent", AttachEvent, Event, 0)
 protected: AttachEvent() = default;
 public:
  explicit AttachEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(room))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, room); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, room); }
  SharedNode::ptr room;
};
class ExpectEvent : public EventFor<Application, ExpectEvent> {
  APPTRAVERSE_NAMED_OBJECT("apptraverse::live_test::ExpectEvent", ExpectEvent, Event, 0)
 protected: ExpectEvent() = default;
 public:
  explicit ExpectEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(expected), AE_MMBR(source))
  template <typename Dnv> void Load(ae::Version<0>, Dnv& d) { d(base_, expected, source); }
  template <typename Dnv> void Save(ae::Version<0>, Dnv& d) const { d(base_, expected, source); }
  ae::ObjId expected;
  std::string source;
};
void Application::Attach(SharedNode::ptr value) {
  auto e = AttachEvent::ptr::Create(ae::CreateWith{*domain}); e->room = std::move(value); Commit(std::move(e));
}
void Application::Expect(ae::ObjId id, std::string value) {
  auto e = ExpectEvent::ptr::Create(ae::CreateWith{*domain});
  e->expected = id; e->source = std::move(value); Commit(std::move(e));
}
void Application::Apply(AttachEvent const& event) { room = event.room; expected = {}; source.clear(); }
void Application::Apply(ExpectEvent const& event) { expected = event.expected; source = event.source; }
APPTRAVERSE_REGISTER(Participant);
APPTRAVERSE_REGISTER(Room);
APPTRAVERSE_REGISTER(TextEvent);
APPTRAVERSE_REGISTER(JoinEvent);
APPTRAVERSE_REGISTER(Application);
APPTRAVERSE_REGISTER(AttachEvent);
APPTRAVERSE_REGISTER(ExpectEvent);

Participant::ptr MakePerson(ae::Domain& domain, std::string uid, std::string name) {
  auto link = SharedLinkDescriptor::ptr::Create(ae::CreateWith{domain});
  link->endpoint_uid = std::move(uid); InitializeRuntimeNode(*link);
  auto person = Participant::ptr::Create(ae::CreateWith{domain});
  person->link = link; person->name = std::move(name); InitializeRuntimeNode(*person); return person;
}

class CountingLink final : public IByteLink {
 public:
  CountingLink(AetherByteLink& link, std::string own) : link_{link}, own_{std::move(own)} {}
  void SetReceiver(Receiver receiver) override {
    if (!receiver) { link_.SetReceiver({}); return; }
    link_.SetReceiver([this, receiver = std::move(receiver)](auto const& source, auto const& bytes) {
      max_received = std::max(max_received, bytes.size());
      SharedSyncMessage message;
      if (DeserializeSharedSyncMessage(bytes, message) && message.kind == SharedSyncKind::Event) {
        ++received_events[source];
      }
      if (receiver) { receiver(source, bytes); }
    });
  }
  bool Send(std::string const& destination, std::vector<std::uint8_t> const& bytes) override {
    SharedSyncMessage message;
    bool const event = DeserializeSharedSyncMessage(bytes, message) && message.kind == SharedSyncKind::Event;
    if (event) {
      ++attempts[destination];
      if (message.timestamp_us <= 20) { ++old_event_attempts[destination]; }
      if (message.stamp.author_uid != own_) { ++foreign_relay; }
    }
    bool const accepted = link_.Send(destination, bytes);
    if (event && accepted) {
      ++accepted_events[destination];
      auto const key = std::make_pair(destination, message.event_id);
      auto const [previous, first] = last_accepted_event_us.emplace(key, service_clock_us);
      if (!first) {
        auto const gap = service_clock_us - previous->second;
        ++accepted_event_retries[destination];
        auto const [minimum, new_minimum] = min_accepted_event_retry_gap_us.emplace(destination, gap);
        if (!new_minimum) { minimum->second = std::min(minimum->second, gap); }
        previous->second = service_clock_us;
      }
    }
    return accepted;
  }
  LinkAvailability Availability(std::string const& peer) const override { return link_.Availability(peer); }
  std::map<std::string, std::uint64_t> attempts, accepted_events, received_events, old_event_attempts;
  // Only accepted whole-frame admissions of the SAME Event are retries.
  // Refused admission checks may occur every Service turn without wire sends.
  std::map<std::pair<std::string, ae::ObjId>, std::uint64_t> last_accepted_event_us;
  std::map<std::string, std::uint64_t> accepted_event_retries, min_accepted_event_retry_gap_us;
  std::uint64_t service_clock_us{0};
  std::size_t max_received{0}, foreign_relay{0};
 private:
  AetherByteLink& link_;
  std::string own_;
};

struct Worker {
  explicit Worker(AetherByteLink& transport) : link{transport}, counted{link, link.EndpointId()} {}
  AetherByteLink& link;
  CountingLink counted;
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  Application::ptr app;
  std::unique_ptr<SharedSyncRuntime> sync;
  std::uint64_t saves{0};
  bool stopped{false};
  std::string error;
  Room* room() const {
    if (!app.is_valid() || !app->room.is_valid()) { return nullptr; }
    auto held = domain->Find(app->room.id());
    if (!held || held->GetClassId() != Room::kClassId) { return nullptr; }
    return held.as<Room>();
  }
  bool Persist() {
    ae::DomainGraph graph{domain.get()};
    auto const saved = graph.SaveRoot(domain->Find(app.id()), app.id());
    if (saved.IsErr() || !SaveUserStorageFile(storage, "native")) { return false; }
    if (!AtomicText("root.id", std::to_string(app.id().id()))) { return false; }
    ++saves; return true;
  }
  void BindExpected() {
    if (app->expected.is_valid()) {
      sync->ExpectInitialState(app->expected, app->source, Room::kClassId,
          [this](SharedNode::ptr value) { app->Attach(std::move(value)); });
    }
  }
  bool Initialize() {
    if (!LoadUserStorageFile(storage, "native")) { return false; }
    domain = std::make_unique<ae::Domain>(storage);
    if (std::filesystem::exists("root.id")) {
      std::uint32_t id = 0; std::ifstream input{"root.id"}; input >> id;
      ae::DomainGraph graph{domain.get()}; auto held = graph.LoadRoot(ae::ObjId{id});
      if (!held || held->GetClassId() != Application::kClassId) { return false; }
      app = Application::ptr{domain.get(), ae::ObjId{id}, {}, held};
      if (app->endpoint != link.EndpointId()) { return false; }
    } else {
      if (!storage.state.empty()) { return false; }
      app = Application::ptr::Create(ae::CreateWith{*domain});
      app->endpoint = link.EndpointId(); InitializeRuntimeNode(*app);
      if (!Persist()) { return false; }
    }
    sync = std::make_unique<SharedSyncRuntime>(*domain, storage, counted,
        link.EndpointId(), [this] { return Persist(); }, 1000000);
    if (app->room.is_valid()) { sync->RegisterNode(app->room); }
    BindExpected(); return true;
  }
  bool Command(std::string const& line) {
    std::istringstream input{line}; std::string command; input >> command;
    if (command == "watch") {
      std::string peer; input >> peer; return link.WatchPeer(peer);
    }
    if (command == "boot") {
      std::string peer; input >> peer;
      if (room() || peer.size() != 36) { return false; }
      auto host = MakePerson(*domain, link.EndpointId(), "H");
      auto a = MakePerson(*domain, peer, "A");
      auto value = Room::ptr::Create(ae::CreateWith{*domain});
      value->text.assign(kBaseSize, 'x'); value->participants = {host, a};
      value->links = {host->link, a->link};
      SharedSyncRuntime::InitializeLocalState(*value); InitializeRuntimeNode(*value);
      app->Attach(value); sync->RegisterNode(value); return Persist();
    }
    if (command == "expect") {
      std::uint32_t id = 0; std::string source; input >> id >> source;
      if (room() || id == 0 || source.size() != 36) { return false; }
      app->Expect(ae::ObjId{id}, source); if (!Persist()) { return false; }
      BindExpected(); return true;
    }
    if (command == "stop") { stopped = true; return Persist(); }
    auto* current = room(); if (!current) { return false; }
    if (command == "seed") {
      auto event = TextEvent::ptr::Create(ae::CreateWith{*domain});
      event->author = current->participants.front(); event->text = "|H10";
      if (!sync->CommitLocal(current->obj_id, event, 10)) { return false; }
      return sync->QueueInitialState(current->obj_id, current->participants[1]->link->endpoint_uid);
    }
    if (command == "append") {
      std::uint64_t time = 0; std::string text; input >> time >> text;
      auto author = std::find_if(current->participants.begin(), current->participants.end(),
          [this](auto const& person) { return person->link->endpoint_uid == link.EndpointId(); });
      if (author == current->participants.end() || text.empty()) { return false; }
      auto event = TextEvent::ptr::Create(ae::CreateWith{*domain}); event->author = *author; event->text = text;
      return sync->CommitLocal(current->obj_id, event, time);
    }
    if (command == "admit") {
      std::string uid; std::uint64_t time = 0; input >> uid >> time;
      auto event = JoinEvent::ptr::Create(ae::CreateWith{*domain});
      event->person = MakePerson(*domain, uid, "B");
      return sync->AdmitPeer(current->obj_id, event, uid, time);
    }
    if (command == "progress") { std::uint64_t time = 0; input >> time; return sync->PublishProgress(current->obj_id, time); }
    if (command == "compact") { return sync->CompactConfirmed(current->obj_id); }
    return false;
  }
  void Commands() {
    std::vector<std::filesystem::path> files;
    for (auto const& entry : std::filesystem::directory_iterator{"commands"}) {
      if (entry.path().extension() == ".cmd") { files.push_back(entry.path()); }
    }
    std::sort(files.begin(), files.end());
    for (auto const& path : files) {
      std::ifstream input{path}; std::string line;
      // A temporary Windows sharing lock is not a rejected business command.
      // Leave the file for the next service turn; the controller has a deadline.
      if (!std::getline(input, line)) { continue; }
      input.close();
      bool const okay = Command(line);
      if (!okay) { std::cerr << "Rejected controller command: " << line << '\n'; }
      AtomicText(std::filesystem::path{"acks"} / (path.stem().string() + ".json"),
                  std::string{"{\"ok\":"} + (okay ? "true}" : "false}"));
      std::error_code ec; std::filesystem::remove(path, ec);
      if (!okay) { error = "command failed: " + line.substr(0, line.find(' ')); }
      if (stopped) { break; }
    }
  }
  std::string Status(bool locally_online) const {
    std::ostringstream out;
    out << "{\"selected\":true,\"uid\":" << Quote(link.EndpointId())
        << ",\"error\":" << Quote(error) << ",\"failed\":" << (sync->failed() ? "true" : "false")
        << ",\"saves\":" << saves << ",\"max_received\":" << counted.max_received
        << ",\"transport_accepted\":" << link.stats().data_accepted
        << ",\"transport_completed\":" << link.stats().data_completed
        << ",\"local_online\":" << (locally_online ? "true" : "false")
        << ",\"presence_queries\":" << link.stats().presence_queries
        << ",\"presence_errors\":" << link.stats().presence_errors
        << ",\"presence_timeouts\":" << link.stats().presence_timeouts
        << ",\"presence_online\":" << link.stats().presence_online_results
        << ",\"presence_offline\":" << link.stats().presence_offline_results
        << ",\"presence_unknown\":" << link.stats().presence_unknown_results
        << ",\"last_presence_error\":" << link.stats().last_presence_error
        << ",\"presence_cloud_servers\":" << link.stats().presence_cloud_servers
        << ",\"presence_queried_servers\":" << link.stats().presence_queried_servers
        << ",\"presence_timing_samples\":" << link.stats().presence_timing_samples
        << ",\"foreign_relay\":" << counted.foreign_relay;
    auto* current = room();
    out << ",\"room_id\":" << (current ? current->obj_id.id() : 0)
        << ",\"participants\":" << (current ? current->participants.size() : 0)
        << ",\"journal\":" << (current ? current->journal.size() : 0)
        << ",\"checkpoint\":" << (current ? current->local_state->checkpoint_us : 0)
        << ",\"size\":" << (current ? current->text.size() : 0)
        << ",\"digest\":" << Quote(current ? Digest(current->text) : "")
        << ",\"tail\":" << Quote(current && current->text.size() >= kBaseSize ? current->text.substr(kBaseSize) : "")
        << ",\"index\":" << (current ? current->local_state->ReceivedPrefix(link.EndpointId()).index : 0);
    std::size_t pending = 0; out << ",\"peers\":{"; bool first = true;
    if (current) {
      for (auto const& peer : current->local_state->peers) {
        auto const& uid = peer->link->endpoint_uid;
        auto const native = link.PeerStats(uid);
        if (!first) { out << ','; } first = false;
        auto const count = [](auto const& map, std::string const& id) {
          auto found = map.find(id); return found == map.end() ? 0ULL : found->second;
        };
        out << Quote(uid) << ":{\"availability\":" << Quote(link.Availability(uid) == LinkAvailability::Online ? "online" :
            link.Availability(uid) == LinkAvailability::Offline ? "offline" : "unknown")
            << ",\"pending\":" << peer->pending_packets.size()
            << ",\"event_attempts\":" << count(counted.attempts, uid)
            << ",\"old_event_attempts\":" << count(counted.old_event_attempts, uid)
            << ",\"event_accepted\":" << count(counted.accepted_events, uid)
            << ",\"event_accepted_retries\":" << count(counted.accepted_event_retries, uid)
            << ",\"min_event_accepted_retry_gap_us\":" << count(counted.min_accepted_event_retry_gap_us, uid)
            << ",\"events_received\":" << count(counted.received_events, uid)
            << ",\"presence_online\":" << native.presence_online_results
            << ",\"presence_offline\":" << native.presence_offline_results
            << ",\"presence_unknown\":" << native.presence_unknown_results
            << ",\"presence_errors\":" << native.presence_errors
            << ",\"last_presence_error\":" << native.last_presence_error << '}';
        pending += peer->pending_packets.size();
      }
    }
    out << "},\"pending\":" << pending << '}'; return out.str();
  }
};
}  // namespace
}  // namespace apptraverse::live_test

int main(int argc, char** argv) {
  using namespace apptraverse::live_test;  // Test executable entry.
  apptraverse::EnsureSharedNodeRegistration();
  auto const name = Arg(argc, argv, "--name", "shared-node-live");
  std::filesystem::create_directories("commands"); std::filesystem::create_directories("acks");
  AtomicText("status.json", "{\"selected\":false,\"error\":\"\"}");
  auto sdk = ae::AetherApp::Construct(ae::AetherAppContext{});
  ae::Client::ptr client; bool selected = false, okay = false;
  ae::Subscription selection = sdk->aether()->SelectClient(
      ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), name)
      .result_event().Subscribe([&](auto const& result) {
        selected = true; okay = static_cast<bool>(result); if (okay) { client = result.value(); }
      });
  auto const start = Micros();
  std::unique_ptr<apptraverse::AetherByteLink> link;
  std::unique_ptr<Worker> worker;
  std::uint64_t next_status = 0;
  std::uint64_t status_blocked_since = 0;
  while (Micros() - start < 600000000ULL && !sdk->IsExited()) {
    auto const wake = sdk->Update(ae::Now()); auto const now = Micros();
    if ((selected && !okay) || (!selected && now - start > 120000000ULL)) {
      AtomicText("status.json", "{\"selected\":false,\"error\":\"registration failed or timed out\"}"); return 2;
    }
    if (okay && !worker) {
      client->connectivity_policy().Load()->ConfigureRxTimings().ForAllPriorities(
          ae::RxTimingConf::Every(std::chrono::milliseconds{1000}));
      link = std::make_unique<apptraverse::AetherByteLink>(*sdk->aether(), client);
      worker = std::make_unique<Worker>(*link);
      if (!worker->Initialize()) {
        AtomicText("status.json", "{\"selected\":true,\"error\":\"native restore failed\"}"); return 3;
      }
      AtomicText("uid.txt", link->EndpointId());
    }
    if (worker) {
      worker->Commands();
      if (auto* room = worker->room()) {
        for (auto const& descriptor : room->links) {
          if (descriptor->endpoint_uid != link->EndpointId()) { link->WatchPeer(descriptor->endpoint_uid); }
        }
      }
      link->Service(now);
      worker->counted.service_clock_us = now;
      worker->sync->Service(now);
      if (now >= next_status || worker->stopped) {
        if (!AtomicText("status.json", worker->Status(client->IsLocallyOnline()))) {
#ifdef _WIN32
          auto const error = GetLastError();
          if (!status_blocked_since) {
            status_blocked_since = now;
            std::cerr << "status publication blocked, Win32 error " << error << '\n';
          }
          // This is disposable controller telemetry, not native persistence.
          // An open reader/indexer can briefly prevent Windows replacement.
          if ((error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED &&
               error != ERROR_LOCK_VIOLATION) || now - status_blocked_since > 5000000ULL) {
            return 4;
          }
          next_status = now + 20000;
#else
          return 4;
#endif
        } else {
          status_blocked_since = 0;
          next_status = now + 200000;
        }
      }
      if (worker->stopped) { worker.reset(); link.reset(); return 0; }
      if (worker->sync->failed() || !worker->error.empty()) { return 5; }
    }
    sdk->WaitUntil(std::min(wake, ae::Now() + std::chrono::milliseconds{10}));
  }
  AtomicText("status.json", "{\"error\":\"worker deadline expired\"}"); return 6;
}
