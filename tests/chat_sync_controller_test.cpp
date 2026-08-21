#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/types/uid.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/shared_graph.h"
#include "apptraverse/sync_session_state.h"

#include "chat_presence.h"
#include "chat_sync_controller.h"
#include "graph_builder.h"
#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_events.h"
#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
#include "model/client.h"
#include "model/registration.h"
#include "model/window.h"
#include "model/window_changed_event.h"
#include "model/window_presenter.h"

#include "chat_transcript.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)


void SubmitViaChat(Chat::ptr chat, Client::ptr client, std::string text) {
  assert(chat.is_valid());
  chat.Load();
  assert(chat.is_loaded());
  assert(chat.domain() != nullptr);
  assert(client.is_valid());
  client.Load();
  assert(client.is_loaded());
  auto event = AddMessageEvent::ptr::Create(ae::CreateWith{*chat.domain()});
  event->author = client;
  event->text = std::move(text);
  chat->Commit(event);
  chat.Save();
}


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

ae::Uid MakeUid(std::uint8_t fill) {
  std::array<std::uint8_t, ae::Uid::kSize> bytes{};
  bytes.fill(fill);
  return ae::Uid{bytes};
}

void SleepMs(int ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

struct Side {
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  examples::SingleClientChatGraph graph;
  ae::Uid self_uid;
  ae::ObjId peer_set_id;
  ae::ObjId chat_id;

  explicit Side(std::string name, ae::Uid uid)
      : domain{std::make_unique<ae::Domain>(ae::Now(), storage)},
        self_uid{uid} {
    graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                 FakeChatPresenter>(
        *domain, name);
    graph.app.Save();
    peer_set_id = graph.peer_set.id();
    chat_id = graph.chat.id();
  }

  SyncReplica Replica() {
    return SyncReplica{*domain, storage, graph.chat.id()};
  }

  void DestroyRuntime() {
    graph = examples::SingleClientChatGraph{};
    domain.reset();
  }

  void ReloadRuntime() {
    domain = std::make_unique<ae::Domain>(ae::Now(), storage);
    graph.app = App::ptr::Declare(ae::CreateWith{*domain}.with_id(
        ToObjId(ApplicationObjId::Application)));
    graph.app.Load();
    graph.chat =
        Chat::ptr::Declare(ae::CreateWith{*domain}.with_id(chat_id));
    graph.chat.Load();
    graph.peer_set = ChatPeerSet::ptr::Declare(
        ae::CreateWith{*domain}.with_id(peer_set_id));
    graph.peer_set.Load();
    graph.chat_presenter = ChatPresenter::ptr::Declare(
        ae::CreateWith{*domain}.with_id(
            ToObjId(ApplicationObjId::ChatPresenter)));
    graph.chat_presenter.Load();
    graph.local_client = graph.app->local_client;
    graph.local_client.Load();
    CHECK(graph.chat.is_loaded());
    CHECK(graph.peer_set.is_loaded());
  }

  void Submit(std::string text) {
    SleepMs(2);
    SubmitViaChat(graph.chat, graph.local_client, std::move(text));
    graph.app.Save();
  }

  // Regression helper: commit only — no external App Save.
  void SubmitWithoutExternalSave(std::string text) {
    SleepMs(2);
    SubmitViaChat(graph.chat, graph.local_client, std::move(text));
  }

  std::string Transcript() const {
    return examples::FormatChatTranscriptUtf8(graph.chat);
  }
};

chat::ChatSyncController::SendFunction MakeDirectSend(
    chat::ChatSyncController*& peer_ctrl, ae::Uid const& self_uid,
    ae::Uid const& peer_uid,
    std::function<void(ae::ObjId, SerializedSyncPacket const&)> on_send = {}) {
  return [&peer_ctrl, self_uid, peer_uid, on_send](
             ae::Uid const& peer, ae::ObjId packet_id,
             SerializedSyncPacket const& bytes) {
    CHECK(peer == peer_uid);
    if (on_send) {
      on_send(packet_id, bytes);
    }
    assert(peer_ctrl != nullptr);
    peer_ctrl->Receive(self_uid, bytes);
  };
}

chat::ChatSyncController::RawSendFunction MakeDirectRawSend(
    chat::ChatSyncController*& peer_ctrl, ae::Uid const& self_uid,
    ae::Uid const& peer_uid) {
  return [&peer_ctrl, self_uid, peer_uid](
             ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
    CHECK(peer == peer_uid);
    assert(peer_ctrl != nullptr);
    peer_ctrl->Receive(self_uid, bytes);
  };
}

std::size_t CountLog(std::vector<std::string> const& logs,
                     std::string const& needle) {
  std::size_t count = 0;
  for (auto const& line : logs) {
    if (line.find(needle) != std::string::npos) {
      ++count;
    }
  }
  return count;
}

std::size_t CountJoinEvents(Chat::ptr chat) {
  chat.Load();
  CHECK(chat.is_loaded());
  std::size_t count = 0;
  for (auto const& record : chat->journal) {
    if (record.event.is_valid() &&
        record.event->GetClassId() == JoinClientEvent::kClassId) {
      ++count;
    }
  }
  return count;
}

std::size_t CountJoinEventsNamed(Chat::ptr chat, std::string const& name) {
  chat.Load();
  CHECK(chat.is_loaded());
  std::size_t count = 0;
  for (auto const& record : chat->journal) {
    if (!record.event.is_valid() ||
        record.event->GetClassId() != JoinClientEvent::kClassId) {
      continue;
    }
    auto join = JoinClientEvent::ptr{record.event};
    join.Load();
    CHECK(join.is_loaded());
    join->client.Load();
    CHECK(join->client.is_loaded());
    if (join->client->name == name) {
      ++count;
    }
  }
  return count;
}

std::size_t CountAddMessageEvents(Chat::ptr chat, std::string const& text) {
  chat.Load();
  CHECK(chat.is_loaded());
  std::size_t count = 0;
  for (auto const& record : chat->journal) {
    if (!record.event.is_valid() ||
        record.event->GetClassId() != AddMessageEvent::kClassId) {
      continue;
    }
    auto msg = AddMessageEvent::ptr{record.event};
    msg.Load();
    CHECK(msg.is_loaded());
    if (msg->text == text) {
      ++count;
    }
  }
  return count;
}

std::size_t CountNeedle(std::string const& hay, std::string const& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = hay.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

void TickUntilInitialSync(chat::ChatSyncController& left_ctrl,
                          chat::ChatSyncController& right_ctrl,
                          ae::Uid const& left_uid, ae::Uid const& right_uid) {
  for (int i = 0; i < 400; ++i) {
    left_ctrl.Tick(ae::Now());
    right_ctrl.Tick(ae::Now());
    SleepMs(5);
    auto* ls = left_ctrl.FindSession(right_uid);
    auto* rs = right_ctrl.FindSession(left_uid);
    if (ls && rs && ls->initial_sync_complete() &&
        rs->initial_sync_complete()) {
      return;
    }
  }
  CHECK(false && "initial sync did not complete");
}

void TestMessagesBeforePairingPersistAndMerge() {
  auto windows_uid = MakeUid(0x11);
  auto android_uid = MakeUid(0x22);

  // Phase 1 — Windows history before any peer.
  Side windows{"Windows", windows_uid};
  windows.graph.peer_set.Load();
  CHECK(windows.graph.peer_set->peers.empty());
  auto const windows_client_id = windows.graph.local_client.id();
  windows.SubmitWithoutExternalSave("w_before_pair_1");
  windows.SubmitWithoutExternalSave("w_before_pair_2");
  windows.DestroyRuntime();
  windows.ReloadRuntime();
  CHECK(windows.graph.local_client.id() == windows_client_id);
  CHECK(CountNeedle(windows.Transcript(), "w_before_pair_1") == 1);
  CHECK(CountNeedle(windows.Transcript(), "w_before_pair_2") == 1);
  CHECK(CountAddMessageEvents(windows.graph.chat, "w_before_pair_1") == 1);
  CHECK(CountAddMessageEvents(windows.graph.chat, "w_before_pair_2") == 1);
  CHECK(windows.graph.peer_set->peers.empty());

  // Phase 2 — Android history before any peer.
  Side android{"Android", android_uid};
  android.graph.peer_set.Load();
  CHECK(android.graph.peer_set->peers.empty());
  auto const android_client_id = android.graph.local_client.id();
  android.SubmitWithoutExternalSave("a_before_pair_1");
  android.SubmitWithoutExternalSave("a_before_pair_2");
  android.DestroyRuntime();
  android.ReloadRuntime();
  CHECK(android.graph.local_client.id() == android_client_id);
  CHECK(CountNeedle(android.Transcript(), "a_before_pair_1") == 1);
  CHECK(CountNeedle(android.Transcript(), "a_before_pair_2") == 1);
  CHECK(CountAddMessageEvents(android.graph.chat, "a_before_pair_1") == 1);
  CHECK(CountAddMessageEvents(android.graph.chat, "a_before_pair_2") == 1);
  CHECK(android.graph.peer_set->peers.empty());

  // Phase 3 — pair only after history exists.
  chat::ChatSyncController* windows_ptr = nullptr;
  chat::ChatSyncController* android_ptr = nullptr;
  chat::ChatSyncController windows_ctrl(
      windows.Replica(), windows.graph.chat, windows.graph.peer_set,
      MakeDirectSend(android_ptr, windows_uid, android_uid),
      MakeDirectRawSend(android_ptr, windows_uid, android_uid),
      chat::ChatSyncTiming{});
  chat::ChatSyncController android_ctrl(
      android.Replica(), android.graph.chat, android.graph.peer_set,
      MakeDirectSend(windows_ptr, android_uid, windows_uid),
      MakeDirectRawSend(windows_ptr, android_uid, windows_uid),
      chat::ChatSyncTiming{});
  windows_ptr = &windows_ctrl;
  android_ptr = &android_ctrl;

  windows_ctrl.Start();
  android_ctrl.Start();
  CHECK(windows_ctrl.runtime_session_count() == 0);
  CHECK(android_ctrl.runtime_session_count() == 0);

  windows_ctrl.AddPeer(android_uid);
  TickUntilInitialSync(windows_ctrl, android_ctrl, windows_uid, android_uid);

  auto check_merged = [&]() {
    windows.graph.chat.Load();
    android.graph.chat.Load();
    windows.graph.peer_set.Load();
    android.graph.peer_set.Load();
    CHECK(CountNeedle(windows.Transcript(), "w_before_pair_1") == 1);
    CHECK(CountNeedle(windows.Transcript(), "w_before_pair_2") == 1);
    CHECK(CountNeedle(windows.Transcript(), "a_before_pair_1") == 1);
    CHECK(CountNeedle(windows.Transcript(), "a_before_pair_2") == 1);
    CHECK(CountNeedle(android.Transcript(), "w_before_pair_1") == 1);
    CHECK(CountNeedle(android.Transcript(), "w_before_pair_2") == 1);
    CHECK(CountNeedle(android.Transcript(), "a_before_pair_1") == 1);
    CHECK(CountNeedle(android.Transcript(), "a_before_pair_2") == 1);
    CHECK(windows.graph.peer_set->peers.size() == 1);
    CHECK(android.graph.peer_set->peers.size() == 1);
    CHECK(windows.graph.peer_set->peers[0].remote_uid == android_uid);
    CHECK(android.graph.peer_set->peers[0].remote_uid == windows_uid);
    CHECK(windows_ctrl.FindSession(android_uid)->pending_packet_count() == 0);
    CHECK(android_ctrl.FindSession(windows_uid)->pending_packet_count() == 0);
    CHECK(CountJoinEvents(windows.graph.chat) == 2);
    CHECK(CountJoinEvents(android.graph.chat) == 2);
    CHECK(CountJoinEventsNamed(windows.graph.chat, "Windows") == 1);
    CHECK(CountJoinEventsNamed(windows.graph.chat, "Android") == 1);
    CHECK(CountJoinEventsNamed(android.graph.chat, "Windows") == 1);
    CHECK(CountJoinEventsNamed(android.graph.chat, "Android") == 1);
  };
  check_merged();

  // Extra ticks: idempotent merge.
  for (int i = 0; i < 50; ++i) {
    windows_ctrl.Tick(ae::Now());
    android_ctrl.Tick(ae::Now());
    SleepMs(5);
  }
  check_merged();

  // Persist + reload after merge.
  windows.graph.chat.Save();
  windows.graph.peer_set.Save();
  android.graph.chat.Save();
  android.graph.peer_set.Save();
  windows.DestroyRuntime();
  android.DestroyRuntime();
  windows.ReloadRuntime();
  android.ReloadRuntime();
  CHECK(CountNeedle(windows.Transcript(), "w_before_pair_1") == 1);
  CHECK(CountNeedle(windows.Transcript(), "w_before_pair_2") == 1);
  CHECK(CountNeedle(windows.Transcript(), "a_before_pair_1") == 1);
  CHECK(CountNeedle(windows.Transcript(), "a_before_pair_2") == 1);
  CHECK(CountNeedle(android.Transcript(), "w_before_pair_1") == 1);
  CHECK(CountNeedle(android.Transcript(), "w_before_pair_2") == 1);
  CHECK(CountNeedle(android.Transcript(), "a_before_pair_1") == 1);
  CHECK(CountNeedle(android.Transcript(), "a_before_pair_2") == 1);

  // Post-pair sanity with new controllers on reloaded state.
  chat::ChatSyncController windows2(
      windows.Replica(), windows.graph.chat, windows.graph.peer_set,
      MakeDirectSend(android_ptr, windows_uid, android_uid),
      MakeDirectRawSend(android_ptr, windows_uid, android_uid),
      chat::ChatSyncTiming{});
  chat::ChatSyncController android2(
      android.Replica(), android.graph.chat, android.graph.peer_set,
      MakeDirectSend(windows_ptr, android_uid, windows_uid),
      MakeDirectRawSend(windows_ptr, android_uid, windows_uid),
      chat::ChatSyncTiming{});
  windows_ptr = &windows2;
  android_ptr = &android2;
  windows2.Start();
  android2.Start();
  CHECK(windows2.runtime_session_count() == 1);
  CHECK(android2.runtime_session_count() == 1);

  windows.SubmitWithoutExternalSave("w_after_pair");
  android.SubmitWithoutExternalSave("a_after_pair");
  for (int i = 0; i < 200; ++i) {
    windows2.Tick(ae::Now());
    android2.Tick(ae::Now());
    SleepMs(5);
    windows.graph.chat.Load();
    android.graph.chat.Load();
    if (CountNeedle(windows.Transcript(), "a_after_pair") == 1 &&
        CountNeedle(android.Transcript(), "w_after_pair") == 1 &&
        windows2.FindSession(android_uid)->pending_packet_count() == 0 &&
        android2.FindSession(windows_uid)->pending_packet_count() == 0) {
      break;
    }
  }
  CHECK(CountNeedle(windows.Transcript(), "w_after_pair") == 1);
  CHECK(CountNeedle(windows.Transcript(), "a_after_pair") == 1);
  CHECK(CountNeedle(android.Transcript(), "w_after_pair") == 1);
  CHECK(CountNeedle(android.Transcript(), "a_after_pair") == 1);
  CHECK(windows2.FindSession(android_uid)->pending_packet_count() == 0);
  CHECK(android2.FindSession(windows_uid)->pending_packet_count() == 0);
}

void TestControllerBidirectionalAndRestart() {
  auto left_uid = MakeUid(0xA1);
  auto right_uid = MakeUid(0xB2);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<std::string> logs;
  auto make_log = [&](std::string prefix) {
    return [&, prefix](std::string const& line) {
      logs.push_back(prefix + line);
    };
  };

  chat::ChatSyncController* left_ptr = nullptr;
  chat::ChatSyncController* right_ptr = nullptr;

  chat::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      MakeDirectSend(right_ptr, left_uid, right_uid),
      MakeDirectRawSend(right_ptr, left_uid, right_uid), chat::ChatSyncTiming{}, {},
      make_log("L:"));
  chat::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectSend(left_ptr, right_uid, left_uid),
      MakeDirectRawSend(left_ptr, right_uid, left_uid), chat::ChatSyncTiming{}, {},
      make_log("R:"));
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();
  CHECK(left_ctrl.runtime_session_count() == 0);
  CHECK(right_ctrl.runtime_session_count() == 0);

  left_ctrl.AddPeer(right_uid);
  CHECK(left.graph.peer_set->peers.size() == 1);

  for (int i = 0; i < 200; ++i) {
    left_ctrl.Tick(ae::Now());
    right_ctrl.Tick(ae::Now());
    SleepMs(5);
    auto* ls = left_ctrl.FindSession(right_uid);
    auto* rs = right_ctrl.FindSession(left_uid);
    if (ls && rs && ls->initial_sync_complete() &&
        rs->initial_sync_complete()) {
      break;
    }
  }
  CHECK(left_ctrl.FindSession(right_uid)->initial_sync_complete());
  CHECK(right_ctrl.FindSession(left_uid)->initial_sync_complete());
  CHECK(right.graph.peer_set->peers.size() == 1);

  left.Submit("hello-left");
  right.Submit("hello-right");
  for (int i = 0; i < 200; ++i) {
    left_ctrl.Tick(ae::Now());
    right_ctrl.Tick(ae::Now());
    SleepMs(5);
    left.graph.chat.Load();
    right.graph.chat.Load();
    if (left.Transcript().find("hello-right") != std::string::npos &&
        right.Transcript().find("hello-left") != std::string::npos) {
      break;
    }
  }
  CHECK(left.Transcript().find("hello-right") != std::string::npos);
  CHECK(right.Transcript().find("hello-left") != std::string::npos);

  auto discovered = DiscoverSharedGraph(left.graph.chat);
  for (auto const& node : discovered) {
    CHECK(node.id() != left.graph.peer_set.id());
    for (auto const& peer : left.graph.peer_set->peers) {
      CHECK(node.id() != peer.session_state.id());
    }
  }

  left.graph.chat.Save();
  left.graph.peer_set.Save();
  right.graph.chat.Save();
  right.graph.peer_set.Save();
  left.DestroyRuntime();
  right.DestroyRuntime();
  left.ReloadRuntime();
  right.ReloadRuntime();

  chat::ChatSyncController left2(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      MakeDirectSend(right_ptr, left_uid, right_uid),
      MakeDirectRawSend(right_ptr, left_uid, right_uid), chat::ChatSyncTiming{}, {},
      make_log("L2:"));
  chat::ChatSyncController right2(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectSend(left_ptr, right_uid, left_uid),
      MakeDirectRawSend(left_ptr, right_uid, left_uid), chat::ChatSyncTiming{}, {},
      make_log("R2:"));
  left_ptr = &left2;
  right_ptr = &right2;

  left2.Start();
  right2.Start();
  CHECK(left2.runtime_session_count() == 1);
  CHECK(right2.runtime_session_count() == 1);
  CHECK(left.graph.peer_set->peers[0].remote_uid == right_uid);
  CHECK(right.graph.peer_set->peers[0].remote_uid == left_uid);
  CHECK(left2.FindSession(right_uid)->initial_sync_complete());
  CHECK(right2.FindSession(left_uid)->initial_sync_complete());

  bool resumed = false;
  for (auto const& line : logs) {
    if (line.find("CHAT_SYNC_RESUMED") != std::string::npos &&
        line.find("initial_complete=1") != std::string::npos) {
      resumed = true;
    }
  }
  CHECK(resumed);
}

void TestImmediateEventPublishAndPollFallback() {
  auto left_uid = MakeUid(0xE1);
  auto right_uid = MakeUid(0xE2);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<ae::ObjId> left_event_packets;
  chat::ChatSyncController* left_ptr = nullptr;
  chat::ChatSyncController* right_ptr = nullptr;

  auto left_send = [&](ae::Uid const& peer, ae::ObjId packet_id,
                       SerializedSyncPacket const& bytes) {
    CHECK(peer == right_uid);
    // Classify by decoding would be heavy; count via session pending kinds.
    left_event_packets.push_back(packet_id);
    assert(right_ptr != nullptr);
    right_ptr->Receive(left_uid, bytes);
  };

  chat::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set, left_send,
      MakeDirectRawSend(right_ptr, left_uid, right_uid),
      chat::ChatSyncTiming{});
  chat::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectSend(left_ptr, right_uid, left_uid),
      MakeDirectRawSend(left_ptr, right_uid, left_uid),
      chat::ChatSyncTiming{});
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;
  left_ctrl.Start();
  right_ctrl.Start();
  left_ctrl.AddPeer(right_uid);
  TickUntilInitialSync(left_ctrl, right_ctrl, left_uid, right_uid);

  // 1. LocalEventCommitted publishes without Tick.
  left_event_packets.clear();
  auto before = left_event_packets.size();
  {
    auto event =
        AddMessageEvent::ptr::Create(ae::CreateWith{*left.graph.chat.domain()});
    event->author = left.graph.local_client;
    event->text = "immediate-no-tick";
    left.graph.chat->Commit(event);
    left.graph.chat.Save();
    EventRecord const* rec = nullptr;
    for (auto const& r : left.graph.chat->journal) {
      if (r.event.id() == event.id()) {
        rec = &r;
        break;
      }
    }
    CHECK(rec != nullptr);
    left_ctrl.LocalEventCommitted(left.graph.chat, *rec);
  }
  CHECK(left_event_packets.size() > before);

  auto* session = left_ctrl.FindSession(right_uid);
  CHECK(session != nullptr);
  auto const pending_after_immediate = session->pending_packet_count();

  // 2. First Tick must not create a duplicate EventPacket for same Event.
  left_event_packets.clear();
  left_ctrl.Tick(ae::Now());
  CHECK(session->pending_packet_count() == pending_after_immediate);
  // No new pending packet ids beyond retries of the same pending set.
  for (int i = 0; i < 5; ++i) {
    right_ctrl.Tick(ae::Now());
    left_ctrl.Tick(ae::Now());
    SleepMs(5);
  }
  CHECK(CountNeedle(right.Transcript(), "immediate-no-tick") == 1);

  // 3. During incomplete initial sync, covered Event is not published again.
  {
    Side early_l{"EarlyL", MakeUid(0xE3)};
    Side early_r{"EarlyR", MakeUid(0xE4)};
    auto e_l_uid = MakeUid(0xE3);
    auto e_r_uid = MakeUid(0xE4);
    int event_kind_sends = 0;
    chat::ChatSyncController* e_l_ptr = nullptr;
    chat::ChatSyncController* e_r_ptr = nullptr;
    chat::ChatSyncController e_l(
        early_l.Replica(), early_l.graph.chat, early_l.graph.peer_set,
        [&](ae::Uid const&, ae::ObjId, SerializedSyncPacket const& bytes) {
          auto decoded = SyncPacketCodec{}.Decode(bytes);
          if (decoded.packet->GetClassId() == EventPacket::kClassId) {
            ++event_kind_sends;
          }
          e_r_ptr->Receive(e_l_uid, bytes);
        },
        MakeDirectRawSend(e_r_ptr, e_l_uid, e_r_uid), chat::ChatSyncTiming{});
    chat::ChatSyncController e_r(
        early_r.Replica(), early_r.graph.chat, early_r.graph.peer_set,
        MakeDirectSend(e_l_ptr, e_r_uid, e_l_uid),
        MakeDirectRawSend(e_l_ptr, e_r_uid, e_l_uid), chat::ChatSyncTiming{});
    e_l_ptr = &e_l;
    e_r_ptr = &e_r;
    e_l.Start();
    e_r.Start();
    // Commit local event BEFORE pairing so it is folded into node-state.
    SubmitViaChat(early_l.graph.chat, early_l.graph.local_client,
                  "covered-by-node-state");
    e_l.AddPeer(e_r_uid);
    auto* early_session = e_l.FindSession(e_r_uid);
    CHECK(early_session != nullptr);
    CHECK(!early_session->initial_sync_complete());
    EventRecord const* covered = nullptr;
    early_l.graph.chat.Load();
    for (auto const& r : early_l.graph.chat->journal) {
      if (!r.event.is_valid()) {
        continue;
      }
      auto msg = AddMessageEvent::ptr{r.event};
      msg.Load();
      if (msg.is_loaded() && msg->text == "covered-by-node-state") {
        covered = &r;
        break;
      }
    }
    CHECK(covered != nullptr);
    auto const sends_before = event_kind_sends;
    e_l.LocalEventCommitted(early_l.graph.chat, *covered);
    CHECK(event_kind_sends == sends_before);
  }

  // 4. Poll recovery: Event committed without LocalEventCommitted is found.
  {
    Side rec_l{"RecL", MakeUid(0xE5)};
    Side rec_r{"RecR", MakeUid(0xE6)};
    auto r_l_uid = MakeUid(0xE5);
    auto r_r_uid = MakeUid(0xE6);
    chat::ChatSyncController* r_l_ptr = nullptr;
    chat::ChatSyncController* r_r_ptr = nullptr;
    chat::ChatSyncController r_l(
        rec_l.Replica(), rec_l.graph.chat, rec_l.graph.peer_set,
        MakeDirectSend(r_r_ptr, r_l_uid, r_r_uid),
        MakeDirectRawSend(r_r_ptr, r_l_uid, r_r_uid), chat::ChatSyncTiming{});
    chat::ChatSyncController r_r(
        rec_r.Replica(), rec_r.graph.chat, rec_r.graph.peer_set,
        MakeDirectSend(r_l_ptr, r_r_uid, r_l_uid),
        MakeDirectRawSend(r_l_ptr, r_r_uid, r_l_uid), chat::ChatSyncTiming{});
    r_l_ptr = &r_l;
    r_r_ptr = &r_r;
    r_l.Start();
    r_r.Start();
    r_l.AddPeer(r_r_uid);
    TickUntilInitialSync(r_l, r_r, r_l_uid, r_r_uid);

    SubmitViaChat(rec_l.graph.chat, rec_l.graph.local_client, "poll-recovery");
    // No LocalEventCommitted — Poll on Tick must discover it.
    for (int i = 0; i < 200; ++i) {
      r_l.Tick(ae::Now());
      r_r.Tick(ae::Now());
      SleepMs(5);
      if (CountNeedle(rec_r.Transcript(), "poll-recovery") == 1) {
        break;
      }
    }
    CHECK(CountNeedle(rec_r.Transcript(), "poll-recovery") == 1);
  }
}

void TestRetryTiming() {
  // A–H: time-based physical send cooldown (no WriteAction completion).
  auto left_uid = MakeUid(0xC3);
  auto right_uid = MakeUid(0xD4);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<ae::ObjId> sent_ids;
  bool deliver = false;

  chat::ChatSyncTiming timing;
  timing.retry_interval = std::chrono::milliseconds{10};
  timing.packet_retry_interval = std::chrono::milliseconds{2000};

  chat::ChatSyncController* left_ptr = nullptr;
  chat::ChatSyncController* right_ptr = nullptr;

  auto left_send = [&](ae::Uid const& peer, ae::ObjId packet_id,
                       SerializedSyncPacket const& bytes) {
    CHECK(peer == right_uid);
    sent_ids.push_back(packet_id);
    if (deliver) {
      assert(right_ptr != nullptr);
      right_ptr->Receive(left_uid, bytes);
    }
  };

  chat::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set, left_send,
      [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
        CHECK(peer == right_uid);
        if (deliver) {
          assert(right_ptr != nullptr);
          right_ptr->Receive(left_uid, bytes);
        }
      },
      timing);
  chat::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      [&](ae::Uid const& peer, ae::ObjId /*packet_id*/,
          SerializedSyncPacket const& bytes) {
        CHECK(peer == left_uid);
        assert(left_ptr != nullptr);
        left_ptr->Receive(right_uid, bytes);
      },
      [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
        CHECK(peer == left_uid);
        assert(left_ptr != nullptr);
        left_ptr->Receive(right_uid, bytes);
      },
      timing);
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();

  auto FindPendingBytes = [](SharedGraphSyncSession* session,
                             ae::ObjId packet_id) {
    for (auto const& pending : session->state()->data.pending_packets) {
      if (pending.packet_id == packet_id) {
        return pending.serialized_bytes;
      }
    }
    CHECK(false && "pending packet missing");
    return SerializedSyncPacket{};
  };

  auto DrainPendingViaAck = [&](chat::ChatSyncController& left_c,
                                SharedGraphSyncSession* session,
                                ae::TimePoint base) {
    deliver = true;
    for (int i = 0; i < 80; ++i) {
      if (session->pending_packet_count() == 0) {
        break;
      }
      auto const id = session->state()->data.pending_packets.front().packet_id;
      right_ctrl.Receive(left_uid, FindPendingBytes(session, id));
      left_c.Tick(base + std::chrono::milliseconds{i * 10});
      right_ctrl.Tick(base + std::chrono::milliseconds{i * 10});
    }
    CHECK(session->pending_packet_count() == 0);
    deliver = false;
  };

  // --- A: AddPeer — one physical send; immediate Ticks do not duplicate ---
  deliver = false;
  sent_ids.clear();
  left_ctrl.AddPeer(right_uid);
  CHECK(sent_ids.size() == 1);
  auto* session = left_ctrl.FindSession(right_uid);
  CHECK(session != nullptr);
  CHECK(session->pending_packet_count() == 1);
  auto const initial_id = sent_ids.front();
  CHECK(session->state()->data.pending_packets.front().packet_id ==
        initial_id);
  CHECK(left_ctrl.physical_attempt_count(right_uid, initial_id) == 1);

  auto t0 = ae::Now();
  sent_ids.clear();
  left_ctrl.Tick(t0);
  left_ctrl.Tick(t0 + std::chrono::milliseconds{5});
  left_ctrl.Tick(t0 + std::chrono::milliseconds{15});
  CHECK(sent_ids.empty());
  CHECK(left_ctrl.physical_attempt_count(right_uid, initial_id) == 1);

  // --- B: Retry deadline — no second send before 2000ms; one after ---
  sent_ids.clear();
  left_ctrl.Tick(t0 + std::chrono::milliseconds{1990});
  CHECK(sent_ids.empty());
  left_ctrl.Tick(t0 + std::chrono::milliseconds{2000});
  CHECK(sent_ids.size() == 1);
  CHECK(sent_ids.front() == initial_id);
  CHECK(left_ctrl.physical_attempt_count(right_uid, initial_id) == 2);
  sent_ids.clear();
  left_ctrl.Tick(t0 + std::chrono::milliseconds{2010});
  left_ctrl.Tick(t0 + std::chrono::milliseconds{2100});
  left_ctrl.Tick(t0 + std::chrono::milliseconds{2500});
  CHECK(sent_ids.empty());

  // --- C: Sustained no-ACK over 10s from first send ---
  // Expected sends at t0+0 (already done), +2000 (done in B), +4000..+10000.
  sent_ids.clear();
  auto const attempts_at_b =
      left_ctrl.physical_attempt_count(right_uid, initial_id);
  CHECK(attempts_at_b == 2);
  for (int ms = 2010; ms <= 10000; ms += 10) {
    left_ctrl.Tick(t0 + std::chrono::milliseconds{ms});
  }
  // Additional sends at 4000,6000,8000,10000 = 4 → total attempts 6 over 10s.
  CHECK(left_ctrl.physical_attempt_count(right_uid, initial_id) == 6);
  CHECK(sent_ids.size() == 4);
  for (auto const& id : sent_ids) {
    CHECK(id == initial_id);
  }

  DrainPendingViaAck(left_ctrl, session,
                     t0 + std::chrono::milliseconds{11000});
  CHECK(left_ctrl.write_gate_size(right_uid) == 0);

  // --- D: Application ACK removes pending + gate slot immediately ---
  deliver = false;
  left.Submit("ack-me");
  sent_ids.clear();
  auto const t_d = t0 + std::chrono::milliseconds{14000};
  left_ctrl.Tick(t_d);
  CHECK(sent_ids.size() == 1);
  auto const ack_target_id = sent_ids.front();
  CHECK(left_ctrl.write_gate_has(right_uid, ack_target_id));
  auto const ack_bytes = FindPendingBytes(session, ack_target_id);
  deliver = true;
  right_ctrl.Receive(left_uid, ack_bytes);
  // Receive path on left after right's ACK exchange:
  for (int i = 0; i < 40; ++i) {
    left_ctrl.Tick(t_d + std::chrono::milliseconds{10 + i * 10});
    right_ctrl.Tick(t_d + std::chrono::milliseconds{10 + i * 10});
    if (session->pending_packet_count() == 0) {
      break;
    }
  }
  CHECK(session->pending_packet_count() == 0);
  CHECK(!left_ctrl.write_gate_has(right_uid, ack_target_id));
  CHECK(left_ctrl.write_gate_size(right_uid) == 0);
  auto const sent_after_ack = sent_ids.size();
  left_ctrl.Tick(t_d + std::chrono::milliseconds{5000});
  left_ctrl.Tick(t_d + std::chrono::milliseconds{7000});
  CHECK(sent_ids.size() == sent_after_ack);
  deliver = false;

  // --- E: One-shot ACK packets bypass the gate ---
  right_ctrl.AddPeer(left_uid);
  right.Submit("trigger-ack");
  sent_ids.clear();
  right_ctrl.Tick(ae::Now());
  CHECK(!sent_ids.empty());
  auto is_left_pending = [&](ae::ObjId id) {
    for (auto const& pending : session->state()->data.pending_packets) {
      if (pending.packet_id == id) {
        return true;
      }
    }
    return false;
  };
  std::size_t oneshot_acks = 0;
  for (auto const& id : sent_ids) {
    if (!is_left_pending(id)) {
      ++oneshot_acks;
      CHECK(!left_ctrl.write_gate_has(right_uid, id));
    }
  }
  CHECK(oneshot_acks >= 1);

  auto const first_wave = sent_ids.size();
  right.Submit("trigger-ack-2");
  right_ctrl.Tick(ae::Now());
  CHECK(sent_ids.size() > first_wave);
  for (std::size_t i = first_wave; i < sent_ids.size(); ++i) {
    if (!is_left_pending(sent_ids[i])) {
      CHECK(!left_ctrl.write_gate_has(right_uid, sent_ids[i]));
    }
  }

  DrainPendingViaAck(left_ctrl, session, ae::Now());

  // --- F: Restart — persisted pending sends once; next Tick does not ---
  deliver = false;
  left.Submit("persist-me");
  left_ctrl.Tick(ae::Now());
  CHECK(session->pending_packet_count() >= 1);
  auto const persist_id =
      session->state()->data.pending_packets.front().packet_id;
  left.graph.app.Save();
  left_ctrl.Stop();
  CHECK(left_ctrl.write_gate_size(right_uid) == 0);

  left.DestroyRuntime();
  left.ReloadRuntime();
  sent_ids.clear();
  chat::ChatSyncController left2(
      left.Replica(), left.graph.chat, left.graph.peer_set, left_send,
      [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
        CHECK(peer == right_uid);
        if (deliver) {
          assert(right_ptr != nullptr);
          right_ptr->Receive(left_uid, bytes);
        }
      },
      timing);
  left_ptr = &left2;
  left2.Start();
  CHECK(sent_ids.size() == 1);
  CHECK(sent_ids.front() == persist_id);
  auto* session2 = left2.FindSession(right_uid);
  CHECK(session2 != nullptr);
  CHECK(session2->pending_packet_count() >= 1);
  CHECK(left2.physical_attempt_count(right_uid, persist_id) == 1);
  sent_ids.clear();
  auto const t_f = ae::Now();
  left2.Tick(t_f);
  left2.Tick(t_f + std::chrono::milliseconds{20});
  CHECK(sent_ids.empty());

  DrainPendingViaAck(left2, session2, t_f + std::chrono::milliseconds{100});

  // --- G: Two pending packets — independent deadlines ---
  deliver = false;
  left2.Stop();
  chat::ChatSyncController left3(
      left.Replica(), left.graph.chat, left.graph.peer_set, left_send,
      [&](ae::Uid const&, std::vector<std::uint8_t> const&) {}, timing);
  left_ptr = &left3;
  left3.Start();
  auto* session3 = left3.FindSession(right_uid);
  CHECK(session3 != nullptr);
  CHECK(session3->pending_packet_count() == 0);

  left.Submit("old-pkt");
  sent_ids.clear();
  auto t_g = ae::Now();
  left3.Tick(t_g);
  CHECK(sent_ids.size() == 1);
  auto const old_id = sent_ids.front();
  sent_ids.clear();
  left3.Tick(t_g + std::chrono::milliseconds{10});
  CHECK(sent_ids.empty());

  left.Submit("new-pkt");
  left3.Tick(t_g + std::chrono::milliseconds{50});
  CHECK(sent_ids.size() == 1);
  auto const new_id = sent_ids.front();
  CHECK(new_id != old_id);
  sent_ids.clear();
  left3.Tick(t_g + std::chrono::milliseconds{60});
  CHECK(sent_ids.empty());

  left3.Tick(t_g + std::chrono::milliseconds{1990});
  CHECK(sent_ids.empty());
  left3.Tick(t_g + std::chrono::milliseconds{2000});
  CHECK(sent_ids.size() == 1);
  CHECK(sent_ids.front() == old_id);
  sent_ids.clear();
  left3.Tick(t_g + std::chrono::milliseconds{2040});
  CHECK(sent_ids.empty());
  left3.Tick(t_g + std::chrono::milliseconds{2050});
  CHECK(sent_ids.size() == 1);
  CHECK(sent_ids.front() == new_id);
  CHECK(session3->pending_packet_count() >= 2);

  // --- H: Stop clears runtime gate; Start sends pending once ---
  left3.Stop();
  CHECK(left3.write_gate_size(right_uid) == 0);
  sent_ids.clear();
  left3.Start();
  // StartOrResume re-offers pending; gate was cleared so each pending may send
  // once.
  CHECK(!sent_ids.empty());
  auto const sent_on_restart = sent_ids.size();
  CHECK(sent_on_restart >= 1);
  sent_ids.clear();
  auto const t_h = ae::Now();
  left3.Tick(t_h);
  left3.Tick(t_h + std::chrono::milliseconds{20});
  CHECK(sent_ids.empty());
}


void TestChatPresenceCodec() {
  auto online = chat::EncodeChatPresence(chat::ChatPresenceMessage::kOnline);
  auto heartbeat =
      chat::EncodeChatPresence(chat::ChatPresenceMessage::kHeartbeat);
  auto offline =
      chat::EncodeChatPresence(chat::ChatPresenceMessage::kOffline);
  CHECK(std::string(online.begin(), online.end()) == "APPTRAVERSE_CHAT_ONLINE_V1");
  CHECK(std::string(heartbeat.begin(), heartbeat.end()) ==
        "APPTRAVERSE_CHAT_HEARTBEAT_V1");
  CHECK(std::string(offline.begin(), offline.end()) ==
        "APPTRAVERSE_CHAT_OFFLINE_V1");
  CHECK(chat::TryDecodeChatPresence(online) ==
        chat::ChatPresenceMessage::kOnline);
  CHECK(chat::TryDecodeChatPresence(heartbeat) ==
        chat::ChatPresenceMessage::kHeartbeat);
  CHECK(chat::TryDecodeChatPresence(offline) ==
        chat::ChatPresenceMessage::kOffline);
  std::vector<std::uint8_t> junk{'x'};
  CHECK(!chat::TryDecodeChatPresence(junk).has_value());
}

void TestPresenceTransitionsAndIsolation() {
  auto left_uid = MakeUid(0xE5);
  auto right_uid = MakeUid(0xF6);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<std::string> logs;
  auto make_log = [&](std::string prefix) {
    return [&, prefix](std::string const& line) {
      logs.push_back(prefix + line);
    };
  };

  chat::ChatSyncTiming timing;
  timing.heartbeat_interval = std::chrono::milliseconds{50};
  timing.offline_timeout = std::chrono::milliseconds{200};
  timing.retry_interval = std::chrono::milliseconds{50};

  chat::ChatSyncController* left_ptr = nullptr;
  chat::ChatSyncController* right_ptr = nullptr;

  std::vector<std::vector<std::uint8_t>> left_raw_sent;

  chat::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      // Sync packets are dropped so presence can be tested in isolation.
      [&](ae::Uid const& peer, ae::ObjId, SerializedSyncPacket const&) {
        CHECK(peer == right_uid);
      },
      [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
        CHECK(peer == right_uid);
        left_raw_sent.push_back(bytes);
        assert(right_ptr != nullptr);
        right_ptr->Receive(left_uid, bytes);
      }, timing, {}, make_log("L:"));
  chat::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      [&](ae::Uid const& peer, ae::ObjId, SerializedSyncPacket const&) {
        CHECK(peer == left_uid);
      },
      MakeDirectRawSend(left_ptr, right_uid, left_uid), timing, {}, make_log("R:"));
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();

  auto const joins_before = CountJoinEvents(right.graph.chat);
  right.graph.chat.Load();
  auto const journal_before = right.graph.chat->journal.size();

  // 1. First Online -> exactly one CHAT_PEER_ONLINE on right (after Tick drains
  // deferred auto-accept; Receive must not create the peer synchronously).
  left_ctrl.AddPeer(right_uid);
  CHECK(right_ctrl.FindSession(left_uid) == nullptr);
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 0);
  right_ctrl.Tick(ae::Now());
  CHECK(right_ctrl.FindSession(left_uid) == nullptr);
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 0);
  right_ctrl.Tick(ae::Now());
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 1);
  CHECK(CountLog(logs, "R:CHAT_PEER_REJOINED") == 0);

  auto* right_session = right_ctrl.FindSession(left_uid);
  CHECK(right_session != nullptr);
  right_session->state().Load();
  auto const delivered_before =
      right_session->state()->data.delivered_event_ids.size();
  auto const pending_before =
      right_session->state()->data.pending_packets.size();
  right.graph.chat.Load();
  CHECK(right.graph.chat->journal.size() == journal_before);
  CHECK(CountJoinEvents(right.graph.chat) == joins_before);

  // 2. Repeated heartbeat -> no second ONLINE marker.
  auto t0 = ae::Now();
  left_ctrl.Tick(t0 + std::chrono::milliseconds{60});
  left_ctrl.Tick(t0 + std::chrono::milliseconds{120});
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 1);
  CHECK(CountLog(logs, "R:CHAT_PEER_REJOINED") == 0);
  bool saw_heartbeat = false;
  for (auto const& bytes : left_raw_sent) {
    auto decoded = chat::TryDecodeChatPresence(bytes);
    if (decoded == chat::ChatPresenceMessage::kHeartbeat) {
      saw_heartbeat = true;
    }
  }
  CHECK(saw_heartbeat);

  // 8. Presence frames not in Chat journal / pending / delivered Event IDs.
  right.graph.chat.Load();
  CHECK(right.graph.chat->journal.size() == journal_before);
  right_session->state().Load();
  CHECK(right_session->state()->data.delivered_event_ids.size() ==
        delivered_before);
  CHECK(right_session->state()->data.pending_packets.size() == pending_before);

  // 3. Timeout -> one CHAT_PEER_OFFLINE reason=timeout.
  auto const after_hb = ae::Now();
  right_ctrl.Tick(after_hb + std::chrono::milliseconds{250});
  CHECK(CountLog(logs, "R:CHAT_PEER_OFFLINE") == 1);
  CHECK(CountLog(logs, "reason=timeout") == 1);

  // 4. Heartbeat after timeout -> CHAT_PEER_REJOINED.
  right_ctrl.Receive(
      left_uid,
      chat::EncodeChatPresence(chat::ChatPresenceMessage::kHeartbeat));
  CHECK(CountLog(logs, "R:CHAT_PEER_REJOINED") == 1);
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 1);

  // 5. Explicit Offline -> immediate offline.
  right_ctrl.Receive(
      left_uid,
      chat::EncodeChatPresence(chat::ChatPresenceMessage::kOffline));
  CHECK(CountLog(logs, "R:CHAT_PEER_OFFLINE") == 2);
  CHECK(CountLog(logs, "reason=explicit") == 1);

  // 6. Repeated offline -> no duplicate marker.
  right_ctrl.Receive(
      left_uid,
      chat::EncodeChatPresence(chat::ChatPresenceMessage::kOffline));
  CHECK(CountLog(logs, "R:CHAT_PEER_OFFLINE") == 2);

  // 9. Restart/rejoin does not create new JoinClientEvent.
  right_ctrl.Receive(
      left_uid,
      chat::EncodeChatPresence(chat::ChatPresenceMessage::kOnline));
  CHECK(CountLog(logs, "R:CHAT_PEER_REJOINED") == 2);
  CHECK(CountJoinEvents(right.graph.chat) == joins_before);
  right.graph.chat.Load();
  CHECK(right.graph.chat->journal.size() == journal_before);
}

void TestSyncPacketBringsPeerOnline() {
  auto left_uid = MakeUid(0x17);
  auto right_uid = MakeUid(0x18);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<std::string> logs;
  auto make_log = [&](std::string prefix) {
    return [&, prefix](std::string const& line) {
      logs.push_back(prefix + line);
    };
  };

  chat::ChatSyncController* left_ptr = nullptr;
  chat::ChatSyncController* right_ptr = nullptr;

  // Raw send from left is a no-op so ONLINE presence does not reach right.
  chat::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      MakeDirectSend(right_ptr, left_uid, right_uid),
      [](ae::Uid const&, std::vector<std::uint8_t> const&) {}, chat::ChatSyncTiming{}, {},
      make_log("L:"));
  chat::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectSend(left_ptr, right_uid, left_uid),
      MakeDirectRawSend(left_ptr, right_uid, left_uid), chat::ChatSyncTiming{}, {},
      make_log("R:"));
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();

  // 7. Sync packet without presence also brings peer online.
  // Presence raw_send is a no-op; only SerializedSyncPacket reaches right.
  // Auto-accept is deferred until Tick.
  left_ctrl.AddPeer(right_uid);
  CHECK(right_ctrl.FindSession(left_uid) == nullptr);
  right_ctrl.Tick(ae::Now());
  CHECK(right_ctrl.FindSession(left_uid) == nullptr);
  right_ctrl.Tick(ae::Now());
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 1);
  CHECK(CountLog(logs, "R:CHAT_PEER_REJOINED") == 0);

  // A later EventPacket must not emit a second ONLINE marker.
  left.Submit("sync-online-probe");
  for (int i = 0; i < 200; ++i) {
    left_ctrl.Tick(ae::Now());
    right_ctrl.Tick(ae::Now());
    SleepMs(5);
    right.graph.chat.Load();
    if (right.Transcript().find("sync-online-probe") != std::string::npos) {
      break;
    }
  }
  CHECK(right.Transcript().find("sync-online-probe") != std::string::npos);
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 1);
  CHECK(CountLog(logs, "R:CHAT_PEER_REJOINED") == 0);
}

void TestDeferredAutoAcceptCases() {
  auto left_uid = MakeUid(0x2A);
  auto right_uid = MakeUid(0x2B);

  // B. unknown peer → Receive does not create synchronously;
  // after Tick one peer/session exists and queued packet is processed.
  {
    Side left{"Windows", left_uid};
    Side right{"Android", right_uid};
    std::vector<std::string> logs;
    chat::ChatSyncController* left_ptr = nullptr;
    chat::ChatSyncController* right_ptr = nullptr;
    chat::ChatSyncController left_ctrl(
        left.Replica(), left.graph.chat, left.graph.peer_set,
        MakeDirectSend(right_ptr, left_uid, right_uid),
        MakeDirectRawSend(right_ptr, left_uid, right_uid),
        chat::ChatSyncTiming{});
    chat::ChatSyncController right_ctrl(
        right.Replica(), right.graph.chat, right.graph.peer_set,
        MakeDirectSend(left_ptr, right_uid, left_uid),
        MakeDirectRawSend(left_ptr, right_uid, left_uid),
        chat::ChatSyncTiming{}, {},
        [&](std::string const& line) { logs.push_back(line); });
    left_ptr = &left_ctrl;
    right_ptr = &right_ctrl;
    left_ctrl.Start();
    right_ctrl.Start();

    right_ctrl.Receive(
        left_uid,
        chat::EncodeChatPresence(chat::ChatPresenceMessage::kOnline));
    CHECK(right_ctrl.FindSession(left_uid) == nullptr);
    CHECK(right_ctrl.runtime_session_count() == 0);
    CHECK(CountLog(logs, "CHAT_PEER_ADDED") == 0);
    CHECK(CountLog(logs, "CHAT_PEER_AUTO_ACCEPT_QUEUED") == 1);

    right_ctrl.Tick(ae::Now());
    CHECK(right_ctrl.FindSession(left_uid) == nullptr);
    CHECK(CountLog(logs, "CHAT_PEER_ADDED") == 0);

    right_ctrl.Tick(ae::Now());
    CHECK(right_ctrl.FindSession(left_uid) != nullptr);
    CHECK(right_ctrl.runtime_session_count() == 1);
    CHECK(CountLog(logs, "CHAT_PEER_ADDED") == 1);
    CHECK(CountLog(logs, "CHAT_PEER_ONLINE") == 1);
    right.graph.peer_set.Load();
    CHECK(right.graph.peer_set->peers.size() == 1);
    CHECK(right.graph.peer_set->peers[0].remote_uid == left_uid);
  }

  // C. two packets from same unknown UID before Tick → one peer/session;
  // both processed in original order.
  {
    Side left{"Windows", left_uid};
    Side right{"Android", right_uid};
    std::vector<std::string> logs;
    chat::ChatSyncController* left_ptr = nullptr;
    chat::ChatSyncController* right_ptr = nullptr;
    chat::ChatSyncController left_ctrl(
        left.Replica(), left.graph.chat, left.graph.peer_set,
        MakeDirectSend(right_ptr, left_uid, right_uid),
        MakeDirectRawSend(right_ptr, left_uid, right_uid),
        chat::ChatSyncTiming{});
    chat::ChatSyncController right_ctrl(
        right.Replica(), right.graph.chat, right.graph.peer_set,
        MakeDirectSend(left_ptr, right_uid, left_uid),
        MakeDirectRawSend(left_ptr, right_uid, left_uid),
        chat::ChatSyncTiming{}, {},
        [&](std::string const& line) { logs.push_back(line); });
    left_ptr = &left_ctrl;
    right_ptr = &right_ctrl;
    left_ctrl.Start();
    right_ctrl.Start();

    right_ctrl.Receive(
        left_uid,
        chat::EncodeChatPresence(chat::ChatPresenceMessage::kOnline));
    right_ctrl.Receive(
        left_uid,
        chat::EncodeChatPresence(chat::ChatPresenceMessage::kHeartbeat));
    CHECK(right_ctrl.FindSession(left_uid) == nullptr);
    CHECK(CountLog(logs, "CHAT_PEER_AUTO_ACCEPT_QUEUED") == 1);

    right_ctrl.Tick(ae::Now());
    CHECK(right_ctrl.FindSession(left_uid) == nullptr);
    right_ctrl.Tick(ae::Now());
    CHECK(right_ctrl.runtime_session_count() == 1);
    CHECK(CountLog(logs, "CHAT_PEER_ADDED") == 1);
    CHECK(CountLog(logs, "CHAT_PEER_ONLINE") == 1);
    // Heartbeat after ONLINE must not emit REJOINED (same connection).
    CHECK(CountLog(logs, "CHAT_PEER_REJOINED") == 0);
    right.graph.peer_set.Load();
    CHECK(right.graph.peer_set->peers.size() == 1);
  }

  // D. explicit AddPeer before deferred queue drains → still one peer/session;
  // queued packet is processed.
  {
    Side left{"Windows", left_uid};
    Side right{"Android", right_uid};
    std::vector<std::string> logs;
    chat::ChatSyncController* left_ptr = nullptr;
    chat::ChatSyncController* right_ptr = nullptr;
    chat::ChatSyncController left_ctrl(
        left.Replica(), left.graph.chat, left.graph.peer_set,
        MakeDirectSend(right_ptr, left_uid, right_uid),
        MakeDirectRawSend(right_ptr, left_uid, right_uid),
        chat::ChatSyncTiming{});
    chat::ChatSyncController right_ctrl(
        right.Replica(), right.graph.chat, right.graph.peer_set,
        MakeDirectSend(left_ptr, right_uid, left_uid),
        MakeDirectRawSend(left_ptr, right_uid, left_uid),
        chat::ChatSyncTiming{}, {},
        [&](std::string const& line) { logs.push_back(line); });
    left_ptr = &left_ctrl;
    right_ptr = &right_ctrl;
    left_ctrl.Start();
    right_ctrl.Start();

    right_ctrl.Receive(
        left_uid,
        chat::EncodeChatPresence(chat::ChatPresenceMessage::kOnline));
    CHECK(right_ctrl.FindSession(left_uid) == nullptr);
    right_ctrl.AddPeer(left_uid);
    CHECK(right_ctrl.runtime_session_count() == 1);
    CHECK(CountLog(logs, "CHAT_PEER_ADDED") == 1);
    // Queued Online still pending until Tick.
    CHECK(CountLog(logs, "CHAT_PEER_ONLINE") == 0);

    right_ctrl.Tick(ae::Now());
    CHECK(CountLog(logs, "CHAT_PEER_ONLINE") == 0);
    right_ctrl.Tick(ae::Now());
    CHECK(right_ctrl.runtime_session_count() == 1);
    CHECK(CountLog(logs, "CHAT_PEER_ADDED") == 1);
    CHECK(CountLog(logs, "CHAT_PEER_ONLINE") == 1);
    right.graph.peer_set.Load();
    CHECK(right.graph.peer_set->peers.size() == 1);
  }

  // E. auto-accepted peer persisted → restart discovers it without UI AddPeer.
  {
    Side left{"Windows", left_uid};
    Side right{"Android", right_uid};
    chat::ChatSyncController* left_ptr = nullptr;
    chat::ChatSyncController* right_ptr = nullptr;
    chat::ChatSyncController left_ctrl(
        left.Replica(), left.graph.chat, left.graph.peer_set,
        MakeDirectSend(right_ptr, left_uid, right_uid),
        MakeDirectRawSend(right_ptr, left_uid, right_uid),
        chat::ChatSyncTiming{});
    chat::ChatSyncController right_ctrl(
        right.Replica(), right.graph.chat, right.graph.peer_set,
        MakeDirectSend(left_ptr, right_uid, left_uid),
        MakeDirectRawSend(left_ptr, right_uid, left_uid),
        chat::ChatSyncTiming{});
    left_ptr = &left_ctrl;
    right_ptr = &right_ctrl;
    left_ctrl.Start();
    right_ctrl.Start();

    right_ctrl.Receive(
        left_uid,
        chat::EncodeChatPresence(chat::ChatPresenceMessage::kOnline));
    right_ctrl.Tick(ae::Now());
    right_ctrl.Tick(ae::Now());
    CHECK(right_ctrl.runtime_session_count() == 1);
    right.graph.peer_set.Save();
    right.graph.chat.Save();
    right.DestroyRuntime();
    right.ReloadRuntime();

    std::vector<ae::Uid> connect_attempts;
    chat::ChatSyncController right2(
        right.Replica(), right.graph.chat, right.graph.peer_set,
        MakeDirectSend(left_ptr, right_uid, left_uid),
        MakeDirectRawSend(left_ptr, right_uid, left_uid),
        chat::ChatSyncTiming{});
    right_ptr = &right2;
    // Simulate ChatComponent::Start connect loop for persisted peers.
    right.graph.peer_set.Load();
    for (auto const& peer : right.graph.peer_set->peers) {
      connect_attempts.push_back(peer.remote_uid);
    }
    right2.Start();
    CHECK(connect_attempts.size() == 1);
    CHECK(connect_attempts[0] == left_uid);
    CHECK(right2.runtime_session_count() == 1);
    CHECK(right2.FindSession(left_uid) != nullptr);
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestChatPresenceCodec();
  apptraverse::test::TestDeferredAutoAcceptCases();
  apptraverse::test::TestPresenceTransitionsAndIsolation();
  apptraverse::test::TestSyncPacketBringsPeerOnline();
  apptraverse::test::TestMessagesBeforePairingPersistAndMerge();
  apptraverse::test::TestControllerBidirectionalAndRestart();
  apptraverse::test::TestImmediateEventPublishAndPollFallback();
  apptraverse::test::TestRetryTiming();
  std::cout << "chat_sync_controller_test OK\n";
  return 0;
}
