#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/obj/obj.h"

#include "apptraverse/object_graph_copy.h"
#include "apptraverse/node_for.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_graph.h"
#include "apptraverse/shared_graph_sync_session.h"
#include "apptraverse/sync_packet.h"
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

APPTRAVERSE_REGISTER(FakeChatPresenter);
APPTRAVERSE_REGISTER(FakeWindow);
APPTRAVERSE_REGISTER(FakeWindowPresenter);

void SleepForDistinctTimestamp() {
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

struct ChatReplica {
  ae::RamDomainStorage storage;
  ae::Domain domain;
  examples::SingleClientChatGraph graph;

  explicit ChatReplica(std::string name) : domain{ae::Now(), storage} {
    graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                 FakeChatPresenter>(
        domain, name);
    graph.app.Save();
  }

  SyncReplica AsSyncReplica() {
    return SyncReplica{domain, storage, graph.chat.id()};
  }

  std::string Transcript() const {
    return examples::FormatChatTranscriptUtf8(graph.chat);
  }

  void ReloadChat() { graph.chat.Load(); }

  void Submit(std::string text) {
    SleepForDistinctTimestamp();
    graph.chat_presenter->SubmitText(std::move(text));
    graph.chat.Save();
    graph.app.Save();
  }

  void CheckLocalIsolation() {
    graph.app.Load();
    graph.chat.Load();
    graph.chat_presenter.Load();
    graph.local_client.Load();
    graph.window.Load();
    CHECK(graph.app->local_client.id() == graph.local_client.id());
    CHECK(graph.chat_presenter->local_client.id() == graph.local_client.id());
    CHECK(graph.chat->presenter.id() == graph.chat_presenter.id());
    CHECK(graph.app->window.id() == graph.window.id());
  }
};

struct SessionPair {
  ChatReplica windows;
  ChatReplica android;
  UnreliableMemoryLink link;
  SharedGraphSyncSession win_session;
  SharedGraphSyncSession and_session;

  SessionPair()
      : windows{"Windows"},
        android{"Android"},
        win_session{windows.AsSyncReplica(), link.MakeSend(0)},
        and_session{android.AsSyncReplica(), link.MakeSend(1)} {
    link.Bind(win_session, and_session);
  }
};

bool IsAckEnvelope(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  return decoded.packet->GetClassId() == AckPacket::kClassId;
}

bool IsNodeStateEnvelope(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  return decoded.packet->GetClassId() == NodeStatePacket::kClassId;
}

bool IsEventEnvelope(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  return decoded.packet->GetClassId() == EventPacket::kClassId;
}

ae::ObjId EventRootId(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  auto event_packet = EventPacket::ptr{decoded.packet};
  return event_packet->event.id();
}

ae::ObjId NodeStateRootId(SerializedSyncPacket const& bytes) {
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  auto node_packet = NodeStatePacket::ptr{decoded.packet};
  return node_packet->node.id();
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

void ExpectConvergedChats(ChatReplica& a, ChatReplica& b) {
  a.ReloadChat();
  b.ReloadChat();
  CHECK(a.graph.chat->journal.size() == b.graph.chat->journal.size());
  CHECK(CollectEventIds(a.graph.chat) == CollectEventIds(b.graph.chat));
  CHECK(JournalOrder(a.graph.chat) == JournalOrder(b.graph.chat));
  CHECK(a.Transcript() == b.Transcript());
  CHECK(CollectSharedNodeIds(a.graph.chat) ==
        CollectSharedNodeIds(b.graph.chat));
}

void TestPerfectFirstSync() {
  SessionPair pair;
  CHECK(pair.windows.graph.chat.id().id() == ToObjId(ApplicationObjId::Chat));
  CHECK(pair.android.graph.chat.id().id() == ToObjId(ApplicationObjId::Chat));

  pair.windows.Submit("W-before");
  pair.android.Submit("A-before");
  pair.win_session.StartInitialSynchronization();
  pair.and_session.StartInitialSynchronization();
  Converge(pair.link, pair.win_session, pair.and_session);

  CHECK(pair.win_session.initial_sync_complete());
  CHECK(pair.and_session.initial_sync_complete());
  ExpectConvergedChats(pair.windows, pair.android);
  CHECK(pair.windows.Transcript().find("W-before") != std::string::npos);
  CHECK(pair.windows.Transcript().find("A-before") != std::string::npos);
  pair.windows.CheckLocalIsolation();
  pair.android.CheckLocalIsolation();
}

void TestFirstSyncOnlyOnce() {
  SessionPair pair;
  pair.windows.Submit("W-before");
  pair.android.Submit("A-before");
  pair.win_session.StartInitialSynchronization();
  pair.and_session.StartInitialSynchronization();
  Converge(pair.link, pair.win_session, pair.and_session);

  pair.win_session.Poll();
  pair.and_session.Poll();
  CHECK(pair.link.pending_count() == 0);
  CHECK(pair.win_session.pending_packet_count() == 0);

  pair.windows.Submit("W-after");
  pair.win_session.Poll();
  CHECK(pair.win_session.pending_packet_count() == 1);
  CHECK(IsEventEnvelope(pair.link.envelopes().front().bytes));
  Converge(pair.link, pair.win_session, pair.and_session);
  ExpectConvergedChats(pair.windows, pair.android);
}

void TestLostEventPacket() {
  SessionPair pair;
  pair.windows.Submit("W-before");
  pair.android.Submit("A-before");
  pair.win_session.StartInitialSynchronization();
  pair.and_session.StartInitialSynchronization();
  Converge(pair.link, pair.win_session, pair.and_session);

  auto const and_journal = pair.android.graph.chat->journal.size();
  pair.windows.Submit("W-lost");
  pair.win_session.Poll();
  CHECK(pair.link.pending_count() == 1);
  pair.link.Drop(0);
  CHECK(pair.win_session.pending_packet_count() == 1);
  pair.android.ReloadChat();
  CHECK(pair.android.graph.chat->journal.size() == and_journal);

  pair.win_session.RetryPending();
  Converge(pair.link, pair.win_session, pair.and_session);
  CHECK(pair.android.Transcript().find("W-lost") != std::string::npos);
  CHECK(pair.win_session.pending_packet_count() == 0);
}

void TestLostAck() {
  SessionPair pair;
  pair.windows.Submit("W-before");
  pair.android.Submit("A-before");
  pair.win_session.StartInitialSynchronization();
  pair.and_session.StartInitialSynchronization();
  Converge(pair.link, pair.win_session, pair.and_session);

  pair.android.Submit("A-ack-lost");
  pair.and_session.Poll();
  pair.link.DeliverNext();
  auto ack_index = FindEnvelope(pair.link, IsAckEnvelope);
  CHECK(ack_index < pair.link.pending_count());
  pair.link.Drop(ack_index);
  CHECK(pair.and_session.pending_packet_count() == 1);

  pair.windows.ReloadChat();
  auto const journal_size = pair.windows.graph.chat->journal.size();
  CHECK(pair.windows.Transcript().find("A-ack-lost") != std::string::npos);

  pair.and_session.RetryPending();
  auto event_index = FindEnvelope(pair.link, IsEventEnvelope);
  CHECK(event_index < pair.link.pending_count());
  pair.link.Deliver(event_index);
  pair.windows.ReloadChat();
  CHECK(pair.windows.graph.chat->journal.size() == journal_size);
  CHECK(FindEnvelope(pair.link, IsAckEnvelope) < pair.link.pending_count());
  Converge(pair.link, pair.win_session, pair.and_session);
  CHECK(pair.and_session.pending_packet_count() == 0);
}

void TestEventBeforeMissingClient() {
  SessionPair pair;
  pair.windows.Submit("W-before");
  pair.android.Submit("A-before");
  pair.win_session.StartInitialSynchronization();
  pair.and_session.StartInitialSynchronization();
  Converge(pair.link, pair.win_session, pair.and_session);

  SleepForDistinctTimestamp();
  auto carol_base = Client::ptr::Create(ae::CreateWith{pair.windows.domain});
  auto carol = Client::ptr::Create(ae::CreateWith{pair.windows.domain});
  carol->name = "Carol";
  carol->base = carol_base;
  carol->CaptureBaseState();
  carol.Save();
  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{pair.windows.domain});
  join->client = carol;
  pair.windows.graph.chat->Commit(join);
  SleepForDistinctTimestamp();
  auto message =
      AddMessageEvent::ptr::Create(ae::CreateWith{pair.windows.domain});
  message->author = carol;
  message->text = "Carol message";
  pair.windows.graph.chat->Commit(message);
  pair.windows.graph.chat.Save();
  auto const carol_id = carol.id();
  auto const message_id = message.id();

  pair.win_session.Poll();
  // Join + Message only — Carol travels inside Event graphs.
  CHECK(pair.win_session.pending_packet_count() == 2);

  auto msg_index = FindEnvelope(pair.link, [&](SerializedSyncPacket const& b) {
    return IsEventEnvelope(b) && EventRootId(b) == message_id;
  });
  CHECK(msg_index < pair.link.pending_count());
  pair.link.Deliver(msg_index);
  // AddMessage before Join is blocked; Client may already exist from Event graph.
  CHECK(FindEnvelope(pair.link, IsAckEnvelope) == pair.link.pending_count());
  pair.android.ReloadChat();
  auto const journal_before = pair.android.graph.chat->journal.size();

  Converge(pair.link, pair.win_session, pair.and_session);
  pair.android.ReloadChat();
  CHECK(pair.android.graph.chat->journal.size() > journal_before);
  auto carol_loaded = Client::ptr::Declare(
      ae::CreateWith{pair.android.domain}.with_id(carol_id));
  carol_loaded.Load();
  CHECK(carol_loaded.is_loaded());
  CHECK(carol_loaded->name == "Carol");
  CHECK(pair.android.Transcript().find("Carol message") != std::string::npos);
}

void TestAddMessageBeforeJoin() {
  SessionPair pair;
  pair.windows.Submit("W-before");
  pair.android.Submit("A-before");
  pair.win_session.StartInitialSynchronization();
  pair.and_session.StartInitialSynchronization();
  Converge(pair.link, pair.win_session, pair.and_session);

  SleepForDistinctTimestamp();
  auto carol_base = Client::ptr::Create(ae::CreateWith{pair.windows.domain});
  auto carol = Client::ptr::Create(ae::CreateWith{pair.windows.domain});
  carol->name = "Carol";
  carol->base = carol_base;
  carol->CaptureBaseState();
  carol.Save();
  auto join = JoinClientEvent::ptr::Create(ae::CreateWith{pair.windows.domain});
  join->client = carol;
  pair.windows.graph.chat->Commit(join);
  SleepForDistinctTimestamp();
  auto message =
      AddMessageEvent::ptr::Create(ae::CreateWith{pair.windows.domain});
  message->author = carol;
  message->text = "Carol message";
  pair.windows.graph.chat->Commit(message);
  pair.windows.graph.chat.Save();
  auto const join_id = join.id();
  auto const message_id = message.id();
  auto const carol_id = carol.id();

  pair.win_session.Poll();
  CHECK(pair.win_session.pending_packet_count() == 2);

  auto msg_index = FindEnvelope(pair.link, [&](SerializedSyncPacket const& b) {
    return IsEventEnvelope(b) && EventRootId(b) == message_id;
  });
  CHECK(msg_index < pair.link.pending_count());
  pair.link.Deliver(msg_index);
  CHECK(FindEnvelope(pair.link, IsAckEnvelope) == pair.link.pending_count());
  pair.android.ReloadChat();
  CHECK(!pair.android.graph.chat->HasEvent(message_id));

  auto join_index = FindEnvelope(pair.link, [&](SerializedSyncPacket const& b) {
    return IsEventEnvelope(b) && EventRootId(b) == join_id;
  });
  CHECK(join_index < pair.link.pending_count());
  pair.link.Deliver(join_index);
  Converge(pair.link, pair.win_session, pair.and_session);
  if (pair.win_session.pending_packet_count() > 0) {
    pair.win_session.RetryPending();
    Converge(pair.link, pair.win_session, pair.and_session);
  }
  CHECK(pair.win_session.pending_packet_count() == 0);
  pair.android.ReloadChat();
  CHECK(pair.android.graph.chat->HasEvent(join_id));
  CHECK(pair.android.graph.chat->HasEvent(message_id));
  auto carol_loaded = Client::ptr::Declare(
      ae::CreateWith{pair.android.domain}.with_id(carol_id));
  carol_loaded.Load();
  CHECK(carol_loaded.is_loaded());
  auto const& transcript = pair.android.Transcript();
  auto pos = transcript.find("Carol message");
  CHECK(pos != std::string::npos);
  CHECK(transcript.find("Carol message", pos + 1) == std::string::npos);
}

void TestInitialNodeStateCarriesClients() {
  ChatReplica windows{"Windows"};
  ChatReplica android{"Android"};
  UnreliableMemoryLink link;
  SharedGraphSyncSession win_session{windows.AsSyncReplica(),
                                     link.MakeSend(0)};
  SharedGraphSyncSession and_session{android.AsSyncReplica(),
                                     link.MakeSend(1)};
  link.Bind(win_session, and_session);

  windows.Submit("W-before");
  win_session.StartInitialSynchronization();
  CHECK(win_session.pending_packet_count() == 1);
  CHECK(IsNodeStateEnvelope(link.envelopes()[0].bytes));
  link.DeliverAllInOrder();
  CHECK(win_session.initial_sync_complete());
  android.ReloadChat();
  CHECK(android.Transcript().find("W-before") != std::string::npos);
  auto remote_client = Client::ptr::Declare(
      ae::CreateWith{android.domain}.with_id(windows.graph.local_client.id()));
  remote_client.Load();
  CHECK(remote_client.is_loaded());
}

void TestDuplicateNodeState() {
  ChatReplica windows{"Windows"};
  ChatReplica android{"Android"};
  UnreliableMemoryLink link;
  SharedGraphSyncSession win_session{windows.AsSyncReplica(),
                                     link.MakeSend(0)};
  SharedGraphSyncSession and_session{android.AsSyncReplica(),
                                     link.MakeSend(1)};
  link.Bind(win_session, and_session);

  win_session.StartInitialSynchronization();
  CHECK(link.pending_count() == 1);
  CHECK(IsNodeStateEnvelope(link.envelopes()[0].bytes));
  CHECK(NodeStateRootId(link.envelopes()[0].bytes) ==
        windows.graph.chat.id());
  link.Duplicate(0);
  Converge(link, win_session, and_session);
  auto remote_client = Client::ptr::Declare(
      ae::CreateWith{android.domain}.with_id(windows.graph.local_client.id()));
  remote_client.Load();
  CHECK(remote_client.is_loaded());
  CHECK(remote_client->name == "Windows");
  android.CheckLocalIsolation();
}

void TestBidirectionalOfflineLike() {
  SessionPair pair;
  pair.windows.Submit("W-before");
  pair.android.Submit("A-before");
  pair.win_session.StartInitialSynchronization();
  pair.and_session.StartInitialSynchronization();
  Converge(pair.link, pair.win_session, pair.and_session);

  pair.windows.Submit("W-offline-1");
  pair.windows.Submit("W-offline-2");
  pair.android.Submit("A-offline-1");
  pair.win_session.Poll();
  pair.and_session.Poll();

  if (pair.link.pending_count() >= 2) {
    pair.link.Move(pair.link.pending_count() - 1, 0);
  }
  auto drop_index = FindEnvelope(pair.link, IsEventEnvelope);
  if (drop_index < pair.link.pending_count()) {
    pair.link.Drop(drop_index);
  }
  auto dup_index = FindEnvelope(pair.link, IsEventEnvelope);
  if (dup_index < pair.link.pending_count()) {
    pair.link.Duplicate(dup_index);
  }
  if (pair.link.pending_count() > 0) {
    pair.link.DeliverNext();
  }
  auto ack_index = FindEnvelope(pair.link, IsAckEnvelope);
  if (ack_index < pair.link.pending_count()) {
    pair.link.Drop(ack_index);
  }

  Converge(pair.link, pair.win_session, pair.and_session);
  ExpectConvergedChats(pair.windows, pair.android);
  CHECK(pair.win_session.pending_packet_count() == 0);
  CHECK(pair.and_session.pending_packet_count() == 0);
  pair.windows.CheckLocalIsolation();
  pair.android.CheckLocalIsolation();
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestPerfectFirstSync();
  apptraverse::test::TestFirstSyncOnlyOnce();
  apptraverse::test::TestLostEventPacket();
  apptraverse::test::TestLostAck();
  apptraverse::test::TestEventBeforeMissingClient();
  apptraverse::test::TestAddMessageBeforeJoin();
  apptraverse::test::TestInitialNodeStateCarriesClients();
  apptraverse::test::TestDuplicateNodeState();
  apptraverse::test::TestBidirectionalOfflineLike();
  std::cout << "single_client_chat_unreliable_sync_test OK\n";
  return 0;
}
