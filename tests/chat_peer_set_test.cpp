#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/types/uid.h"

#include "apptraverse/object_macros.h"
#include "apptraverse/shared_graph.h"
#include "apptraverse/sync_packet.h"
#include "apptraverse/sync_session_state.h"

#include "graph_builder.h"
#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_peer_events.h"
#include "model/chat_peer_set.h"
#include "model/chat_presenter.h"
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

void TestAddPeerViaEventAndDuplicate() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(
      domain, "Alice");
  graph.app.Save();

  CHECK(graph.chat->peer_set.is_valid());
  CHECK(graph.peer_set.id() == graph.chat->peer_set.id());
  CHECK(graph.peer_set->peers.empty());

  auto const uid = MakeUid(0x11);
  auto const& peer = AddChatPeer(graph.peer_set, graph.chat.id(), uid);
  CHECK(peer.remote_uid == uid);
  CHECK(peer.session_state.is_valid());
  peer.session_state.Load();
  CHECK(peer.session_state.is_loaded());
  CHECK(peer.session_state->data.shared_root_id.id() ==
        ToObjId(ApplicationObjId::Chat));
  CHECK(graph.peer_set->peers.size() == 1);
  CHECK(graph.peer_set->journal.size() == 1);

  auto const session_id = peer.session_state.id();
  auto const& again = AddChatPeer(graph.peer_set, graph.chat.id(), uid);
  CHECK(again.session_state.id() == session_id);
  CHECK(graph.peer_set->peers.size() == 1);
  CHECK(graph.peer_set->journal.size() == 1);
}

void TestPeerPersistsAcrossReload() {
  ae::RamDomainStorage storage;
  ae::ObjId peer_set_id;
  ae::ObjId session_id;
  auto const uid = MakeUid(0x22);
  {
    ae::Domain domain{ae::Now(), storage};
    auto graph =
        examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                             FakeChatPresenter>(domain,
                                                                "Alice");
    graph.app.Save();
    auto const& peer = AddChatPeer(graph.peer_set, graph.chat.id(), uid);
    peer_set_id = graph.peer_set.id();
    session_id = peer.session_state.id();
    graph.peer_set.Save();
    graph.chat.Save();
    graph.app.Save();
  }

  ae::Domain reloaded{ae::Now(), storage};
  auto peer_set = ChatPeerSet::ptr::Declare(
      ae::CreateWith{reloaded}.with_id(peer_set_id));
  peer_set.Load();
  CHECK(peer_set.is_loaded());
  CHECK(peer_set->peers.size() == 1);
  CHECK(peer_set->peers[0].remote_uid == uid);
  CHECK(peer_set->peers[0].session_state.id() == session_id);
  peer_set->peers[0].session_state.Load();
  CHECK(peer_set->peers[0].session_state.is_loaded());
  CHECK(peer_set->peers[0].session_state->data.shared_root_id.id() ==
        ToObjId(ApplicationObjId::Chat));
}

void TestDiscoverSharedGraphExcludesLocalPeers() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(
      domain, "Alice");
  auto const& peer =
      AddChatPeer(graph.peer_set, graph.chat.id(), MakeUid(0x33));
  graph.app.Save();

  auto discovered = DiscoverSharedGraph(graph.chat);
  for (auto const& node : discovered) {
    CHECK(node.id() != graph.peer_set.id());
    CHECK(node.id() != peer.session_state.id());
  }
}

void TestChatPacketOmitsPeerSetAndUid() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto graph = examples::BuildSingleClientChatGraph<FakeWindow, FakeWindowPresenter,
                                                    FakeChatPresenter>(
      domain, "Alice");
  auto const uid = MakeUid(0x44);
  AddChatPeer(graph.peer_set, graph.chat.id(), uid);
  graph.chat.Save();
  graph.peer_set.Save();

  ae::RamDomainStorage build_storage;
  ae::Domain build_domain{ae::Now(), build_storage};
  CopyObjectGraph(graph.chat, storage, build_domain, build_storage,
                  SharedCopyMode::kCopyLoadedTargets);
  auto build_chat =
      Chat::ptr::Declare(ae::CreateWith{build_domain}.with_id(graph.chat.id()));
  build_chat.Load();
  CHECK(build_chat.is_loaded());

  auto packet = NodeStatePacket::ptr::Create(ae::CreateWith{build_domain});
  packet->node = build_chat;
  auto bytes = SyncPacketCodec{}.Encode(packet);
  auto decoded = SyncPacketCodec{}.Decode(bytes);
  auto node_packet = NodeStatePacket::ptr{decoded.packet};
  CHECK(node_packet->node.is_loaded());
  auto chat = Chat::ptr{node_packet->node};
  CHECK(chat.is_loaded());
  CHECK(!chat->peer_set.is_valid());

  // Encoded bytes must not contain the remote UID value pattern.
  auto const& uid_bytes = uid.value;
  bool found_uid = false;
  if (bytes.size() >= uid_bytes.size()) {
    for (std::size_t i = 0; i + uid_bytes.size() <= bytes.size(); ++i) {
      bool match = true;
      for (std::size_t j = 0; j < uid_bytes.size(); ++j) {
        if (bytes[i + j] != uid_bytes[j]) {
          match = false;
          break;
        }
      }
      if (match) {
        found_uid = true;
        break;
      }
    }
  }
  CHECK(!found_uid);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::EnsureSingleClientChatRegistration();
  apptraverse::test::TestAddPeerViaEventAndDuplicate();
  apptraverse::test::TestPeerPersistsAcrossReload();
  apptraverse::test::TestDiscoverSharedGraphExcludesLocalPeers();
  apptraverse::test::TestChatPacketOmitsPeerSetAndUid();
  std::cout << "chat_peer_set_test OK\n";
  return 0;
}
