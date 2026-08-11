#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/directory_domain_storage.h"
#include "apptraverse/event_for.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_graph_copy.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_graph.h"
#include "apptraverse/shared_graph_sync_session.h"
#include "apptraverse/sync_packet.h"
#include "apptraverse/sync_session_state.h"
#include "apptraverse/unreliable_memory_link.h"
#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"
#include "model/window.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

#include "../examples/single_client_chat/common/chat_transcript.h"
#include "../examples/single_client_chat/common/graph_builder.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class FakeChatPresenter : public ChatPresenter {
  APPTRAVERSE_OBJECT(FakeChatPresenter, ChatPresenter, 0)
 protected:
  FakeChatPresenter() = default;
 public:
  explicit FakeChatPresenter(ae::ObjProp prop) : ChatPresenter{prop} {}
  AE_OBJECT_REFLECT()
};

class FakeWindow : public NodeFor<FakeWindow, Window> {
  APPTRAVERSE_OBJECT(FakeWindow, Window, 0)
 protected:
  FakeWindow() = default;
 public:
  explicit FakeWindow(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT()
  void Apply(WindowChangedEvent const&) override {}
};

class FakeWindowPresenter : public WindowPresenter {
  APPTRAVERSE_OBJECT(FakeWindowPresenter, WindowPresenter, 0)
 protected:
  FakeWindowPresenter() = default;
 public:
  explicit FakeWindowPresenter(ae::ObjProp prop) : WindowPresenter{prop} {}
  AE_OBJECT_REFLECT()
};

class RestartDoc;
class RestartBumpEvent;

class RestartDoc : public NodeFor<RestartDoc> {
  APPTRAVERSE_OBJECT(RestartDoc, Node, 0)
 protected:
  RestartDoc() = default;
 public:
  explicit RestartDoc(ae::ObjProp prop) : NodeFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(value))
  std::int32_t value{0};
  void Apply(RestartBumpEvent const& event);
};

class RestartBumpEvent : public EventFor<RestartDoc, RestartBumpEvent> {
  APPTRAVERSE_OBJECT(RestartBumpEvent, Event, 0)
 protected:
  RestartBumpEvent() = default;
 public:
  explicit RestartBumpEvent(ae::ObjProp prop) : EventFor{prop} {}
  AE_OBJECT_REFLECT(AE_MMBR(delta))
  std::int32_t delta{0};
};

void RestartDoc::Apply(RestartBumpEvent const& event) { value += event.delta; }

APPTRAVERSE_REGISTER(FakeChatPresenter);
APPTRAVERSE_REGISTER(FakeWindow);
APPTRAVERSE_REGISTER(FakeWindowPresenter);
APPTRAVERSE_REGISTER(RestartDoc);
APPTRAVERSE_REGISTER(RestartBumpEvent);

void SleepForDistinctTimestamp() {
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

bool IsAckEnvelope(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  return decoded.packet->GetClassId() == AckPacket::kClassId;
}

bool IsEventEnvelope(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  return decoded.packet->GetClassId() == EventPacket::kClassId;
}

bool IsNodeStateEnvelope(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  return decoded.packet->GetClassId() == NodeStatePacket::kClassId;
}

std::size_t FindEnvelope(
    UnreliableMemoryLink const& link,
    std::function<bool(SerializedSyncPacket const&)> pred) {
  for (std::size_t i = 0; i < link.pending_count(); ++i) {
    if (pred(link.envelopes()[i].bytes)) {
      return i;
    }
  }
  return link.pending_count();
}

void Converge(UnreliableMemoryLink& link, SharedGraphSyncSession& a,
              SharedGraphSyncSession& b, int max_rounds = 64) {
  for (int round = 0; round < max_rounds; ++round) {
    link.DeliverAllInOrder();
    if (link.pending_count() == 0 && a.pending_packet_count() == 0 &&
        b.pending_packet_count() == 0) {
      return;
    }
    a.RetryPending();
    b.RetryPending();
  }
  CHECK(false && "sessions failed to converge");
}

struct DocSide {
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  RestartDoc::ptr doc;
  RestartDoc::ptr doc_base;
  SyncSessionState::ptr sync_state;
  ae::ObjId doc_id;
  ae::ObjId sync_state_id;

  DocSide() {
    domain = std::make_unique<ae::Domain>(ae::Now(), storage);
    doc_base = RestartDoc::ptr::Create(ae::CreateWith{*domain}.with_id(1000));
    doc = RestartDoc::ptr::Create(ae::CreateWith{*domain}.with_id(1001));
    doc->base = doc_base;
    doc->CaptureBaseState();
    doc.Save();
    doc_id = doc.id();
    sync_state = CreateSyncSessionState(*domain, doc_id);
    sync_state.Save();
    sync_state_id = sync_state.id();
  }

  SyncReplica AsReplica() { return SyncReplica{*domain, storage, doc_id}; }

  void CommitBump(std::int32_t delta, ae::ObjId::Type event_id) {
    auto event = RestartBumpEvent::ptr::Create(
        ae::CreateWith{*domain}.with_id(event_id));
    event->delta = delta;
    doc->Commit(event);
    doc.Save();
  }

  void DestroyRuntime() {
    sync_state = {};
    doc = {};
    doc_base = {};
    domain.reset();
  }

  void ReloadRuntime() {
    domain = std::make_unique<ae::Domain>(ae::Now(), storage);
    doc = RestartDoc::ptr::Declare(ae::CreateWith{*domain}.with_id(doc_id));
    doc.Load();
    CHECK(doc.is_loaded());
    sync_state = SyncSessionState::ptr::Declare(
        ae::CreateWith{*domain}.with_id(sync_state_id));
    sync_state.Load();
    CHECK(sync_state.is_loaded());
  }
};

void TestLostPacketSenderRestart() {
  DocSide left;
  DocSide right;
  left.CommitBump(5, 100);

  UnreliableMemoryLink link;
  {
    SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                        link.MakeSend(0)};
    SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                         link.MakeSend(1)};
    link.Bind(left_session, right_session);
    left_session.StartOrResume();
    CHECK(left_session.pending_packet_count() == 1);
    link.Drop(0);
  }
  left.DestroyRuntime();

  left.ReloadRuntime();
  SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);

  CHECK(left.sync_state->data.pending_packets.size() == 1);
  auto const packet_id = left.sync_state->data.pending_packets[0].packet_id;
  auto const bytes = left.sync_state->data.pending_packets[0].serialized_bytes;

  left_session.StartOrResume();
  CHECK(left.sync_state->data.pending_packets[0].packet_id == packet_id);
  CHECK(left.sync_state->data.pending_packets[0].serialized_bytes == bytes);
  Converge(link, left_session, right_session);
  right.doc.Load();
  CHECK(right.doc->value == 5);
  CHECK(right.doc->journal.size() == 1);
  CHECK(left_session.pending_packet_count() == 0);
}

void TestLostAckReceiverRestart() {
  DocSide left;
  DocSide right;
  left.CommitBump(3, 200);

  UnreliableMemoryLink link;
  {
    SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                        link.MakeSend(0)};
    SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                         link.MakeSend(1)};
    link.Bind(left_session, right_session);
    left_session.StartOrResume();
    link.DeliverNext();
    auto ack_index = FindEnvelope(link, IsAckEnvelope);
    CHECK(ack_index < link.pending_count());
    link.Drop(ack_index);
    CHECK(right.sync_state->data.successfully_received_packet_ids.size() == 1);
    right.doc.Load();
    CHECK(right.doc->value == 3);
  }
  right.DestroyRuntime();

  right.ReloadRuntime();
  SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);

  CHECK(right.sync_state->data.successfully_received_packet_ids.size() == 1);
  left_session.RetryPending();
  auto event_or_state = FindEnvelope(link, [](SerializedSyncPacket const& b) {
    return !IsAckEnvelope(b);
  });
  CHECK(event_or_state < link.pending_count());
  right.doc.Load();
  auto const journal_before = right.doc->journal.size();
  link.Deliver(event_or_state);
  right.doc.Load();
  CHECK(right.doc->journal.size() == journal_before);
  CHECK(right.doc->value == 3);
  CHECK(FindEnvelope(link, IsAckEnvelope) < link.pending_count());
  Converge(link, left_session, right_session);
  CHECK(left_session.pending_packet_count() == 0);
}

void TestBothSidesRestartAfterInitialSync() {
  DocSide left;
  DocSide right;
  left.CommitBump(1, 110);
  right.CommitBump(2, 120);

  UnreliableMemoryLink link;
  {
    SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                        link.MakeSend(0)};
    SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                         link.MakeSend(1)};
    link.Bind(left_session, right_session);
    left_session.StartOrResume();
    right_session.StartOrResume();
    Converge(link, left_session, right_session);
    CHECK(left_session.initial_sync_complete());
    CHECK(right_session.initial_sync_complete());
  }

  left.DestroyRuntime();
  right.DestroyRuntime();
  left.ReloadRuntime();
  right.ReloadRuntime();

  SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);
  CHECK(left.sync_state->data.initial_sync_complete);
  CHECK(right.sync_state->data.initial_sync_complete);

  left_session.StartOrResume();
  right_session.StartOrResume();
  CHECK(link.pending_count() == 0);
  CHECK(left_session.pending_packet_count() == 0);
  CHECK(right_session.pending_packet_count() == 0);
  left_session.Poll();
  right_session.Poll();
  CHECK(link.pending_count() == 0);
}

void TestRestartDuringInitialSync() {
  DocSide left;
  DocSide right;
  left.CommitBump(1, 30);

  UnreliableMemoryLink link;
  {
    auto data = left.sync_state->data;
    data.initial_sync_started = true;
    auto event = SetSyncSessionDataEvent::ptr::Create(
        ae::CreateWith{*left.domain});
    event->data = data;
    left.sync_state->Commit(std::move(event));
    left.sync_state.Save();
    CHECK(left.sync_state->data.initial_sync_started);
    CHECK(left.sync_state->data.pending_packets.empty());
  }
  left.DestroyRuntime();

  left.ReloadRuntime();
  {
    SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                        link.MakeSend(0)};
    SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                         link.MakeSend(1)};
    link.Bind(left_session, right_session);
    left_session.StartOrResume();
    CHECK(left_session.pending_packet_count() == 1);
    link.Drop(0);
  }
  left.DestroyRuntime();

  left.ReloadRuntime();
  SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                      link.MakeSend(0)};
  SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                       link.MakeSend(1)};
  link.Bind(left_session, right_session);
  auto const packet_id = left.sync_state->data.pending_packets[0].packet_id;
  auto const bytes = left.sync_state->data.pending_packets[0].serialized_bytes;
  left_session.StartOrResume();
  CHECK(left.sync_state->data.pending_packets[0].packet_id == packet_id);
  CHECK(left.sync_state->data.pending_packets[0].serialized_bytes == bytes);

  Converge(link, left_session, right_session);
  CHECK(left_session.initial_sync_complete());
  CHECK(left_session.pending_packet_count() == 0);
  right.doc.Load();
  CHECK(right.doc->value == 1);
}

void TestBlockedAddMessageSenderRestart() {
  struct ChatPair {
    ae::RamDomainStorage storage;
    std::unique_ptr<ae::Domain> domain;
    Client::ptr client;
    Chat::ptr chat;
    SyncSessionState::ptr sync_state;
    ae::ObjId sync_state_id;
    ae::ObjId chat_id;
    ae::ObjId client_id;

    explicit ChatPair(bool seed_client) {
      domain = std::make_unique<ae::Domain>(ae::Now(), storage);
      if (seed_client) {
        auto client_base =
            Client::ptr::Create(ae::CreateWith{*domain}.with_id(201));
        client = Client::ptr::Create(ae::CreateWith{*domain}.with_id(202));
        client->name = "Alice";
        client->base = client_base;
        client->CaptureBaseState();
        client.Save();
        client_id = client.id();
      }
      auto chat_base = Chat::ptr::Create(ae::CreateWith{*domain}.with_id(101));
      chat = Chat::ptr::Create(ae::CreateWith{*domain}.with_id(102));
      chat->base = chat_base;
      chat->CaptureBaseState();
      chat.Save();
      chat_id = chat.id();
      sync_state = CreateSyncSessionState(*domain, chat_id);
      sync_state.Save();
      sync_state_id = sync_state.id();
    }

    SyncReplica AsReplica() { return SyncReplica{*domain, storage, chat_id}; }

    void DestroyRuntime() {
      sync_state = {};
      chat = {};
      client = {};
      domain.reset();
    }

    void ReloadRuntime() {
      domain = std::make_unique<ae::Domain>(ae::Now(), storage);
      chat = Chat::ptr::Declare(ae::CreateWith{*domain}.with_id(chat_id));
      chat.Load();
      client = Client::ptr::Declare(ae::CreateWith{*domain}.with_id(client_id));
      client.Load();
      sync_state = SyncSessionState::ptr::Declare(
          ae::CreateWith{*domain}.with_id(sync_state_id));
      sync_state.Load();
      CHECK(sync_state.is_loaded());
    }
  };

  ChatPair left{true};
  ChatPair right{false};
  auto right_rep = right.AsReplica();
  ImportObjectGraph(left.client, left.storage, right_rep,
                    SharedCopyMode::kCopyLoadedTargets);

  UnreliableMemoryLink link;
  ae::ObjId blocked_packet_id;
  SerializedSyncPacket blocked_bytes;
  {
    SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                        link.MakeSend(0)};
    SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                         link.MakeSend(1)};
    link.Bind(left_session, right_session);
    left_session.StartOrResume();
    link.DeliverAllInOrder();
    CHECK(left_session.initial_sync_complete());

    auto join =
        JoinClientEvent::ptr::Create(ae::CreateWith{*left.domain}.with_id(301));
    join->client = left.client;
    left.chat->Commit(join);
    auto message =
        AddMessageEvent::ptr::Create(ae::CreateWith{*left.domain}.with_id(501));
    message->author = left.client;
    message->text = "blocked-then-retry";
    left.chat->Commit(message);
    left.chat.Save();

    left_session.Poll();
    CHECK(left_session.pending_packet_count() == 2);

    auto message_index = FindEnvelope(link, [](SerializedSyncPacket const& b) {
      if (!IsEventEnvelope(b)) {
        return false;
      }
      auto decoded = SyncPacketCodec{}.Decode(b);
      auto event_packet = EventPacket::ptr{decoded.packet};
      return event_packet->event.id().id() == 501;
    });
    CHECK(message_index < link.pending_count());
    link.Deliver(message_index);
    CHECK(FindEnvelope(link, IsAckEnvelope) == link.pending_count());

    for (auto const& pending : left.sync_state->data.pending_packets) {
      if (pending.kind == PendingSyncPacketKind::kEvent &&
          pending.event_id.id() == 501) {
        blocked_packet_id = pending.packet_id;
        blocked_bytes = pending.serialized_bytes;
      }
    }
    CHECK(blocked_packet_id.IsValid());
  }

  left.DestroyRuntime();
  left.ReloadRuntime();
  {
    SharedGraphSyncSession left_session{left.AsReplica(), left.sync_state,
                                        link.MakeSend(0)};
    SharedGraphSyncSession right_session{right.AsReplica(), right.sync_state,
                                         link.MakeSend(1)};
    link.Bind(left_session, right_session);

    bool found = false;
    for (auto const& pending : left.sync_state->data.pending_packets) {
      if (pending.packet_id == blocked_packet_id) {
        CHECK(pending.serialized_bytes == blocked_bytes);
        found = true;
      }
    }
    CHECK(found);

    left_session.StartOrResume();
    Converge(link, left_session, right_session);
    right.chat.Load();
    CHECK(right.chat->HasEvent(ae::ObjId{301}));
    CHECK(right.chat->HasEvent(ae::ObjId{501}));
    CHECK(left_session.pending_packet_count() == 0);
  }
}

void TestDirectoryStorageSessionReload() {
  auto root = std::filesystem::temp_directory_path() /
              "apptraverse_restart_safe_dir_session";
  std::filesystem::remove_all(root);
  ae::ObjId state_id;
  ae::ObjId packet_id;
  std::vector<std::uint8_t> bytes;
  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};
    auto state = CreateSyncSessionState(domain, ae::ObjId{55});
    state.Save();
    state_id = state.id();

    SyncSessionData data = state->data;
    data.initial_sync_started = true;
    PendingSyncPacketState pending;
    pending.packet_id = ae::ObjId{88};
    pending.serialized_bytes = {9, 8, 7, 6};
    pending.kind = PendingSyncPacketKind::kNodeState;
    pending.node_id = ae::ObjId{55};
    pending.is_initial_state = true;
    data.pending_packets.push_back(pending);
    auto event =
        SetSyncSessionDataEvent::ptr::Create(ae::CreateWith{domain});
    event->data = data;
    state->Commit(std::move(event));
    state.Save();
    packet_id = state->data.pending_packets[0].packet_id;
    bytes = state->data.pending_packets[0].serialized_bytes;
  }

  {
    DirectoryDomainStorage storage{root};
    ae::Domain domain{ae::Now(), storage};
    auto state = SyncSessionState::ptr::Declare(
        ae::CreateWith{domain}.with_id(state_id));
    state.Load();
    CHECK(state.is_loaded());
    CHECK(state->data.initial_sync_started);
    CHECK(state->data.shared_root_id.id() == 55);
    CHECK(state->data.pending_packets.size() == 1);
    CHECK(state->data.pending_packets[0].packet_id == packet_id);
    CHECK(state->data.pending_packets[0].serialized_bytes == bytes);
    CHECK(state->data.pending_packets[0].is_initial_state);
  }
  std::filesystem::remove_all(root);
}

struct ChatSide {
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  examples::SingleClientChatGraph graph;
  SyncSessionState::ptr sync_state;
  ae::ObjId chat_id;
  ae::ObjId sync_state_id;
  ae::ObjId local_client_id;
  std::string name;

  explicit ChatSide(std::string platform) : name{std::move(platform)} {
    domain = std::make_unique<ae::Domain>(ae::Now(), storage);
    graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                 FakeChatPresenter>(
        *domain, name);
    graph.app.Save();
    chat_id = graph.chat.id();
    local_client_id = graph.local_client.id();
    sync_state = CreateSyncSessionState(*domain, chat_id);
    sync_state.Save();
    sync_state_id = sync_state.id();
  }

  SyncReplica AsReplica() { return SyncReplica{*domain, storage, chat_id}; }

  void Submit(std::string text) {
    SleepForDistinctTimestamp();
    graph.chat_presenter->SubmitText(std::move(text));
    graph.chat.Save();
    graph.app.Save();
  }

  std::string Transcript() const {
    return examples::FormatChatTranscriptUtf8(graph.chat);
  }

  void DestroyRuntime() {
    sync_state = {};
    graph = examples::SingleClientChatGraph{};
    domain.reset();
  }

  void ReloadRuntime() {
    domain = std::make_unique<ae::Domain>(ae::Now(), storage);
    graph.app = App::ptr::Declare(ae::CreateWith{*domain}.with_id(
        ToObjId(ApplicationObjId::Application)));
    graph.app.Load();
    graph.chat = Chat::ptr::Declare(ae::CreateWith{*domain}.with_id(chat_id));
    graph.chat.Load();
    graph.local_client = Client::ptr::Declare(
        ae::CreateWith{*domain}.with_id(local_client_id));
    graph.local_client.Load();
    graph.chat_presenter = ChatPresenter::ptr::Declare(
        ae::CreateWith{*domain}.with_id(
            ToObjId(ApplicationObjId::ChatPresenter)));
    graph.chat_presenter.Load();
    sync_state = SyncSessionState::ptr::Declare(
        ae::CreateWith{*domain}.with_id(sync_state_id));
    sync_state.Load();
    CHECK(sync_state.is_loaded());
    CHECK(graph.chat.is_loaded());
  }
};

std::set<ae::ObjId::Type> CollectEventIds(Chat::ptr chat) {
  std::set<ae::ObjId::Type> ids;
  for (auto const& record : chat->journal) {
    ids.insert(record.event.id().id());
  }
  return ids;
}

std::set<ae::ObjId::Type> CollectSharedNodeIds(Chat::ptr chat) {
  std::set<ae::ObjId::Type> ids;
  for (auto const& node : DiscoverSharedGraph(chat)) {
    ids.insert(node.id().id());
  }
  return ids;
}

std::vector<ae::ObjId::Type> JournalOrder(Chat::ptr chat) {
  std::vector<ae::ObjId::Type> ids;
  for (auto const& record : chat->journal) {
    ids.push_back(record.event.id().id());
  }
  return ids;
}

void TestChatRestartConvergence() {
  ChatSide windows{"Windows"};
  ChatSide android{"Android"};
  UnreliableMemoryLink link;

  {
    SharedGraphSyncSession win_session{windows.AsReplica(), windows.sync_state,
                                       link.MakeSend(0)};
    SharedGraphSyncSession and_session{android.AsReplica(), android.sync_state,
                                       link.MakeSend(1)};
    link.Bind(win_session, and_session);
    windows.Submit("W-before");
    android.Submit("A-before");
    win_session.StartOrResume();
    and_session.StartOrResume();
    Converge(link, win_session, and_session);

    windows.Submit("W-offline-1");
    android.Submit("A-offline-1");
    win_session.Poll();
    and_session.Poll();
    auto drop_index = FindEnvelope(link, IsEventEnvelope);
    if (drop_index < link.pending_count()) {
      link.Drop(drop_index);
    }
    auto ack_index = FindEnvelope(link, IsAckEnvelope);
    if (ack_index < link.pending_count()) {
      link.Drop(ack_index);
    }
  }
  windows.DestroyRuntime();
  android.DestroyRuntime();
  windows.ReloadRuntime();
  android.ReloadRuntime();

  SharedGraphSyncSession win_session{windows.AsReplica(), windows.sync_state,
                                     link.MakeSend(0)};
  SharedGraphSyncSession and_session{android.AsReplica(), android.sync_state,
                                     link.MakeSend(1)};
  link.Bind(win_session, and_session);
  win_session.StartOrResume();
  and_session.StartOrResume();
  Converge(link, win_session, and_session);

  windows.graph.chat.Load();
  android.graph.chat.Load();
  CHECK(CollectSharedNodeIds(windows.graph.chat) ==
        CollectSharedNodeIds(android.graph.chat));
  CHECK(CollectEventIds(windows.graph.chat) ==
        CollectEventIds(android.graph.chat));
  CHECK(JournalOrder(windows.graph.chat) == JournalOrder(android.graph.chat));
  CHECK(windows.Transcript() == android.Transcript());
  CHECK(win_session.pending_packet_count() == 0);
  CHECK(and_session.pending_packet_count() == 0);
  CHECK(windows.graph.app->local_client.id() == windows.local_client_id);
  CHECK(android.graph.app->local_client.id() == android.local_client_id);

  auto win_discovered = DiscoverSharedGraph(windows.graph.chat);
  for (auto const& node : win_discovered) {
    CHECK(node.id() != windows.sync_state_id);
  }
}

void TestEventBeforeInitialNodeState() {
  DocSide src;
  src.CommitBump(9, 909);

  ae::RamDomainStorage dst_storage;
  ae::Domain dst_domain{ae::Now(), dst_storage};
  auto dst_state = CreateSyncSessionState(dst_domain, src.doc_id);
  dst_state.Save();
  SyncReplica dst_rep{dst_domain, dst_storage, src.doc_id};

  UnreliableMemoryLink link;
  SharedGraphSyncSession src_session{src.AsReplica(), src.sync_state,
                                     link.MakeSend(0)};
  SharedGraphSyncSession dst_session{dst_rep, dst_state, link.MakeSend(1)};
  link.Bind(src_session, dst_session);

  src_session.StartOrResume();
  CHECK(src_session.pending_packet_count() == 1);
  CHECK(IsNodeStateEnvelope(link.envelopes()[0].bytes));
  auto const lost_initial = link.envelopes()[0].bytes;
  auto const lost_id = src.sync_state->data.pending_packets[0].packet_id;
  link.Drop(0);

  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  auto event = RestartBumpEvent::ptr::Declare(
      ae::CreateWith{*src.domain}.with_id(909));
  event.Load();
  CopyObjectGraph(event, src.storage, build_domain, build_storage,
                  SharedCopyMode::kCopyLoadedTargets);
  auto build_event =
      Event::ptr::Declare(ae::CreateWith{build_domain}.with_id(909));
  build_event.Load();
  auto packet = EventPacket::ptr::Create(ae::CreateWith{build_domain});
  packet->target_node_id = src.doc_id;
  packet->timestamp_us = src.doc->journal.front().timestamp_us;
  packet->event = build_event;
  auto event_bytes = SyncPacketCodec{}.Encode(packet);

  dst_session.Receive(event_bytes);
  CHECK(FindEnvelope(link, IsAckEnvelope) == link.pending_count());
  CHECK(!StorageHasObject(dst_storage, src.doc_id));

  src_session.RetryPending();
  CHECK(link.pending_count() == 1);
  CHECK(link.envelopes()[0].bytes == lost_initial);
  CHECK(src.sync_state->data.pending_packets[0].packet_id == lost_id);
  link.DeliverAllInOrder();
  CHECK(StorageHasObject(dst_storage, src.doc_id));

  dst_session.Receive(event_bytes);
  Converge(link, src_session, dst_session);
  auto loaded =
      RestartDoc::ptr::Declare(ae::CreateWith{dst_domain}.with_id(src.doc_id));
  loaded.Load();
  CHECK(loaded.is_loaded());
  CHECK(loaded->value == 9);
  CHECK(src_session.pending_packet_count() == 0);
}

void TestCarolJoinSingleEventPacket() {
  ChatSide windows{"Windows"};
  ChatSide android{"Android"};
  UnreliableMemoryLink link;
  SharedGraphSyncSession win_session{windows.AsReplica(), windows.sync_state,
                                     link.MakeSend(0)};
  SharedGraphSyncSession and_session{android.AsReplica(), android.sync_state,
                                     link.MakeSend(1)};
  link.Bind(win_session, and_session);
  windows.Submit("W-before");
  android.Submit("A-before");
  win_session.StartOrResume();
  and_session.StartOrResume();
  Converge(link, win_session, and_session);

  SleepForDistinctTimestamp();
  auto carol_base = Client::ptr::Create(ae::CreateWith{*windows.domain});
  auto carol = Client::ptr::Create(ae::CreateWith{*windows.domain});
  carol->name = "Carol";
  carol->base = carol_base;
  carol->CaptureBaseState();
  carol.Save();
  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{*windows.domain});
  join->client = carol;
  windows.graph.chat->Commit(join);
  windows.graph.chat.Save();
  auto const carol_id = carol.id();
  auto const join_id = join.id();

  win_session.Poll();
  CHECK(win_session.pending_packet_count() == 1);
  CHECK(IsEventEnvelope(link.envelopes()[0].bytes));
  CHECK(FindEnvelope(link, IsNodeStateEnvelope) == link.pending_count());

  auto decoded = SyncPacketCodec{}.Decode(link.envelopes()[0].bytes);
  auto event_packet = EventPacket::ptr{decoded.packet};
  CHECK(event_packet->event.id() == join_id);
  auto join_event = JoinClientEvent::ptr{event_packet->event};
  CHECK(join_event->client.id() == carol_id);
  CHECK(join_event->client.is_loaded());
  CHECK(join_event->client->name == "Carol");

  Converge(link, win_session, and_session);
  auto remote_carol = Client::ptr::Declare(
      ae::CreateWith{*android.domain}.with_id(carol_id));
  remote_carol.Load();
  CHECK(remote_carol.is_loaded());
  CHECK(remote_carol->name == "Carol");
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestLostPacketSenderRestart();
  apptraverse::test::TestLostAckReceiverRestart();
  apptraverse::test::TestBothSidesRestartAfterInitialSync();
  apptraverse::test::TestRestartDuringInitialSync();
  apptraverse::test::TestBlockedAddMessageSenderRestart();
  apptraverse::test::TestDirectoryStorageSessionReload();
  apptraverse::test::TestChatRestartConvergence();
  apptraverse::test::TestEventBeforeInitialNodeState();
  apptraverse::test::TestCarolJoinSingleEventPacket();
  std::cout << "restart_safe_sync_test OK\n";
  return 0;
}
