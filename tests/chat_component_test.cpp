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

#include "chat_component.h"
#include "chat_presentation.h"
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
    CHECK(graph.chat_presenter.is_loaded());
  }
};

examples::ChatComponent::SendFunction MakeDirectSend(
    examples::ChatComponent*& peer, ae::Uid const& self_uid,
    ae::Uid const& peer_uid) {
  return [&peer, self_uid, peer_uid](ae::Uid const& dest, ae::ObjId,
                                     SerializedSyncPacket const& bytes) {
    CHECK(dest == peer_uid);
    assert(peer != nullptr);
    peer->Receive(self_uid, bytes);
  };
}

examples::ChatComponent::RawSendFunction MakeDirectRawSend(
    examples::ChatComponent*& peer, ae::Uid const& self_uid,
    ae::Uid const& peer_uid) {
  return [&peer, self_uid, peer_uid](ae::Uid const& dest,
                                     std::vector<std::uint8_t> const& bytes) {
    CHECK(dest == peer_uid);
    assert(peer != nullptr);
    peer->Receive(self_uid, bytes);
  };
}

std::unique_ptr<examples::ChatComponent> MakeComponent(
    Side& side, examples::ChatComponent*& peer_ptr, ae::Uid const& peer_uid,
    bool auto_accept) {
  return std::make_unique<examples::ChatComponent>(
      side.Replica(), side.graph.local_client, side.graph.chat,
      side.graph.chat_presenter, side.graph.peer_set,
      MakeDirectSend(peer_ptr, side.self_uid, peer_uid),
      MakeDirectRawSend(peer_ptr, side.self_uid, peer_uid),
      examples::ChatSyncTiming{}, auto_accept);
}

bool TimelineHasMessage(examples::ChatPresentationSnapshot const& snap,
                        std::string const& text,
                        examples::ChatMessageDirection dir) {
  for (auto const& item : snap.timeline) {
    if (item.kind == examples::ChatTimelineItemKind::kMessage &&
        item.text == text && item.direction == dir) {
      return true;
    }
  }
  return false;
}

bool TimelineHasText(examples::ChatPresentationSnapshot const& snap,
                     std::string const& text) {
  for (auto const& item : snap.timeline) {
    if (item.kind == examples::ChatTimelineItemKind::kMessage &&
        item.text == text) {
      return true;
    }
  }
  return false;
}

void TickUntilInitialSync(examples::ChatComponent& left,
                          examples::ChatComponent& right,
                          ae::Uid const& left_uid, ae::Uid const& right_uid) {
  for (int i = 0; i < 400; ++i) {
    left.Tick(ae::Now());
    right.Tick(ae::Now());
    SleepMs(5);
    auto* ls = left.FindSession(right_uid);
    auto* rs = right.FindSession(left_uid);
    if (ls && rs && ls->initial_sync_complete() &&
        rs->initial_sync_complete()) {
      return;
    }
  }
  CHECK(false && "initial sync did not complete");
}

void TestHeadlessStartStop() {
  Side side{"Headless", MakeUid(0x31)};
  examples::ChatComponent* unused = nullptr;
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
  component->Stop();  // idempotent
  CHECK(!component->is_running());
}

void TestTwoIndependentComponents() {
  Side left{"Left", MakeUid(0x41)};
  Side right{"Right", MakeUid(0x42)};
  examples::ChatComponent* l_peer = nullptr;
  examples::ChatComponent* r_peer = nullptr;
  auto left_c = MakeComponent(left, r_peer, right.self_uid, false);
  auto right_c = MakeComponent(right, l_peer, left.self_uid, true);
  l_peer = left_c.get();
  r_peer = right_c.get();
  left_c->Start();
  right_c->Start();
  CHECK(left_c->is_running());
  CHECK(right_c->is_running());
  CHECK(left_c->runtime_session_count() == 0);
  CHECK(right_c->runtime_session_count() == 0);
  left_c->Stop();
  right_c->Stop();
}

void TestSubmitTextUpdatesPresentation() {
  Side side{"Local", MakeUid(0x51)};
  examples::ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0x52), false);
  component->Start();
  CHECK(component->SubmitText("  hello-local  "));
  CHECK(!component->SubmitText("   "));
  auto snap = component->CapturePresentation();
  CHECK(TimelineHasMessage(snap, "hello-local",
                           examples::ChatMessageDirection::kLocal));
  component->Stop();
}

void TestRemoteSyncBetweenComponents() {
  auto left_uid = MakeUid(0x61);
  auto right_uid = MakeUid(0x62);
  Side left{"Windows", left_uid};
  Side right{"Android", right_uid};

  examples::ChatComponent* left_ptr = nullptr;
  examples::ChatComponent* right_ptr = nullptr;
  auto left_c = MakeComponent(left, right_ptr, right_uid, false);
  auto right_c = MakeComponent(right, left_ptr, left_uid, true);
  left_ptr = left_c.get();
  right_ptr = right_c.get();

  left_c->Start();
  right_c->Start();
  CHECK(left_c->AddPeer(right_uid).has_value());
  TickUntilInitialSync(*left_c, *right_c, left_uid, right_uid);

  CHECK(left_c->SubmitText("from-left"));
  for (int i = 0; i < 200; ++i) {
    left_c->Tick(ae::Now());
    right_c->Tick(ae::Now());
    SleepMs(5);
    auto snap = right_c->CapturePresentation();
    if (TimelineHasText(snap, "from-left")) {
      break;
    }
  }
  auto right_snap = right_c->CapturePresentation();
  CHECK(TimelineHasText(right_snap, "from-left"));
  CHECK(TimelineHasMessage(right_snap, "from-left",
                           examples::ChatMessageDirection::kRemote));

  left_c->Stop();
  right_c->Stop();
}

void TestStopClearsSubscriptions() {
  Side side{"Sub", MakeUid(0x71)};
  examples::ChatComponent* unused = nullptr;
  auto component = MakeComponent(side, unused, MakeUid(0x72), false);
  int callbacks = 0;
  component->Start();
  auto id = component->SubscribePresentationChanged([&]() { ++callbacks; });
  CHECK(id != 0);
  CHECK(component->SubmitText("ping"));
  CHECK(callbacks >= 1);
  int const after_submit = callbacks;
  component->Stop();
  int const after_stop = callbacks;
  // Further activity must not notify cleared subscribers.
  CHECK(!component->SubmitText("should-fail"));
  component->Receive(MakeUid(0x99), {});
  CHECK(callbacks == after_stop);
  // Destroy safely.
  component.reset();
  CHECK(callbacks == after_stop);
  (void)after_submit;
}

void TestRestartRestoresPersistedChat() {
  Side side{"Persist", MakeUid(0x81)};
  examples::ChatComponent* unused = nullptr;
  {
    auto component = MakeComponent(side, unused, MakeUid(0x82), false);
    component->Start();
    CHECK(component->SubmitText("persisted-msg"));
    side.graph.chat.Save();
    side.graph.app.Save();
    component->Stop();
  }
  side.DestroyRuntime();
  side.ReloadRuntime();
  {
    auto component = MakeComponent(side, unused, MakeUid(0x82), false);
    component->Start();
    auto snap = component->CapturePresentation();
    CHECK(TimelineHasText(snap, "persisted-msg"));
    component->Stop();
  }
}

}  // namespace apptraverse::test

// Headers under test must stay platform-neutral (no HWND / Activity).
#include "chat_component.h"
#include "chat_presentation.h"

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestHeadlessStartStop();
  apptraverse::test::TestTwoIndependentComponents();
  apptraverse::test::TestSubmitTextUpdatesPresentation();
  apptraverse::test::TestRemoteSyncBetweenComponents();
  apptraverse::test::TestStopClearsSubscriptions();
  apptraverse::test::TestRestartRestoresPersistedChat();
  std::cout << "chat_component_test OK\n";
  return 0;
}