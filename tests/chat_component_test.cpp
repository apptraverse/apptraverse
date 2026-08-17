#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/types/uid.h"

#include "aether-miscpp/format/format.h"

#include "apptraverse/object_macros.h"

#include "chat_component.h"
#include "chat_component_graph.h"
#include "chat_presentation.h"
#include "chat_presence.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_component_registration.h"
#include "model/chat_peer_set.h"
#include "model/client.h"

namespace apptraverse::test {

using chat::AddPeerResult;
using chat::BuildChatComponentGraph;
using chat::ChatComponent;
using chat::ChatComponentGraph;
using chat::ChatMessageDirection;
using chat::ChatPresentationSnapshot;
using chat::ChatPresenceMessage;
using chat::ChatSyncTiming;
using chat::ChatTimelineItemKind;
using chat::EncodeChatPresence;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

ae::Uid MakeUid(std::uint8_t fill) {
  std::array<std::uint8_t, ae::Uid::kSize> bytes{};
  bytes.fill(fill);
  return ae::Uid{bytes};
}

struct Side {
  ae::RamDomainStorage storage;
  std::unique_ptr<ae::Domain> domain;
  ChatComponentGraph graph;
  ae::Uid self_uid;
  ae::ObjId peer_set_id;
  ae::ObjId chat_id;
  ae::ObjId local_client_id;

  explicit Side(std::string name, ae::Uid uid)
      : domain{std::make_unique<ae::Domain>(ae::Now(), storage)},
        self_uid{uid} {
    graph = BuildChatComponentGraph(*domain, name);
    graph.chat.Save();
    peer_set_id = graph.peer_set.id();
    chat_id = graph.chat.id();
    local_client_id = graph.local_client.id();
  }

  SyncReplica Replica() {
    return SyncReplica{*domain, storage, graph.chat.id()};
  }

  void DestroyRuntime() {
    graph = ChatComponentGraph{};
    domain.reset();
  }

  void ReloadRuntime() {
    domain = std::make_unique<ae::Domain>(ae::Now(), storage);
    graph.chat =
        Chat::ptr::Declare(ae::CreateWith{*domain}.with_id(chat_id));
    graph.chat.Load();
    CHECK(graph.chat.is_loaded());
    graph.peer_set = ChatPeerSet::ptr::Declare(
        ae::CreateWith{*domain}.with_id(peer_set_id));
    graph.peer_set.Load();
    CHECK(graph.peer_set.is_loaded());
    graph.local_client = Client::ptr::Declare(
        ae::CreateWith{*domain}.with_id(local_client_id));
    graph.local_client.Load();
    CHECK(graph.local_client.is_loaded());
    graph.chat->peer_set = graph.peer_set;
  }
};

ChatComponent::SendFunction MakeDirectSend(ChatComponent*& peer,
                                           ae::Uid const& self_uid,
                                           ae::Uid const& peer_uid) {
  return [&peer, self_uid, peer_uid](ae::Uid const& dest, ae::ObjId,
                                     SerializedSyncPacket const& bytes) {
    CHECK(dest == peer_uid);
    if (peer == nullptr) {
      return;
    }
    peer->Receive(self_uid, bytes);
  };
}

ChatComponent::RawSendFunction MakeDirectRawSend(ChatComponent*& peer,
                                                 ae::Uid const& self_uid,
                                                 ae::Uid const& peer_uid) {
  return [&peer, self_uid, peer_uid](ae::Uid const& dest,
                                     std::vector<std::uint8_t> const& bytes) {
    CHECK(dest == peer_uid);
    if (peer == nullptr) {
      return;
    }
    peer->Receive(self_uid, bytes);
  };
}

std::unique_ptr<ChatComponent> MakeComponent(
    Side& side, ChatComponent*& peer_ptr, ae::Uid const& peer_uid,
    bool auto_accept, ChatComponent::ConnectFunction connect = {},
    ChatSyncTiming timing = {}) {
  return std::make_unique<ChatComponent>(
      side.Replica(), side.graph.local_client, side.graph.chat,
      MakeDirectSend(peer_ptr, side.self_uid, peer_uid),
      MakeDirectRawSend(peer_ptr, side.self_uid, peer_uid),
      std::move(connect), timing, auto_accept);
}

bool TimelineHasMessage(ChatPresentationSnapshot const& snap,
                        std::string const& text, ChatMessageDirection dir) {
  for (auto const& item : snap.timeline) {
    if (item.kind == ChatTimelineItemKind::kMessage && item.text == text &&
        item.direction == dir) {
      return true;
    }
  }
  return false;
}

bool TimelineHasText(ChatPresentationSnapshot const& snap,
                     std::string const& text) {
  for (auto const& item : snap.timeline) {
    if (item.kind == ChatTimelineItemKind::kMessage && item.text == text) {
      return true;
    }
  }
  return false;
}

std::optional<chat::ChatTimelineItemView> FindMessage(
    ChatPresentationSnapshot const& snap, std::string const& text) {
  for (auto const& item : snap.timeline) {
    if (item.kind == ChatTimelineItemKind::kMessage && item.text == text) {
      return item;
    }
  }
  return std::nullopt;
}

bool PeerSyncComplete(ChatPresentationSnapshot const& snap,
                      ae::Uid const& peer_uid) {
  auto const peer_text = ae::Format("{}", peer_uid);
  for (auto const& peer : snap.peers) {
    if (peer.remote_uid == peer_text && peer.initial_sync_complete) {
      return true;
    }
  }
  return false;
}

std::size_t PeerPending(ChatPresentationSnapshot const& snap,
                        ae::Uid const& peer_uid) {
  auto const peer_text = ae::Format("{}", peer_uid);
  for (auto const& peer : snap.peers) {
    if (peer.remote_uid == peer_text) {
      return peer.pending_packets;
    }
  }
  return 0;
}

bool PeerOnline(ChatPresentationSnapshot const& snap, ae::Uid const& peer_uid) {
  auto const peer_text = ae::Format("{}", peer_uid);
  for (auto const& peer : snap.peers) {
    if (peer.remote_uid == peer_text) {
      return peer.online;
    }
  }
  return false;
}

void TickPair(ChatComponent& left, ChatComponent& right, ae::TimePoint& now,
              std::chrono::milliseconds step = std::chrono::milliseconds{5}) {
  now += step;
  left.Tick(now);
  right.Tick(now);
}

void TickUntilInitialSync(ChatComponent& left, ChatComponent& right,
                          ae::Uid const& left_uid, ae::Uid const& right_uid,
                          ae::TimePoint& now) {
  for (int i = 0; i < 400; ++i) {
    TickPair(left, right, now);
    if (PeerSyncComplete(left.CapturePresentation(), right_uid) &&
        PeerSyncComplete(right.CapturePresentation(), left_uid)) {
      return;
    }
  }
  CHECK(false && "initial sync did not complete");
}

// A
void TestHeadlessStartStop() {
  Side side{"Headless", MakeUid(0x31)};
  ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0x32), false);
  CHECK(!component->is_running());
  auto snap0 = component->CapturePresentation();
  CHECK(!snap0.running);
  component->Start();
  CHECK(component->is_running());
  auto snap1 = component->CapturePresentation();
  CHECK(snap1.running);
  CHECK(!snap1.local_participant.display_name.empty());
  component->Stop();
  CHECK(!component->is_running());
  component->Stop();
  CHECK(!component->is_running());
}

// B
void TestTwoIndependentComponents() {
  Side left{"Left", MakeUid(0x41)};
  Side right{"Right", MakeUid(0x42)};
  ChatComponent* l_peer = nullptr;
  ChatComponent* r_peer = nullptr;
  auto left_c = MakeComponent(left, r_peer, right.self_uid, false);
  auto right_c = MakeComponent(right, l_peer, left.self_uid, true);
  l_peer = left_c.get();
  r_peer = right_c.get();
  left_c->Start();
  right_c->Start();
  CHECK(left_c->is_running());
  CHECK(right_c->is_running());
  CHECK(left_c->CapturePresentation().peers.empty());
  CHECK(right_c->CapturePresentation().peers.empty());
  left_c->Stop();
  right_c->Stop();
}

// C
void TestSubmitTextReturnsIdAndSnapshot() {
  Side side{"Local", MakeUid(0x51)};
  ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0x52), false);
  component->Start();
  auto const id = component->SubmitText("  hello-local  ");
  CHECK(id.has_value());
  CHECK(!component->SubmitText("   ").has_value());
  auto snap = component->CapturePresentation();
  auto item = FindMessage(snap, "hello-local");
  CHECK(item.has_value());
  CHECK(item->event_obj_id == *id);
  CHECK(item->timestamp_us != 0);
  CHECK(item->direction == ChatMessageDirection::kLocal);
  component->Stop();
}

// D
void TestPersistenceWithoutExternalSave() {
  Side side{"Persist", MakeUid(0x81)};
  ChatComponent* unused = nullptr;
  std::uint32_t event_id = 0;
  std::uint64_t event_ts = 0;
  {
    auto component = MakeComponent(side, unused, MakeUid(0x82), false);
    component->Start();
    auto const id = component->SubmitText("persisted-msg");
    CHECK(id.has_value());
    event_id = *id;
    auto item = FindMessage(component->CapturePresentation(), "persisted-msg");
    CHECK(item.has_value());
    event_ts = item->timestamp_us;
    CHECK(event_ts != 0);
    component->Stop();
  }
  side.DestroyRuntime();
  side.ReloadRuntime();
  {
    auto component = MakeComponent(side, unused, MakeUid(0x82), false);
    component->Start();
    auto item = FindMessage(component->CapturePresentation(), "persisted-msg");
    CHECK(item.has_value());
    CHECK(item->event_obj_id == event_id);
    CHECK(item->timestamp_us == event_ts);
    CHECK(item->text == "persisted-msg");
    component->Stop();
  }
}

// E
void TestRemoteSyncSameEventIdAndDirection() {
  auto left_uid = MakeUid(0x61);
  auto right_uid = MakeUid(0x62);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  ChatComponent* left_ptr = nullptr;
  ChatComponent* right_ptr = nullptr;
  auto left_c = MakeComponent(left, right_ptr, right_uid, false);
  auto right_c = MakeComponent(right, left_ptr, left_uid, true);
  left_ptr = left_c.get();
  right_ptr = right_c.get();

  auto now = ae::Now();
  left_c->Start();
  right_c->Start();
  CHECK(left_c->AddPeer(right_uid) == AddPeerResult::kAdded);
  TickUntilInitialSync(*left_c, *right_c, left_uid, right_uid, now);

  auto const id = left_c->SubmitText("from-left");
  CHECK(id.has_value());
  auto left_item = FindMessage(left_c->CapturePresentation(), "from-left");
  CHECK(left_item.has_value());
  CHECK(left_item->direction == ChatMessageDirection::kLocal);

  for (int i = 0; i < 400; ++i) {
    TickPair(*left_c, *right_c, now);
    if (TimelineHasText(right_c->CapturePresentation(), "from-left")) {
      break;
    }
  }
  auto right_item = FindMessage(right_c->CapturePresentation(), "from-left");
  CHECK(right_item.has_value());
  CHECK(right_item->event_obj_id == *id);
  CHECK(right_item->timestamp_us == left_item->timestamp_us);
  CHECK(right_item->direction == ChatMessageDirection::kRemote);

  left_c->Stop();
  right_c->Stop();
}

// F
void TestAddPeerPersistenceAndConnectOnStart() {
  Side side{"Peers", MakeUid(0x91)};
  auto peer_uid = MakeUid(0x92);
  ChatComponent* unused = nullptr;
  int connect_calls = 0;
  auto connect = [&](ae::Uid const& uid) {
    CHECK(uid == peer_uid);
    ++connect_calls;
  };
  {
    auto component =
        MakeComponent(side, unused, peer_uid, false, connect);
    component->Start();
    CHECK(connect_calls == 0);
    CHECK(component->AddPeer(peer_uid) == AddPeerResult::kAdded);
    CHECK(connect_calls == 1);
    side.graph.chat.Save();
    component->Stop();
  }
  side.DestroyRuntime();
  side.ReloadRuntime();
  connect_calls = 0;
  {
    auto component =
        MakeComponent(side, unused, peer_uid, false, connect);
    component->Start();
    CHECK(connect_calls == 1);
    auto snap = component->CapturePresentation();
    CHECK(snap.peers.size() == 1);
    CHECK(snap.peers[0].remote_uid == ae::Format("{}", peer_uid));
    component->Stop();
  }
}

// G
void TestNotificationOnLocalSubmit() {
  Side side{"Notify", MakeUid(0xA1)};
  ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0xA2), false);
  int callbacks = 0;
  component->Start();
  auto const baseline = callbacks;
  component->SubscribePresentationChanged([&]() { ++callbacks; });
  // Start already notified before subscribe; submit must notify.
  CHECK(component->SubmitText("ping").has_value());
  CHECK(callbacks == baseline + 1);
  component->Stop();
}

// H / I
void TestPendingNotifications() {
  auto left_uid = MakeUid(0xB1);
  auto right_uid = MakeUid(0xB2);
  Side left{"LeftPending", left_uid};
  Side right{"RightPending", right_uid};

  ChatComponent* left_ptr = nullptr;
  ChatComponent* right_ptr = nullptr;
  bool deliver_left_sync = true;

  auto left_c = std::make_unique<ChatComponent>(
      left.Replica(), left.graph.local_client, left.graph.chat,
      [&](ae::Uid const& dest, ae::ObjId, SerializedSyncPacket const& bytes) {
        CHECK(dest == right_uid);
        if (!deliver_left_sync || right_ptr == nullptr) {
          return;
        }
        right_ptr->Receive(left_uid, bytes);
      },
      MakeDirectRawSend(right_ptr, left_uid, right_uid),
      ChatComponent::ConnectFunction{}, ChatSyncTiming{}, false);
  auto right_c = MakeComponent(right, left_ptr, left_uid, true);
  left_ptr = left_c.get();
  right_ptr = right_c.get();

  auto now = ae::Now();
  left_c->Start();
  right_c->Start();
  CHECK(left_c->AddPeer(right_uid) == AddPeerResult::kAdded);
  TickUntilInitialSync(*left_c, *right_c, left_uid, right_uid, now);

  int left_notify = 0;
  bool saw_pending_n_notify = false;
  bool saw_pending_zero_notify = false;
  left_c->SubscribePresentationChanged([&]() {
    ++left_notify;
    auto const pending =
        PeerPending(left_c->CapturePresentation(), right_uid);
    if (pending > 0) {
      saw_pending_n_notify = true;
    }
    if (saw_pending_n_notify && pending == 0) {
      saw_pending_zero_notify = true;
    }
  });
  CHECK(PeerPending(left_c->CapturePresentation(), right_uid) == 0);

  deliver_left_sync = false;
  int const before_submit = left_notify;
  CHECK(left_c->SubmitText("needs-ack").has_value());
  CHECK(left_notify > before_submit);

  now += std::chrono::milliseconds{5};
  left_c->Tick(now);
  CHECK(PeerPending(left_c->CapturePresentation(), right_uid) > 0);
  CHECK(saw_pending_n_notify);

  deliver_left_sync = true;
  for (int i = 0; i < 600; ++i) {
    TickPair(*left_c, *right_c, now);
    if (PeerPending(left_c->CapturePresentation(), right_uid) == 0 &&
        TimelineHasText(right_c->CapturePresentation(), "needs-ack")) {
      break;
    }
  }
  CHECK(PeerPending(left_c->CapturePresentation(), right_uid) == 0);
  CHECK(saw_pending_zero_notify);
  CHECK(TimelineHasText(right_c->CapturePresentation(), "needs-ack"));

  left_c->Stop();
  right_c->Stop();
}

// J
void TestOnlineOfflinePresenceNotifications() {
  Side side{"Presence", MakeUid(0xC1)};
  auto peer_uid = MakeUid(0xC2);
  ChatSyncTiming timing;
  timing.offline_timeout = std::chrono::seconds{2};
  timing.heartbeat_interval = std::chrono::seconds{30};
  timing.retry_interval = std::chrono::milliseconds{100};

  // Drop sync packets so pending retries do not generate extra notifications.
  auto component = std::make_unique<ChatComponent>(
      side.Replica(), side.graph.local_client, side.graph.chat,
      [](ae::Uid const&, ae::ObjId, SerializedSyncPacket const&) {},
      [](ae::Uid const&, std::vector<std::uint8_t> const&) {},
      ChatComponent::ConnectFunction{}, timing, true);

  int notify = 0;
  component->Start();
  component->SubscribePresentationChanged([&]() { ++notify; });
  CHECK(component->AddPeer(peer_uid) == AddPeerResult::kAdded);
  CHECK(!PeerOnline(component->CapturePresentation(), peer_uid));

  int const before_online = notify;
  component->Receive(peer_uid,
                     EncodeChatPresence(ChatPresenceMessage::kOnline));
  CHECK(notify == before_online + 1);
  CHECK(PeerOnline(component->CapturePresentation(), peer_uid));

  int const after_online = notify;
  component->Receive(peer_uid,
                     EncodeChatPresence(ChatPresenceMessage::kHeartbeat));
  CHECK(notify == after_online);

  int const before_offline = notify;
  auto now = ae::Now() + timing.offline_timeout + std::chrono::seconds{1};
  component->Tick(now);
  CHECK(!PeerOnline(component->CapturePresentation(), peer_uid));
  CHECK(notify > before_offline);

  component->Stop();
}

// K
void TestStopReentrancy() {
  Side side{"StopRe", MakeUid(0xD1)};
  ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0xD2), false);
  int a_calls = 0;
  int b_calls = 0;
  ChatComponent::SubscriptionId b_id = 0;
  component->Start();
  component->SubscribePresentationChanged([&]() {
    ++a_calls;
    component->Stop();
  });
  b_id = component->SubscribePresentationChanged([&]() { ++b_calls; });
  CHECK(b_id != 0);
  CHECK(component->SubmitText("stop-me").has_value());
  CHECK(a_calls == 1);
  CHECK(b_calls == 0);
  CHECK(!component->is_running());
}

// L
void TestUnsubscribeReentrancy() {
  Side side{"UnsubRe", MakeUid(0xE1)};
  ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0xE2), false);
  int a_calls = 0;
  int b_calls = 0;
  ChatComponent::SubscriptionId b_id = 0;
  component->Start();
  component->SubscribePresentationChanged([&]() {
    ++a_calls;
    component->Unsubscribe(b_id);
  });
  b_id = component->SubscribePresentationChanged([&]() { ++b_calls; });
  CHECK(component->SubmitText("unsub-b").has_value());
  CHECK(a_calls == 1);
  CHECK(b_calls == 0);
  component->Stop();
}

// M
void TestStopThenStartNewSubscription() {
  Side side{"RestartSub", MakeUid(0xF1)};
  ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0xF2), false);
  int first = 0;
  int second = 0;
  component->Start();
  component->SubscribePresentationChanged([&]() { ++first; });
  CHECK(component->SubmitText("first").has_value());
  CHECK(first >= 1);
  component->Stop();
  component->Start();
  component->SubscribePresentationChanged([&]() { ++second; });
  int const second_before = second;
  CHECK(component->SubmitText("second").has_value());
  CHECK(second == second_before + 1);
  CHECK(first == first);  // first subscribers cleared on Stop
  component->Stop();
}

// N
void TestDestructionStopsCallbacks() {
  Side side{"Destroy", MakeUid(0x11)};
  ChatComponent* unused = nullptr;
  int callbacks = 0;
  {
    auto component = MakeComponent(side, unused, MakeUid(0x12), false);
    component->Start();
    component->SubscribePresentationChanged([&]() { ++callbacks; });
    CHECK(component->SubmitText("alive").has_value());
    CHECK(callbacks >= 1);
    component->Stop();
    int const after_stop = callbacks;
    component.reset();
    CHECK(callbacks == after_stop);
  }
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureChatComponentRegistration();
  apptraverse::test::TestHeadlessStartStop();
  apptraverse::test::TestTwoIndependentComponents();
  apptraverse::test::TestSubmitTextReturnsIdAndSnapshot();
  apptraverse::test::TestPersistenceWithoutExternalSave();
  apptraverse::test::TestRemoteSyncSameEventIdAndDirection();
  apptraverse::test::TestAddPeerPersistenceAndConnectOnStart();
  apptraverse::test::TestNotificationOnLocalSubmit();
  apptraverse::test::TestPendingNotifications();
  apptraverse::test::TestOnlineOfflinePresenceNotifications();
  apptraverse::test::TestStopReentrancy();
  apptraverse::test::TestUnsubscribeReentrancy();
  apptraverse::test::TestStopThenStartNewSubscription();
  apptraverse::test::TestDestructionStopsCallbacks();
  std::cout << "chat_component_test OK\n";
  return 0;
}