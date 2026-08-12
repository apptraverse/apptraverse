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

#include "aether_p2p_outgoing_routing.h"
#include "chat_sync_controller.h"
#include "graph_builder.h"
#include "model/app.h"
#include "model/application_ids.h"
#include "model/chat.h"
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

  std::string Transcript() const {
    return examples::FormatChatTranscriptUtf8(graph.chat);
  }
};

examples::SyncTransportOperations MakeDirectOps(
    examples::ChatSyncController*& peer_ctrl, ae::Uid const& self_uid,
    ae::Uid const& peer_uid,
    std::function<void(ae::ObjId, SerializedSyncPacket const&)> on_send = {}) {
  examples::SyncTransportOperations ops;
  ops.outgoing_state = [](ae::Uid const&) {
    return examples::P2pOutgoingState::kWritable;
  };
  ops.send = [&peer_ctrl, self_uid, peer_uid, on_send](
                 ae::Uid const& peer, ae::ObjId packet_id,
                 SerializedSyncPacket const& bytes) {
    CHECK(peer == peer_uid);
    if (on_send) {
      on_send(packet_id, bytes);
    }
    assert(peer_ctrl != nullptr);
    peer_ctrl->Receive(self_uid, bytes);
  };
  return ops;
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

  auto left_ops = MakeDirectOps(right_ptr, left_uid, right_uid);
  auto right_ops = MakeDirectOps(left_ptr, right_uid, left_uid);

  examples::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set, left_ops,
      examples::SyncRecoveryPolicy{}, false, {}, make_log("L:"));
  examples::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set, right_ops,
      examples::SyncRecoveryPolicy{}, true, {}, make_log("R:"));
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
      MakeDirectOps(right_ptr, left_uid, right_uid),
      examples::SyncRecoveryPolicy{}, false, {}, make_log("L2:"));
  examples::ChatSyncController right2(
      right.Replica(), right.graph.chat, right.graph.peer_set,
      MakeDirectOps(left_ptr, right_uid, left_uid),
      examples::SyncRecoveryPolicy{}, true, {}, make_log("R2:"));
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

void TestRetryGatedOnTransportState() {
  auto left_uid = MakeUid(0xC3);
  auto right_uid = MakeUid(0xD4);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<SerializedSyncPacket> sent;
  bool deliver = false;
  examples::P2pOutgoingState outgoing = examples::P2pOutgoingState::kWritable;

  examples::ChatSyncController* left_ptr = nullptr;
  examples::ChatSyncController* right_ptr = nullptr;

  examples::SyncTransportOperations left_ops;
  left_ops.outgoing_state = [&](ae::Uid const& peer) {
    CHECK(peer == right_uid);
    return outgoing;
  };
  left_ops.send = [&](ae::Uid const& peer, ae::ObjId /*packet_id*/,
                      SerializedSyncPacket const& bytes) {
    CHECK(peer == right_uid);
    sent.push_back(bytes);
    if (deliver) {
      assert(right_ptr != nullptr);
      right_ptr->Receive(left_uid, bytes);
    }
  };

  examples::SyncTransportOperations right_ops;
  right_ops.send = [&](ae::Uid const& peer, ae::ObjId /*packet_id*/,
                       SerializedSyncPacket const& bytes) {
    CHECK(peer == left_uid);
    assert(left_ptr != nullptr);
    left_ptr->Receive(right_uid, bytes);
  };

  examples::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set, left_ops,
      examples::SyncRecoveryPolicy{}, false);
  examples::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set, right_ops,
      examples::SyncRecoveryPolicy{}, true);
  left_ptr = &left_ctrl;
  right_ptr = &right_ctrl;

  left_ctrl.Start();
  right_ctrl.Start();

  left_ctrl.AddPeer(right_uid);
  auto pending_count = [&]() {
    auto const* session = left_ctrl.FindSession(right_uid);
    CHECK(session != nullptr);
    return session->pending_packet_count();
  };
  auto pending_id = [&]() {
    auto const* session = left_ctrl.FindSession(right_uid);
    CHECK(session != nullptr);
    CHECK(!session->state()->data.pending_packets.empty());
    return session->state()->data.pending_packets.front().packet_id;
  };
  CHECK(sent.size() == 1);
  CHECK(pending_count() == 1);
  auto const initial_bytes = sent.front();
  auto const initial_id = pending_id();

  sent.clear();
  outgoing = examples::P2pOutgoingState::kConnecting;
  for (int i = 0; i < 5; ++i) {
    left_ctrl.Tick(ae::Now());
    SleepMs(5);
  }
  CHECK(sent.empty());
  CHECK(pending_count() == 1);
  CHECK(pending_id() == initial_id);

  outgoing = examples::P2pOutgoingState::kWritable;
  left_ctrl.Tick(ae::Now());
  CHECK(sent.size() == 1);
  CHECK(sent.front() == initial_bytes);
  CHECK(pending_count() == 1);
  CHECK(pending_id() == initial_id);

  deliver = true;
  right_ctrl.Receive(left_uid, initial_bytes);
  for (int i = 0; i < 200 && pending_count() > 0; ++i) {
    left_ctrl.Tick(ae::Now());
    right_ctrl.Tick(ae::Now());
    SleepMs(5);
  }
  CHECK(pending_count() == 0);
}

void TestAckStallRecovery() {
  auto left_uid = MakeUid(0xE5);
  auto right_uid = MakeUid(0xF6);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  std::vector<std::pair<ae::ObjId, SerializedSyncPacket>> sent;
  int restream_calls = 0;
  int replace_calls = 0;
  examples::P2pOutgoingState outgoing = examples::P2pOutgoingState::kConnecting;

  examples::SyncRecoveryPolicy policy;
  policy.retry_interval = std::chrono::milliseconds{20};
  policy.restream_after = std::chrono::milliseconds{60};
  policy.replace_after = std::chrono::milliseconds{120};

  examples::SyncTransportOperations left_ops;
  left_ops.outgoing_state = [&](ae::Uid const&) { return outgoing; };
  left_ops.restream_outgoing = [&](ae::Uid const&) { ++restream_calls; };
  left_ops.replace_outgoing = [&](ae::Uid const&) {
    ++replace_calls;
    outgoing = examples::P2pOutgoingState::kConnecting;
  };
  left_ops.send = [&](ae::Uid const& peer, ae::ObjId packet_id,
                      SerializedSyncPacket const& bytes) {
    CHECK(peer == right_uid);
    sent.emplace_back(packet_id, bytes);
  };

  examples::ChatSyncController left_ctrl(
      left.Replica(), left.graph.chat, left.graph.peer_set, left_ops, policy,
      false);

  left_ctrl.Start();
  left_ctrl.AddPeer(right_uid);
  CHECK(!sent.empty());
  auto const initial_id = sent.front().first;
  auto const initial_bytes = sent.front().second;
  auto* session = left_ctrl.FindSession(right_uid);
  CHECK(session != nullptr);
  CHECK(session->pending_packet_count() == 1);
  auto const rev0 = session->ack_progress_revision();

  // A. Not writable: no constant retries; connecting skips restream; replace later.
  sent.clear();
  auto t0 = ae::Now();
  for (int i = 0; i < 5; ++i) {
    left_ctrl.Tick(t0 + std::chrono::milliseconds{i * 10});
  }
  CHECK(sent.empty());
  CHECK(restream_calls == 0);
  CHECK(replace_calls == 0);

  left_ctrl.Tick(t0 + std::chrono::milliseconds{70});
  CHECK(restream_calls == 0);  // connecting: do not restream
  CHECK(replace_calls == 0);

  // Writable stall triggers restream once.
  outgoing = examples::P2pOutgoingState::kWritable;
  left_ctrl.Tick(t0 + std::chrono::milliseconds{80});
  CHECK(restream_calls == 1);
  CHECK(replace_calls == 0);

  outgoing = examples::P2pOutgoingState::kConnecting;
  left_ctrl.Tick(t0 + std::chrono::milliseconds{130});
  CHECK(restream_calls == 1);
  CHECK(replace_calls == 1);

  // B. After replacement, same packet id/bytes on later writable retry.
  outgoing = examples::P2pOutgoingState::kWritable;
  sent.clear();
  left_ctrl.Tick(t0 + std::chrono::milliseconds{160});
  CHECK(!sent.empty());
  CHECK(sent.front().first == initial_id);
  CHECK(sent.front().second == initial_bytes);

  // C. ACK advances revision and resets recovery (no second replace).
  examples::ChatSyncController* right_ptr = nullptr;
  examples::SyncTransportOperations right_ops;
  right_ops.send = [&](ae::Uid const& peer, ae::ObjId /*packet_id*/,
                       SerializedSyncPacket const& bytes) {
    CHECK(peer == left_uid);
    left_ctrl.Receive(right_uid, bytes);
  };
  examples::ChatSyncController right_ctrl(
      right.Replica(), right.graph.chat, right.graph.peer_set, right_ops,
      examples::SyncRecoveryPolicy{}, true);
  right_ptr = &right_ctrl;
  (void)right_ptr;
  right_ctrl.Start();
  right_ctrl.AddPeer(left_uid);
  right_ctrl.Receive(left_uid, initial_bytes);

  for (int i = 0; i < 50; ++i) {
    left_ctrl.Tick(t0 + std::chrono::milliseconds{200 + i * 5});
    right_ctrl.Tick(t0 + std::chrono::milliseconds{200 + i * 5});
    if (session->pending_packet_count() == 0) {
      break;
    }
  }
  CHECK(session->pending_packet_count() == 0);
  CHECK(session->ack_progress_revision() > rev0);
  auto const rev_after_ack = session->ack_progress_revision();
  int const replace_after_ack = replace_calls;

  // D. Duplicate ACK does not change revision.
  {
    ae::RamDomainStorage build_storage;
    ae::Domain build_domain{ae::Now(), build_storage};
    auto ack = AckPacket::ptr::Create(ae::CreateWith{build_domain});
    ack->acknowledged_packet_id = initial_id;
    left_ctrl.Receive(right_uid, SyncPacketCodec{}.Encode(ack));
  }
  CHECK(session->ack_progress_revision() == rev_after_ack);

  // E. New pending after ACK gets a fresh recovery cycle.
  restream_calls = 0;
  replace_calls = 0;
  outgoing = examples::P2pOutgoingState::kWritable;
  left.Submit("after-ack");
  sent.clear();
  auto t1 = ae::Now();
  left_ctrl.Tick(t1);
  CHECK(session->pending_packet_count() >= 1);
  left_ctrl.Tick(t1 + std::chrono::milliseconds{70});
  CHECK(restream_calls == 1);
  left_ctrl.Tick(t1 + std::chrono::milliseconds{130});
  CHECK(replace_calls == 1);
  CHECK(replace_after_ack == 1);

  // F. Sender restart keeps packet identity; recovery timers restart.
  auto const* pending = &session->state()->data.pending_packets.front();
  auto const restart_id = pending->packet_id;
  auto const restart_bytes = pending->serialized_bytes;
  left.graph.chat.Save();
  left.graph.peer_set.Save();
  left.DestroyRuntime();
  left.ReloadRuntime();

  int restream2 = 0;
  int replace2 = 0;
  examples::P2pOutgoingState outgoing2 =
      examples::P2pOutgoingState::kConnecting;
  examples::SyncTransportOperations left2_ops;
  left2_ops.outgoing_state = [&](ae::Uid const&) { return outgoing2; };
  left2_ops.restream_outgoing = [&](ae::Uid const&) { ++restream2; };
  left2_ops.replace_outgoing = [&](ae::Uid const&) { ++replace2; };
  left2_ops.send = [&](ae::Uid const&, ae::ObjId packet_id,
                       SerializedSyncPacket const& bytes) {
    sent.emplace_back(packet_id, bytes);
  };
  examples::ChatSyncController left2(
      left.Replica(), left.graph.chat, left.graph.peer_set, left2_ops, policy,
      false);
  left2.Start();
  auto* session2 = left2.FindSession(right_uid);
  CHECK(session2 != nullptr);
  CHECK(session2->pending_packet_count() >= 1);
  CHECK(session2->state()->data.pending_packets.front().packet_id ==
        restart_id);
  CHECK(session2->state()->data.pending_packets.front().serialized_bytes ==
        restart_bytes);
  auto t2 = ae::Now();
  left2.Tick(t2 + std::chrono::milliseconds{10});
  CHECK(restream2 == 0);
  outgoing2 = examples::P2pOutgoingState::kWritable;
  left2.Tick(t2 + std::chrono::milliseconds{70});
  CHECK(restream2 == 1);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestControllerBidirectionalAndRestart();
  apptraverse::test::TestRetryGatedOnTransportState();
  apptraverse::test::TestAckStallRecovery();
  std::cout << "chat_sync_controller_test OK\n";
  return 0;
}
