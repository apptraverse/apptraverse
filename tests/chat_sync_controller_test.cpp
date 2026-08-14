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
    graph.chat_presenter->SubmitText(std::move(text));
    graph.chat.Save();
    graph.app.Save();
  }

  // Regression helper: only the presenter command path — no external Save.
  void SubmitWithoutExternalSave(std::string text) {
    SleepMs(2);
    graph.chat_presenter->SubmitText(std::move(text));
  }

  std::string Transcript() const {
    return examples::FormatChatTranscriptUtf8(graph.chat);
  }
};

examples::ChatSyncController::SendFunction MakeDirectSend(
    examples::ChatSyncController*& peer_ctrl, ae::Uid const& self_uid,
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

examples::ChatSyncController::RawSendFunction MakeDirectRawSend(
    examples::ChatSyncController*& peer_ctrl, ae::Uid const& self_uid,
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

void TickUntilInitialSync(examples::ChatSyncController& left_ctrl,
                          examples::ChatSyncController& right_ctrl,
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
  examples::ChatSyncController* windows_ptr = nullptr;
  examples::ChatSyncController* android_ptr = nullptr;
  examples::ChatSyncController windows_ctrl(
      windows.Replica(), windows.graph.chat, windows.graph.peer_set,
      MakeDirectSend(android_ptr, windows_uid, android_uid),
      MakeDirectRawSend(android_ptr, windows_uid, android_uid),
      examples::ChatSyncTiming{}, false);
  examples::ChatSyncController android_ctrl(
      android.Replica(), android.graph.chat, android.graph.peer_set,
      MakeDirectSend(windows_ptr, android_uid, windows_uid),
      MakeDirectRawSend(windows_ptr, android_uid, windows_uid),
      examples::ChatSyncTiming{}, true);
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
  examples::ChatSyncController windows2(
      windows.Replica(), windows.graph.chat, windows.graph.peer_set,
      MakeDirectSend(android_ptr, windows_uid, android_uid),
      MakeDirectRawSend(android_ptr, windows_uid, android_uid),
      examples::ChatSyncTiming{}, false);
  examples::ChatSyncController android2(
      android.Replica(), android.graph.chat, android.graph.peer_set,
      MakeDirectSend(windows_ptr, android_uid, windows_uid),
      MakeDirectRawSend(windows_ptr, android_uid, windows_uid),
      examples::ChatSyncTiming{}, true);
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

  examples::ChatSyncController* left_ptr = nullptr;
  examples::ChatSyncController* right_ptr = nullptr;

  examples::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      MakeDirectSend(right_ptr, left_uid, right_uid),
      MakeDirectRawSend(right_ptr, left_uid, right_uid), examples::ChatSyncTiming{}, false, {},
      make_log("L:"));
  examples::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectSend(left_ptr, right_uid, left_uid),
      MakeDirectRawSend(left_ptr, right_uid, left_uid), examples::ChatSyncTiming{}, true, {},
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

  examples::ChatSyncController left2(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      MakeDirectSend(right_ptr, left_uid, right_uid),
      MakeDirectRawSend(right_ptr, left_uid, right_uid), examples::ChatSyncTiming{}, false, {},
      make_log("L2:"));
  examples::ChatSyncController right2(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectSend(left_ptr, right_uid, left_uid),
      MakeDirectRawSend(left_ptr, right_uid, left_uid), examples::ChatSyncTiming{}, true, {},
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

void TestRetryTiming() {
  auto left_uid = MakeUid(0xC3);
  auto right_uid = MakeUid(0xD4);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<std::pair<ae::ObjId, SerializedSyncPacket>> sent;
  bool deliver = false;

  examples::ChatSyncTiming timing;
  timing.retry_interval = std::chrono::milliseconds{20};

  examples::ChatSyncController* left_ptr = nullptr;
  examples::ChatSyncController* right_ptr = nullptr;

  examples::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      [&](ae::Uid const& peer, ae::ObjId packet_id,
          SerializedSyncPacket const& bytes) {
        CHECK(peer == right_uid);
        sent.emplace_back(packet_id, bytes);
        if (deliver) {
          assert(right_ptr != nullptr);
          right_ptr->Receive(left_uid, bytes);
        }
      },
      [&](ae::Uid const& peer, std::vector<std::uint8_t> const& bytes) {
        CHECK(peer == right_uid);
        if (deliver) {
          assert(right_ptr != nullptr);
          right_ptr->Receive(left_uid, bytes);
        }
      },
      timing, false);
  examples::ChatSyncController right_ctrl(
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
      timing, true);
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();

  left_ctrl.AddPeer(right_uid);
  CHECK(sent.size() == 1);
  auto* session = left_ctrl.FindSession(right_uid);
  CHECK(session != nullptr);
  CHECK(session->pending_packet_count() == 1);
  auto const initial_id = sent.front().first;
  auto const initial_bytes = sent.front().second;
  CHECK(session->state()->data.pending_packets.front().packet_id ==
        initial_id);

  sent.clear();
  auto t0 = ae::Now();
  left_ctrl.Tick(t0);
  CHECK(sent.size() == 1);
  CHECK(sent.front().first == initial_id);
  CHECK(sent.front().second == initial_bytes);

  left_ctrl.Tick(t0 + std::chrono::milliseconds{50});
  CHECK(!sent.empty());
  CHECK(sent.back().first == initial_id);
  CHECK(sent.back().second == initial_bytes);

  deliver = true;
  right_ctrl.Receive(left_uid, initial_bytes);
  for (int i = 0; i < 50; ++i) {
    left_ctrl.Tick(t0 + std::chrono::milliseconds{200 + i * 5});
    right_ctrl.Tick(t0 + std::chrono::milliseconds{200 + i * 5});
    if (session->pending_packet_count() == 0) {
      break;
    }
  }
  CHECK(session->pending_packet_count() == 0);
  auto const sent_at_clear = sent.size();
  left_ctrl.Tick(t0 + std::chrono::milliseconds{500});
  left_ctrl.Tick(t0 + std::chrono::milliseconds{700});
  CHECK(sent.size() == sent_at_clear);

  deliver = false;
  left.Submit("after-ack");
  sent.clear();
  auto const t1 = t0 + std::chrono::milliseconds{1000};
  left_ctrl.Tick(t1);
  CHECK(session->pending_packet_count() >= 1);
  CHECK(!sent.empty());
  auto const new_id = sent.front().first;
  auto const new_bytes = sent.front().second;
  CHECK(new_id != initial_id);

  sent.clear();
  left_ctrl.Tick(t1 + std::chrono::milliseconds{30});
  CHECK(!sent.empty());
  CHECK(sent.back().first == new_id);
  CHECK(sent.back().second == new_bytes);

  right.graph.chat.Load();
  auto count_occurrences = [](std::string const& hay, std::string const& needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
      ++count;
      pos += needle.size();
    }
    return count;
  };
  CHECK(count_occurrences(right.Transcript(), "after-ack") == 0);

  deliver = true;
  right_ctrl.Receive(left_uid, new_bytes);
  right_ctrl.Receive(left_uid, new_bytes);
  for (int i = 0; i < 50; ++i) {
    left_ctrl.Tick(t1 + std::chrono::milliseconds{200 + i * 5});
    right_ctrl.Tick(t1 + std::chrono::milliseconds{200 + i * 5});
    if (session->pending_packet_count() == 0) {
      break;
    }
  }
  right.graph.chat.Load();
  CHECK(session->pending_packet_count() == 0);
  CHECK(count_occurrences(right.Transcript(), "after-ack") == 1);
}


void TestChatPresenceCodec() {
  auto online = examples::EncodeChatPresence(examples::ChatPresenceMessage::kOnline);
  auto heartbeat =
      examples::EncodeChatPresence(examples::ChatPresenceMessage::kHeartbeat);
  auto offline =
      examples::EncodeChatPresence(examples::ChatPresenceMessage::kOffline);
  CHECK(std::string(online.begin(), online.end()) == "APPTRAVERSE_CHAT_ONLINE_V1");
  CHECK(std::string(heartbeat.begin(), heartbeat.end()) ==
        "APPTRAVERSE_CHAT_HEARTBEAT_V1");
  CHECK(std::string(offline.begin(), offline.end()) ==
        "APPTRAVERSE_CHAT_OFFLINE_V1");
  CHECK(examples::TryDecodeChatPresence(online) ==
        examples::ChatPresenceMessage::kOnline);
  CHECK(examples::TryDecodeChatPresence(heartbeat) ==
        examples::ChatPresenceMessage::kHeartbeat);
  CHECK(examples::TryDecodeChatPresence(offline) ==
        examples::ChatPresenceMessage::kOffline);
  std::vector<std::uint8_t> junk{'x'};
  CHECK(!examples::TryDecodeChatPresence(junk).has_value());
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

  examples::ChatSyncTiming timing;
  timing.heartbeat_interval = std::chrono::milliseconds{50};
  timing.offline_timeout = std::chrono::milliseconds{200};
  timing.retry_interval = std::chrono::milliseconds{50};

  examples::ChatSyncController* left_ptr = nullptr;
  examples::ChatSyncController* right_ptr = nullptr;

  std::vector<std::vector<std::uint8_t>> left_raw_sent;

  examples::ChatSyncController left_ctrl(
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
      }, timing, false, {}, make_log("L:"));
  examples::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      [&](ae::Uid const& peer, ae::ObjId, SerializedSyncPacket const&) {
        CHECK(peer == left_uid);
      },
      MakeDirectRawSend(left_ptr, right_uid, left_uid), timing, true, {}, make_log("R:"));
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();

  auto const joins_before = CountJoinEvents(right.graph.chat);
  right.graph.chat.Load();
  auto const journal_before = right.graph.chat->journal.size();

  // 1. First Online -> exactly one CHAT_PEER_ONLINE on right.
  left_ctrl.AddPeer(right_uid);
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
    auto decoded = examples::TryDecodeChatPresence(bytes);
    if (decoded == examples::ChatPresenceMessage::kHeartbeat) {
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
      examples::EncodeChatPresence(examples::ChatPresenceMessage::kHeartbeat));
  CHECK(CountLog(logs, "R:CHAT_PEER_REJOINED") == 1);
  CHECK(CountLog(logs, "R:CHAT_PEER_ONLINE") == 1);

  // 5. Explicit Offline -> immediate offline.
  right_ctrl.Receive(
      left_uid,
      examples::EncodeChatPresence(examples::ChatPresenceMessage::kOffline));
  CHECK(CountLog(logs, "R:CHAT_PEER_OFFLINE") == 2);
  CHECK(CountLog(logs, "reason=explicit") == 1);

  // 6. Repeated offline -> no duplicate marker.
  right_ctrl.Receive(
      left_uid,
      examples::EncodeChatPresence(examples::ChatPresenceMessage::kOffline));
  CHECK(CountLog(logs, "R:CHAT_PEER_OFFLINE") == 2);

  // 9. Restart/rejoin does not create new JoinClientEvent.
  right_ctrl.Receive(
      left_uid,
      examples::EncodeChatPresence(examples::ChatPresenceMessage::kOnline));
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

  examples::ChatSyncController* left_ptr = nullptr;
  examples::ChatSyncController* right_ptr = nullptr;

  // Raw send from left is a no-op so ONLINE presence does not reach right.
  examples::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set,
      MakeDirectSend(right_ptr, left_uid, right_uid),
      [](ae::Uid const&, std::vector<std::uint8_t> const&) {}, examples::ChatSyncTiming{}, false, {},
      make_log("L:"));
  examples::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectSend(left_ptr, right_uid, left_uid),
      MakeDirectRawSend(left_ptr, right_uid, left_uid), examples::ChatSyncTiming{}, true, {},
      make_log("R:"));
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();

  // 7. Sync packet without presence also brings peer online.
  // Presence raw_send is a no-op; only SerializedSyncPacket reaches right.
  left_ctrl.AddPeer(right_uid);
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

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestChatPresenceCodec();
  apptraverse::test::TestPresenceTransitionsAndIsolation();
  apptraverse::test::TestSyncPacketBringsPeerOnline();
  apptraverse::test::TestMessagesBeforePairingPersistAndMerge();
  apptraverse::test::TestControllerBidirectionalAndRestart();
  apptraverse::test::TestRetryTiming();
  std::cout << "chat_sync_controller_test OK\n";
  return 0;
}
