// Test-only headless harness: runs the real chat business stack (ChatComponent,
// ChatSyncController, RoomMembershipController) with no Win32 UI, no threads and
// no Aether transport. Two runtimes live in one process, each with its own state
// directory, model domain, room state and UID.
//
// What stands in for production infrastructure:
//   - HeadlessChatPresenter replaces the Win32 presenter (stores snapshots).
//   - HeadlessMemoryTransport replaces AetherP2pTransport: it carries the real
//     serialized bytes produced by the stack between runtimes and owns the
//     per-peer session generation, exactly like the transport does. It does not
//     synthesize packets and never inspects payloads.
#ifndef APPTRAVERSE_TESTS_HEADLESS_ROOM_RUNTIME_H_
#define APPTRAVERSE_TESTS_HEADLESS_ROOM_RUNTIME_H_

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether-miscpp/format/format.h"
#include "aether/types/uid.h"

#include "apptraverse/domain_snapshot_io.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_macros.h"

#include "chat_component.h"
#include "chat_component_graph.h"
#include "chat_peer_schedule.h"
#include "chat_presence.h"
#include "chat_presentation.h"
#include "chat_transcript.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_component_registration.h"
#include "model/chat_events.h"
#include "model/chat_peer_set.h"
#include "model/chat_room_local_state.h"
#include "model/client.h"
#include "room_inbound_demux.h"
#include "room_membership_controller.h"

namespace apptraverse::testing {

using apptraverse::chat::ChatComponent;
using apptraverse::chat::ChatPresentationSnapshot;
using apptraverse::chat::ChatSyncTiming;
using apptraverse::chat::ChatTimelineItemKind;
using apptraverse::chat::RoomMembershipController;
using apptraverse::chat::RoomMembershipHooks;
using apptraverse::chat::RoomUiStatus;

// ---------------------------------------------------------------------------
// In-memory trace
// ---------------------------------------------------------------------------

struct TraceEvent {
  std::int64_t timestamp_us{0};
  std::string marker;
  std::string side;
  std::uint64_t event_id{0};
  std::uint64_t packet_id{0};
  std::string detail;
};

class HeadlessTrace {
 public:
  using Entry = TraceEvent;

  void Event(std::string side, std::string marker, std::string detail = {},
             std::uint64_t event_id = 0, std::uint64_t packet_id = 0) {
    entries_.push_back(TraceEvent{NowUs(), std::move(marker), std::move(side),
                                  event_id, packet_id, std::move(detail)});
  }

  std::vector<TraceEvent> const& entries() const { return entries_; }

  // First timestamp of `marker` at or after `since_us`, or nullopt.
  std::optional<std::int64_t> FirstAfter(std::string_view marker,
                                         std::int64_t since_us) const {
    for (auto const& e : entries_) {
      if (e.marker == marker && e.timestamp_us >= since_us) {
        return e.timestamp_us;
      }
    }
    return std::nullopt;
  }

  std::optional<std::int64_t> FirstAfter(std::string_view side,
                                         std::string_view marker,
                                         std::int64_t since_us) const {
    for (auto const& e : entries_) {
      if (e.side == side && e.marker == marker && e.timestamp_us >= since_us) {
        return e.timestamp_us;
      }
    }
    return std::nullopt;
  }

  static std::int64_t NowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

 private:
  std::vector<TraceEvent> entries_;
};

// ---------------------------------------------------------------------------
// Headless presenter
// ---------------------------------------------------------------------------

class HeadlessChatPresenter {
 public:
  void Apply(ChatPresentationSnapshot const& snapshot) {
    snapshot_ = snapshot;
    ++apply_count_;
  }

  void SetSendEnabled(bool enabled) { send_enabled_ = enabled; }
  void SetRoomStatus(RoomUiStatus status) { room_status_ = status; }

  // "name: text" for messages, "* name joined" for joins, in journal order.
  std::vector<std::string> Lines() const {
    std::vector<std::string> out;
    out.reserve(snapshot_.timeline.size());
    for (auto const& item : snapshot_.timeline) {
      if (item.kind == ChatTimelineItemKind::kJoined) {
        out.push_back("* " + item.author.display_name + " joined");
      } else {
        std::string line = item.author.display_name + ": " + item.text;
        if (item.show_offline_marker) {
          line += " ";
          line += apptraverse::chat::kOfflinePingMarker;
        }
        out.push_back(std::move(line));
      }
    }
    return out;
  }

  std::vector<std::string> Messages() const {
    std::vector<std::string> out;
    for (auto const& item : snapshot_.timeline) {
      if (item.kind == ChatTimelineItemKind::kMessage) {
        out.push_back(item.text);
      }
    }
    return out;
  }

  std::size_t CountMessage(std::string const& text) const {
    std::size_t n = 0;
    for (auto const& item : snapshot_.timeline) {
      if (item.kind == ChatTimelineItemKind::kMessage && item.text == text) {
        ++n;
      }
    }
    return n;
  }

  std::size_t JoinCount() const {
    std::size_t n = 0;
    for (auto const& item : snapshot_.timeline) {
      if (item.kind == ChatTimelineItemKind::kJoined) {
        ++n;
      }
    }
    return n;
  }

  std::vector<std::string> Participants() const {
    std::vector<std::string> out;
    for (auto const& item : snapshot_.timeline) {
      if (item.kind != ChatTimelineItemKind::kJoined) {
        continue;
      }
      if (std::find(out.begin(), out.end(), item.author.display_name) ==
          out.end()) {
        out.push_back(item.author.display_name);
      }
    }
    return out;
  }

  std::size_t PendingCount() const {
    std::size_t n = 0;
    for (auto const& peer : snapshot_.peers) {
      n += peer.pending_packets;
    }
    return n;
  }

  bool SendEnabled() const { return send_enabled_; }
  RoomUiStatus RoomStatus() const { return room_status_; }
  bool Running() const { return snapshot_.running; }
  std::uint64_t apply_count() const { return apply_count_; }
  ChatPresentationSnapshot const& snapshot() const { return snapshot_; }

 private:
  ChatPresentationSnapshot snapshot_{};
  bool send_enabled_{false};
  RoomUiStatus room_status_{RoomUiStatus::kDisconnected};
  std::uint64_t apply_count_{0};
};

// ---------------------------------------------------------------------------
// Logical transport
// ---------------------------------------------------------------------------
//
// The business stack never names a transport type: ChatComponent takes
// SendFunction / RawSendFunction / ConnectFunction and is told about sessions
// through NotifyTransportSessionReady. That callback set is the logical
// transport contract:
//
//   Send(peer, bytes)
//   OnReceive(peer, bytes)          -> ChatComponent::Receive / room.OnControl
//   OnSessionReady(peer, generation)-> ChatComponent::NotifyTransportSessionReady
//   OnSessionLost(peer)             -> no business entry point exists today;
//                                      loss is inferred from presence timeout.
//
// AetherP2pTransport implements it in production; HeadlessMemoryTransport
// implements it for tests. Neither production file is touched.

class HeadlessRoomRuntime;

// The four calls the business stack needs from any transport. Implemented by
// HeadlessMemoryTransport (deterministic, in-process) and by the Aether-backed
// bridge used to run the same scenarios over the real cloud.
class IHeadlessTransport {
 public:
  virtual ~IHeadlessTransport() = default;

  virtual void Register(HeadlessRoomRuntime* runtime) = 0;
  virtual void Unregister(HeadlessRoomRuntime* runtime) = 0;
  virtual void Send(ae::Uid const& from, ae::Uid const& to,
                    std::vector<std::uint8_t> bytes) = 0;
  virtual void Connect(ae::Uid const& from, ae::Uid const& to) = 0;
  // Drop the outbound session and build a new one (new generation).
  virtual void ReconnectSession(ae::Uid const& from, ae::Uid const& to) = 0;
};

class HeadlessMemoryTransport : public IHeadlessTransport {
 public:
  explicit HeadlessMemoryTransport(HeadlessTrace& trace) : trace_{trace} {}

  // Process attach / detach (a stopped process has no endpoint at all).
  void Register(HeadlessRoomRuntime* runtime) override;
  void Unregister(HeadlessRoomRuntime* runtime) override;

  void Send(ae::Uid const& from, ae::Uid const& to,
            std::vector<std::uint8_t> bytes) override;
  // Creates or reuses a session and reports readiness once reachable.
  void Connect(ae::Uid const& from, ae::Uid const& to) override;
  void ReconnectSession(ae::Uid const& from, ae::Uid const& to) override;

  // Endpoint stays in the process but its link is down: frames to and from it
  // are dropped and its sessions are lost.
  void Disconnect(ae::Uid const& uid);
  // Link is up again: every session touching the endpoint gets a new
  // generation and both sides observe SessionReady.
  void Reconnect(ae::Uid const& uid);
  bool IsConnected(ae::Uid const& uid) const {
    return disconnected_.count(UidText(uid)) == 0;
  }

  // Global deterministic loss switch, independent of endpoint state.
  void Drop(bool drop_all) { drop_all_ = drop_all; }

  std::uint64_t SessionGeneration(ae::Uid const& from,
                                  ae::Uid const& to) const {
    auto it = generations_.find(SessionKey(from, to));
    return it == generations_.end() ? 0 : it->second;
  }

  // Deliver one queued frame. Returns false when the queue is empty.
  bool PumpOnce();
  // Deliver everything currently queued.
  std::size_t Pump() {
    std::size_t n = 0;
    while (PumpOnce()) {
      ++n;
    }
    return n;
  }
  void DrainQueue() { queue_.clear(); }

 private:
  struct Frame {
    ae::Uid from{};
    ae::Uid to{};
    std::vector<std::uint8_t> bytes;
  };

  static std::string UidText(ae::Uid const& uid) {
    return ae::Format("{}", uid);
  }
  static std::string SessionKey(ae::Uid const& from, ae::Uid const& to) {
    return ae::Format("{}->{}", from, to);
  }

  HeadlessRoomRuntime* Find(ae::Uid const& uid);
  // Forget every session that touches `uid`, so the next Connect re-announces.
  void InvalidateSessions(ae::Uid const& uid);
  void DropQueuedFor(ae::Uid const& uid);

  HeadlessTrace& trace_;
  std::vector<HeadlessRoomRuntime*> runtimes_;
  std::deque<Frame> queue_;
  // Session generation per directed pair, mirroring transport generations.
  std::unordered_map<std::string, std::uint64_t> generations_;
  std::unordered_map<std::string, bool> announced_;
  std::unordered_set<std::string> disconnected_;
  bool drop_all_{false};
};

// ---------------------------------------------------------------------------
// Runtime
// ---------------------------------------------------------------------------

class HeadlessRoomRuntime {
 public:
  HeadlessRoomRuntime(std::filesystem::path state_dir,
                      IHeadlessTransport& transport,
                      HeadlessTrace& trace, std::string role_label)
      : state_dir_{std::move(state_dir)},
        transport_{transport},
        trace_{trace},
        role_label_{std::move(role_label)} {
    // Sync packet classes live in the core registration; chat model classes in
    // the component registration. Both are required before Declare/Load.
    EnsureObjectRegistration();
    EnsureChatComponentRegistration();
  }

  ~HeadlessRoomRuntime() { Stop(); }

  HeadlessRoomRuntime(HeadlessRoomRuntime const&) = delete;
  HeadlessRoomRuntime& operator=(HeadlessRoomRuntime const&) = delete;

  // One-shot model creation, equivalent to the product "distill" step.
  void Distill(ChatRoomRole role, std::string name, ae::Uid uid,
               std::optional<ae::Uid> host_uid = std::nullopt) {
    std::filesystem::remove_all(state_dir_);
    std::filesystem::create_directories(ModelRoot());

    ae::RamDomainStorage ram;
    ae::Domain domain{ae::Now(), ram};
    auto const policy = role == ChatRoomRole::kHost
                            ? chat::LocalJoinPolicy::kJoinLocal
                            : chat::LocalJoinPolicy::kDoNotJoinLocal;
    auto graph = chat::BuildChatComponentGraph(domain, name, policy);

    auto room = ChatRoomLocalState::ptr::Create(
        ae::CreateWith{domain}.with_id(
            ToObjId(ApplicationObjId::ChatRoomLocalState)));
    room->role = role;
    room->local_client_obj_id = graph.local_client.id().id();
    room->local_display_name = name;
    if (host_uid.has_value()) {
      room->host_uid = ae::Format("{}", *host_uid);
    }
    room->active_membership_revision = 0;
    room.Save();

    graph.chat_base.Save();
    graph.chat.Save();
    graph.client_base.Save();
    graph.local_client.Save();
    graph.peer_set_base.Save();
    graph.peer_set.Save();
    SaveDirectorySnapshot(ram, ModelRoot());

    SaveIdentity(uid);
  }

  // Build the business stack from the persisted state directory and start it.
  void Start() {
    assert(component_ == nullptr);
    uid_ = LoadIdentity();
    storage_ = std::make_unique<ae::RamDomainStorage>();
    LoadDirectorySnapshot(ModelRoot(), *storage_);
    domain_ = std::make_unique<ae::Domain>(ae::Now(), *storage_);

    chat_ = Chat::ptr::Declare(ae::CreateWith{*domain_}.with_id(
        ToObjId(ApplicationObjId::Chat)));
    chat_.Load();
    assert(chat_.is_loaded());
    peer_set_ = chat_->peer_set;
    peer_set_.Load();
    assert(peer_set_.is_loaded());

    room_state_ = LoadChatRoomLocalState(*domain_);
    assert(room_state_.is_loaded());
    local_client_ = Client::ptr::Declare(ae::CreateWith{*domain_}.with_id(
        room_state_->local_client_obj_id));
    local_client_.Load();
    assert(local_client_.is_loaded());

    trace_.Event(role_label_, "PROCESS_START",
                 ae::Format("revision={}",
                            room_state_->active_membership_revision));

    component_ = std::make_unique<ChatComponent>(
        SyncReplica{*domain_, *storage_, chat_.id()}, local_client_, chat_,
        [this](ae::Uid const& peer, ae::ObjId packet_id,
               SerializedSyncPacket const& bytes) {
          trace_.Event(role_label_, "SYNC_WRITE",
                       ae::Format("bytes={}", bytes.size()), 0,
                       packet_id.id());
          transport_.Send(uid_, peer, bytes);
        },
        [this](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
          transport_.Send(uid_, peer, bytes);
        },
        [this](ae::Uid const& peer) { transport_.Connect(uid_, peer); },
        ChatSyncTiming{},
        [this](std::string const& line) { OnComponentLog(line); });

    RoomMembershipHooks hooks{};
    hooks.send_control = [this](ae::Uid const& peer,
                                std::vector<std::uint8_t> const& bytes) {
      transport_.Send(uid_, peer, bytes);
    };
    hooks.connect_peer = [this](ae::Uid const& peer) {
      transport_.Connect(uid_, peer);
    };
    hooks.add_chat_peer = [this](ae::Uid const& peer) {
      if (component_ != nullptr) {
        (void)component_->AddPeer(peer);
      }
    };
    hooks.ensure_host_join = [this](ae::Uid const& uid,
                                    std::uint32_t client_obj_id,
                                    std::string const& name) {
      return EnsureHostJoin(uid, client_obj_id, name);
    };
    hooks.has_local_join = [this] {
      return component_ != nullptr && component_->HasLocalJoin();
    };
    hooks.probe_local_join = [this] {
      chat::RoomLocalJoinIdentity id{};
      if (component_ == nullptr) {
        return id;
      }
      auto const probe = component_->ProbeLocalJoin();
      id.local_client_obj_id = probe.local_client_obj_id;
      id.join_client_obj_id = probe.join_client_obj_id;
      id.obj_id_match =
          probe.kind == ChatComponent::LocalJoinMatchKind::kObjId;
      id.name_fallback =
          probe.kind == ChatComponent::LocalJoinMatchKind::kNameFallback;
      return id;
    };
    hooks.on_ui_changed = [this] { SyncUiFromRoom(); };
    hooks.on_model_changed = [this] { PublishPresentation(); };
    hooks.log = [this](std::string const& line) {
      trace_.Event(role_label_, "ROOM_TRANSITION", line);
    };

    room_ = std::make_unique<RoomMembershipController>(
        room_state_->role, uid_, local_client_.id().id(),
        room_state_->local_display_name, room_state_, hooks);

    component_->SetIncomingPeerAuthorize([this](ae::Uid const& peer) {
      return room_ != nullptr && room_->IsAuthorizedSyncPeer(peer);
    });
    component_->SetReconnectPeer([this](ae::Uid const& peer) {
      trace_.Event(role_label_, "STALE_PATH_RECONNECT", ae::Format("{}", peer));
      transport_.ReconnectSession(uid_, peer);
    });
    if (schedule_query_) {
      component_->SetQueryPeerSchedule(schedule_query_);
    }
    // The controller can reach Active inside its own constructor (host role),
    // before room_ is assigned, so publish the initial UI state explicitly.
    SyncUiFromRoom();

    transport_.Register(this);

    if (room_state_->role == ChatRoomRole::kHost) {
      room_->HostBootstrap();
      for (auto const& p : room_->ActiveParticipants()) {
        if (p.uid == uid_) {
          continue;
        }
        transport_.Connect(uid_, p.uid);
      }
    } else if (!room_state_->host_uid.empty()) {
      room_->ClientConnect(
          ae::Uid::FromString(std::string_view{room_state_->host_uid}));
    }

    component_->Start();
    if (room_state_->role == ChatRoomRole::kHost) {
      // AddPeer requires a running component (mirrors the Windows runtime).
      for (auto const& p : room_->ActiveParticipants()) {
        if (p.uid == uid_) {
          continue;
        }
        (void)component_->AddPeer(p.uid);
      }
    }
    SyncUiFromRoom();
    PublishPresentation();
  }

  void StartHost(std::string name, ae::Uid uid) {
    Distill(ChatRoomRole::kHost, std::move(name), uid);
    Start();
  }

  void StartClient(std::string name, ae::Uid uid, ae::Uid host_uid) {
    Distill(ChatRoomRole::kClient, std::move(name), uid, host_uid);
    Start();
  }

  void AnnounceNextPingUnknown() {
    if (announce_next_ping_unknown_) {
      announce_next_ping_unknown_();
    }
    trace_.Event(role_label_, "AETHER_NEXT_PING_UNKNOWN_SENT");
  }

  void Stop() {
    if (component_ == nullptr) {
      return;
    }
    AnnounceNextPingUnknown();
    trace_.Event(role_label_, "PROCESS_STOP");
    transport_.Unregister(this);
    component_->Stop();
    component_.reset();
    room_.reset();
    // Persist exactly what the process had, so a restart resumes this state.
    SaveDirectorySnapshot(*storage_, ModelRoot());
    chat_ = Chat::ptr{};
    peer_set_ = ChatPeerSet::ptr{};
    local_client_ = Client::ptr{};
    room_state_ = ChatRoomLocalState::ptr{};
    domain_.reset();
    storage_.reset();
  }

  ae::Uid Uid() const { return uid_; }
  bool IsRunning() const { return component_ != nullptr; }
  HeadlessChatPresenter& Presenter() { return presenter_; }
  RoomMembershipController* Room() { return room_.get(); }
  ChatComponent* Component() { return component_.get(); }

  void SetQueryPeerSchedule(chat::QueryPeerScheduleFunction fn) {
    schedule_query_ = std::move(fn);
    if (component_ != nullptr) {
      component_->SetQueryPeerSchedule(schedule_query_);
    }
  }
  void SetQueryPeerOnlineSchedule(chat::QueryPeerScheduleFunction fn) {
    SetQueryPeerSchedule(std::move(fn));
  }
  void SetAnnounceNextPingUnknown(std::function<void()> fn) {
    announce_next_ping_unknown_ = std::move(fn);
  }

  void InjectPeerOnline(ae::Uid const& from) {
    if (component_ == nullptr) {
      return;
    }
    component_->Receive(from, chat::EncodeChatPresence(
                                  chat::ChatPresenceMessage::kOnline));
    PublishPresentation();
  }
  std::filesystem::path const& StateDir() const { return state_dir_; }

  std::string LocalName() const {
    return room_state_.is_loaded() ? room_state_->local_display_name
                                   : std::string{};
  }
  std::uint32_t LocalClientObjId() const {
    return local_client_.is_valid() ? local_client_.id().id() : 0;
  }
  ChatRoomRole Role() const {
    return room_state_.is_loaded() ? room_state_->role : ChatRoomRole::kHost;
  }
  std::uint64_t AppliedRevision() const {
    return room_ != nullptr ? room_->applied_revision() : 0;
  }

  void ConnectTo(ae::Uid host) {
    assert(room_ != nullptr);
    room_->ClientConnect(host);
  }

  bool Send(std::string text) {
    assert(component_ != nullptr);
    if (room_ == nullptr || !room_->CanSendChat()) {
      return false;
    }
    auto const id = component_->SubmitText(std::move(text));
    if (!id.has_value()) {
      return false;
    }
    PublishPresentation();
    return true;
  }

  void Tick() {
    if (component_ == nullptr) {
      return;
    }
    auto const now = ae::Now();
    component_->Tick(now);
    room_->Tick(now);
  }

  // Transport reports a (re)established session for this peer.
  void OnSessionReady(ae::Uid const& peer, std::uint64_t generation) {
    if (component_ == nullptr) {
      return;
    }
    trace_.Event(role_label_, "SESSION_READY",
                 ae::Format("generation={}", generation));
    component_->NotifyTransportSessionReady(peer, generation);
    if (room_ != nullptr && room_->ui_status() == RoomUiStatus::kActive &&
        Role() == ChatRoomRole::kClient) {
      room_->ClientNudgeReconnect();
    }
    PublishPresentation();
  }

  // Inbound frame from the link: same demux order as the Windows runtime.
  void Deliver(ae::Uid const& from, std::vector<std::uint8_t> const& bytes) {
    if (component_ == nullptr) {
      return;
    }
    trace_.Event(role_label_, "SYNC_RECEIVE", std::to_string(bytes.size()));
    std::optional<chat::RoomControlMessage> msg;
    auto const kind = chat::ClassifyRoomControlInbound(bytes, &msg);
    if (kind == chat::RoomInboundKind::kRoomControlOk) {
      room_->OnControl(from, *msg);
    } else if (kind == chat::RoomInboundKind::kRoomControlDecodeFail) {
      // Drop corrupt control; never treat as Chat sync.
    } else {
      component_->Receive(from, bytes);
    }
    if (component_->HasLocalJoin()) {
      room_->NotifyLocalJoinAppeared();
    }
    PublishPresentation();
  }

  std::size_t PendingPackets() const {
    return presenter_.PendingCount();
  }

 private:
  std::filesystem::path ModelRoot() const { return state_dir_ / "model"; }
  std::filesystem::path IdentityPath() const {
    return state_dir_ / "identity.txt";
  }

  void SaveIdentity(ae::Uid const& uid) const {
    std::ofstream out{IdentityPath(), std::ios::binary | std::ios::trunc};
    out << ae::Format("{}", uid);
  }

  ae::Uid LoadIdentity() const {
    std::ifstream in{IdentityPath(), std::ios::binary};
    std::string text;
    in >> text;
    return ae::Uid::FromString(std::string_view{text});
  }

  void SyncUiFromRoom() {
    if (room_ == nullptr) {
      return;
    }
    bool const send_ok = room_->CanSendChat();
    auto const status = room_->ui_status();
    if (send_ok != presenter_.SendEnabled()) {
      trace_.Event(role_label_, "SEND_ENABLED_CHANGED",
                   send_ok ? "true" : "false");
    }
    if (status != presenter_.RoomStatus()) {
      if (status == RoomUiStatus::kActive) {
        trace_.Event(role_label_, "ROOM_ACTIVE_CHANGED", "true");
      } else if (presenter_.RoomStatus() == RoomUiStatus::kActive) {
        trace_.Event(role_label_, "ROOM_ACTIVE_CHANGED", "false");
      }
      trace_.Event(role_label_, "ROOM_STATUS",
                   std::to_string(static_cast<int>(status)));
    }
    presenter_.SetSendEnabled(send_ok);
    presenter_.SetRoomStatus(status);
  }

  void PublishPresentation() {
    if (component_ == nullptr) {
      return;
    }
    presenter_.Apply(component_->CapturePresentation());
    trace_.Event(role_label_, "SYNC_PRESENTATION",
                 ae::Format("timeline={} pending={}",
                            presenter_.snapshot().timeline.size(),
                            presenter_.PendingCount()));
  }

  // Maps real ChatSyncController / SharedGraphSyncSession markers onto the
  // in-memory trace. Nothing is written to stdout or stderr.
  void OnComponentLog(std::string const& line) {
    if (line.rfind("CHAT_EVENT_COMMITTED", 0) == 0) {
      trace_.Event(role_label_, "EVENT_COMMITTED", line);
    } else if (line.rfind("CHAT_PENDING_CHANGED", 0) == 0) {
      trace_.Event(role_label_,
                   line.find("pending=0") == std::string::npos
                       ? "PENDING_ADDED"
                       : "PENDING_REMOVED",
                   line);
    } else if (line.rfind("SYNC_PENDING_REMOVED", 0) == 0) {
      trace_.Event(role_label_, "PENDING_REMOVED", line);
    } else if (line.rfind("SYNC_EVENT_APPLIED", 0) == 0 ||
               line.rfind("SYNC_NODE_STATE_RECEIVED", 0) == 0) {
      trace_.Event(role_label_, "SYNC_APPLY", line);
    } else if (line.rfind("SYNC_RECONNECT_FLUSH", 0) == 0) {
      trace_.Event(role_label_, "FLUSH", line);
    } else if (line.rfind("SYNC_STALE_PATH_RECONNECT", 0) == 0) {
      trace_.Event(role_label_, "STALE_PATH_DECIDED", line);
    } else if (line.rfind("SYNC_STALE_PATH_SKIPPED", 0) == 0) {
      trace_.Event(role_label_, "STALE_PATH_SKIPPED", line);
    } else if (line.rfind("SYNC_ACK_RECEIVED", 0) == 0) {
      trace_.Event(role_label_, "ACK", line);
    } else if (line.rfind("CHAT_PEER_OFFLINE", 0) == 0) {
      trace_.Event(role_label_, "PEER_OFFLINE", line);
      if (room_ != nullptr && Role() == ChatRoomRole::kClient) {
        room_->ClientNudgeReconnect();
      }
    } else if (line.rfind("CHAT_SYNC_PAYLOAD_WRITE", 0) == 0) {
      trace_.Event(role_label_, "CHAT_SYNC_PAYLOAD_WRITE", line);
    } else if (line.rfind("SYNC_PAYLOAD_RETRY_HELD", 0) == 0) {
      trace_.Event(role_label_, "SYNC_PAYLOAD_RETRY_HELD", line);
    } else if (line.rfind("SYNC_PAYLOAD_RETRY_ALLOWED", 0) == 0) {
      trace_.Event(role_label_, "SYNC_PAYLOAD_RETRY_ALLOWED", line);
    } else if (line.rfind("SYNC_RETRY_HELD_SCHEDULE", 0) == 0) {
      trace_.Event(role_label_, "SYNC_RETRY_HELD_SCHEDULE", line);
    } else if (line.rfind("SYNC_RETRY_ALLOWED", 0) == 0) {
      trace_.Event(role_label_, "SYNC_RETRY_ALLOWED", line);
    } else if (line.rfind("SYNC_RETRY_HELD", 0) == 0) {
      trace_.Event(role_label_, "RETRY_HELD", line);
    } else if (line.rfind("PEER_UAP_QUERY_BEGIN", 0) == 0) {
      trace_.Event(role_label_, "PEER_UAP_QUERY_BEGIN", line);
    } else if (line.rfind("PEER_UAP_QUERY_RESULT", 0) == 0) {
      trace_.Event(role_label_, "PEER_UAP_QUERY_RESULT", line);
    } else if (line.rfind("PEER_PING_DEADLINE", 0) == 0) {
      trace_.Event(role_label_, "PEER_PING_DEADLINE", line);
    } else if (line.rfind("PEER_PING_ADVANCED", 0) == 0) {
      trace_.Event(role_label_, "PEER_PING_ADVANCED", line);
    } else if (line.rfind("PEER_PING_MISSED", 0) == 0) {
      trace_.Event(role_label_, "PEER_PING_MISSED", line);
    } else if (line.rfind("PEER_NO_FUTURE_PING", 0) == 0) {
      trace_.Event(role_label_, "PEER_NO_FUTURE_PING", line);
    } else if (line.rfind("PEER_ONLINE_REARMED", 0) == 0) {
      trace_.Event(role_label_, "PEER_ONLINE_REARMED", line);
    } else if (line.rfind("OFFLINE_PING_MARKER_ON", 0) == 0) {
      trace_.Event(role_label_, "OFFLINE_PING_MARKER_ON", line);
    } else if (line.rfind("OFFLINE_PING_MARKER_OFF", 0) == 0) {
      trace_.Event(role_label_, "OFFLINE_PING_MARKER_OFF", line);
    } else if (line.rfind("PEER_SCHEDULE_QUERY", 0) == 0) {
      trace_.Event(role_label_, "PEER_SCHEDULE_QUERY", line);
    } else if (line.rfind("PEER_SCHEDULE_RESULT", 0) == 0) {
      trace_.Event(role_label_, "PEER_SCHEDULE_RESULT", line);
    } else if (line.rfind("PEER_SCHEDULE_DEADLINE", 0) == 0) {
      trace_.Event(role_label_, "PEER_SCHEDULE_DEADLINE", line);
    } else if (line.rfind("PEER_SCHEDULE_ADVANCED", 0) == 0) {
      trace_.Event(role_label_, "PEER_SCHEDULE_ADVANCED", line);
    } else if (line.rfind("PEER_SCHEDULE_MISSED", 0) == 0) {
      trace_.Event(role_label_, "PEER_SCHEDULE_MISSED", line);
    } else if (line.rfind("PEER_OFFLINE_MISSED_VISIT", 0) == 0) {
      trace_.Event(role_label_, "PEER_OFFLINE_MISSED_VISIT", line);
    } else if (line.rfind("PEER_ONLINE_NOTIFY", 0) == 0) {
      trace_.Event(role_label_, "PEER_ONLINE_NOTIFY", line);
    } else if (line.rfind("PEER_RETRY_REARMED", 0) == 0) {
      trace_.Event(role_label_, "PEER_RETRY_REARMED", line);
    } else if (line.rfind("OFFLINE_MARKER_ON", 0) == 0) {
      trace_.Event(role_label_, "OFFLINE_MARKER_ON", line);
    } else if (line.rfind("OFFLINE_MARKER_OFF", 0) == 0) {
      trace_.Event(role_label_, "OFFLINE_MARKER_OFF", line);
    } else if (line.rfind("CHAT_STARTUP_NOTIFY", 0) == 0) {
      trace_.Event(role_label_, "STARTUP_NOTIFY", line);
    } else if (line.rfind("CHAT_PEER_REJOINED", 0) == 0) {
      trace_.Event(role_label_, "PEER_REJOINED", line);
    }
  }

  // Real host-side Join commit (same logic as the Windows runtime hook).
  bool EnsureHostJoin(ae::Uid const&, std::uint32_t client_obj_id,
                      std::string const& name) {
    chat_.Load();
    for (auto const& record : chat_->journal) {
      if (!record.event.is_valid() ||
          record.event->GetClassId() != JoinClientEvent::kClassId) {
        continue;
      }
      auto join = JoinClientEvent::ptr{record.event};
      join.Load();
      if (join.is_loaded() && join->client.is_valid() &&
          join->client.id().id() == client_obj_id) {
        return false;
      }
    }
    auto client_base = Client::ptr::Create(ae::CreateWith{*domain_});
    auto client = Client::ptr::Create(
        ae::CreateWith{*domain_}.with_id(client_obj_id));
    client->name = name;
    client->base = client_base;
    client->CaptureBaseState();
    client.Save();
    auto join = JoinClientEvent::ptr::Create(ae::CreateWith{*domain_});
    join->client = client;
    chat_->Commit(join);
    chat_.Save();
    for (auto const& record : chat_->journal) {
      if (record.event.is_valid() && record.event.id() == join.id()) {
        component_->PublishCommittedJournalEvent(record);
        break;
      }
    }
    trace_.Event(role_label_, "HOST_JOIN_COMMITTED", name);
    return true;
  }

  std::filesystem::path state_dir_;
  IHeadlessTransport& transport_;
  HeadlessTrace& trace_;
  std::string role_label_;

  ae::Uid uid_{};
  std::unique_ptr<ae::RamDomainStorage> storage_;
  std::unique_ptr<ae::Domain> domain_;
  Chat::ptr chat_;
  ChatPeerSet::ptr peer_set_;
  Client::ptr local_client_;
  ChatRoomLocalState::ptr room_state_;
  std::unique_ptr<ChatComponent> component_;
  std::unique_ptr<RoomMembershipController> room_;
  chat::QueryPeerScheduleFunction schedule_query_;
  std::function<void()> announce_next_ping_unknown_;
  HeadlessChatPresenter presenter_;
};

// ---------------------------------------------------------------------------
// Transport implementation (needs the complete runtime type)
// ---------------------------------------------------------------------------

inline void HeadlessMemoryTransport::Register(HeadlessRoomRuntime* runtime) {
  if (std::find(runtimes_.begin(), runtimes_.end(), runtime) !=
      runtimes_.end()) {
    return;
  }
  runtimes_.push_back(runtime);
  disconnected_.erase(UidText(runtime->Uid()));
  // A returning endpoint re-links the streams of peers that already had a
  // session toward it: each of them observes a new session generation.
  auto const uid = runtime->Uid();
  std::vector<HeadlessRoomRuntime*> relink;
  for (auto* other : runtimes_) {
    if (other == runtime) {
      continue;
    }
    if (generations_.count(SessionKey(other->Uid(), uid)) != 0) {
      relink.push_back(other);
    }
  }
  for (auto* other : relink) {
    Connect(other->Uid(), uid);
  }
}

inline void HeadlessMemoryTransport::Unregister(HeadlessRoomRuntime* runtime) {
  runtimes_.erase(std::remove(runtimes_.begin(), runtimes_.end(), runtime),
                  runtimes_.end());
  auto const uid = runtime->Uid();
  DropQueuedFor(uid);
  InvalidateSessions(uid);
}

inline void HeadlessMemoryTransport::InvalidateSessions(ae::Uid const& uid) {
  auto const uid_text = UidText(uid);
  for (auto& [key, announced] : announced_) {
    if (announced && key.find(uid_text) != std::string::npos) {
      announced = false;
      trace_.Event("transport", "SESSION_LOST", key);
    }
  }
}

inline void HeadlessMemoryTransport::DropQueuedFor(ae::Uid const& uid) {
  auto const before = queue_.size();
  queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                              [&uid](Frame const& f) {
                                return f.to == uid || f.from == uid;
                              }),
               queue_.end());
  if (queue_.size() != before) {
    trace_.Event("transport", "TRANSPORT_DROP",
                 ae::Format("in_flight={}", before - queue_.size()));
  }
}

inline void HeadlessMemoryTransport::Disconnect(ae::Uid const& uid) {
  disconnected_.insert(UidText(uid));
  DropQueuedFor(uid);
  InvalidateSessions(uid);
  trace_.Event("transport", "TRANSPORT_DISCONNECT", UidText(uid));
}

inline void HeadlessMemoryTransport::Reconnect(ae::Uid const& uid) {
  disconnected_.erase(UidText(uid));
  trace_.Event("transport", "TRANSPORT_RECONNECT", UidText(uid));
  // Both directions re-link, so both sides see a fresh generation.
  std::vector<std::pair<ae::Uid, ae::Uid>> pairs;
  for (auto* other : runtimes_) {
    if (other->Uid() == uid) {
      continue;
    }
    if (generations_.count(SessionKey(other->Uid(), uid)) != 0) {
      pairs.emplace_back(other->Uid(), uid);
    }
    if (generations_.count(SessionKey(uid, other->Uid())) != 0) {
      pairs.emplace_back(uid, other->Uid());
    }
  }
  for (auto const& [from, to] : pairs) {
    Connect(from, to);
  }
}

inline HeadlessRoomRuntime* HeadlessMemoryTransport::Find(ae::Uid const& uid) {
  for (auto* runtime : runtimes_) {
    if (runtime->Uid() == uid) {
      return runtime;
    }
  }
  return nullptr;
}

inline void HeadlessMemoryTransport::Send(ae::Uid const& from,
                                          ae::Uid const& to,
                                          std::vector<std::uint8_t> bytes) {
  // No endpoint, a downed link, or the global drop switch: the frame is lost
  // and the sender keeps it pending, exactly as on the wire.
  if (drop_all_ || Find(to) == nullptr || !IsConnected(to) ||
      !IsConnected(from)) {
    trace_.Event("transport", "TRANSPORT_DROP", SessionKey(from, to));
    return;
  }
  trace_.Event("transport", "TRANSPORT_SEND", SessionKey(from, to));
  queue_.push_back(Frame{from, to, std::move(bytes)});
}

inline void HeadlessMemoryTransport::Connect(ae::Uid const& from,
                                             ae::Uid const& to) {
  auto* dest = Find(to);
  auto* src = Find(from);
  if (dest == nullptr || src == nullptr || !IsConnected(from) ||
      !IsConnected(to)) {
    return;
  }
  auto const key = SessionKey(from, to);
  auto& announced = announced_[key];
  if (announced) {
    return;
  }
  announced = true;
  auto const generation = ++generations_[key];
  src->OnSessionReady(to, generation);
}

inline void HeadlessMemoryTransport::ReconnectSession(ae::Uid const& from,
                                                     ae::Uid const& to) {
  auto const key = SessionKey(from, to);
  auto it = announced_.find(key);
  if (it != announced_.end() && it->second) {
    it->second = false;
    trace_.Event("transport", "SESSION_LOST", key);
  }
  Connect(from, to);
}

inline bool HeadlessMemoryTransport::PumpOnce() {
  if (queue_.empty()) {
    return false;
  }
  auto frame = std::move(queue_.front());
  queue_.pop_front();
  auto* dest = Find(frame.to);
  if (dest == nullptr || !IsConnected(frame.to) || !IsConnected(frame.from)) {
    trace_.Event("transport", "TRANSPORT_DROP",
                 SessionKey(frame.from, frame.to));
    return true;
  }
  trace_.Event("transport", "TRANSPORT_DELIVER",
               SessionKey(frame.from, frame.to));
  dest->Deliver(frame.from, frame.bytes);
  return true;
}

// ---------------------------------------------------------------------------
// Pump helpers
// ---------------------------------------------------------------------------

// Advances both runtimes and the transport until `done` or `timeout`.
template <typename DoneFn>
bool PumpUntil(HeadlessMemoryTransport& transport,
               std::vector<HeadlessRoomRuntime*> const& runtimes,
               DoneFn done, std::chrono::milliseconds timeout) {
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    for (int i = 0; i < 64 && transport.PumpOnce(); ++i) {
    }
    for (auto* runtime : runtimes) {
      runtime->Tick();
    }
    if (done()) {
      return true;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
}

inline void PumpFor(HeadlessMemoryTransport& transport,
                    std::vector<HeadlessRoomRuntime*> const& runtimes,
                    std::chrono::milliseconds duration) {
  (void)PumpUntil(transport, runtimes, [] { return false; }, duration);
}

}  // namespace apptraverse::testing

#endif  // APPTRAVERSE_TESTS_HEADLESS_ROOM_RUNTIME_H_
