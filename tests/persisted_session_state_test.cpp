#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/clock.h"
#include "aether/domain_storage/ram_domain_storage.h"

#include "apptraverse/node_for.h"
#include "apptraverse/object_link.h"
#include "apptraverse/object_macros.h"
#include "apptraverse/shared_graph.h"
#include "apptraverse/sync_session_state.h"

namespace apptraverse::test {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

class ProbeChat : public NodeFor<ProbeChat> {
  APPTRAVERSE_OBJECT(ProbeChat, Node, 0)

 protected:
  ProbeChat() = default;

 public:
  explicit ProbeChat(ae::ObjProp prop) : NodeFor{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(session))

  LocalPtr<SyncSessionState> session;
};

APPTRAVERSE_REGISTER(ProbeChat);

void TestCreateAndMutateViaEvent() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto state = CreateSyncSessionState(domain, ae::ObjId{42});
  state.Save();
  CHECK(state->data.shared_root_id.id() == 42);
  CHECK(!state->data.initial_sync_started);
  CHECK(state->journal.empty());

  SyncSessionData data = state->data;
  data.initial_sync_started = true;
  PendingSyncPacketState pending;
  pending.packet_id = ae::ObjId{9};
  pending.serialized_bytes = {1, 2, 3};
  pending.kind = PendingSyncPacketKind::kEvent;
  data.pending_packets.push_back(pending);

  auto event =
      SetSyncSessionDataEvent::ptr::Create(ae::CreateWith{domain});
  event->data = data;
  state->Commit(std::move(event));
  state.Save();

  CHECK(state->journal.size() == 1);
  CHECK(state->data.initial_sync_started);
  CHECK(state->data.pending_packets.size() == 1);
  CHECK(state->data.pending_packets[0].serialized_bytes ==
        (std::vector<std::uint8_t>{1, 2, 3}));

  ae::Domain reloaded{ae::Now(), storage};
  auto loaded = SyncSessionState::ptr::Declare(
      ae::CreateWith{reloaded}.with_id(state.id()));
  loaded.Load();
  CHECK(loaded.is_loaded());
  CHECK(loaded->data.initial_sync_started);
  CHECK(loaded->data.pending_packets[0].packet_id.id() == 9);
  CHECK(loaded->data.shared_root_id.id() == 42);
}

void TestNotDiscoveredThroughLocalPtr() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto chat_base = ProbeChat::ptr::Create(ae::CreateWith{domain}.with_id(1));
  auto chat = ProbeChat::ptr::Create(ae::CreateWith{domain}.with_id(2));
  chat->base = chat_base;
  chat->CaptureBaseState();
  auto state = CreateSyncSessionState(domain, chat.id());
  state.Save();
  chat->session = state;
  chat.Save();

  auto discovered = DiscoverSharedGraph(chat);
  CHECK(discovered.size() == 1);
  CHECK(discovered.front().id() == chat.id());
  for (auto const& node : discovered) {
    CHECK(node.id() != state.id());
  }
}

void TestPendingBeforeSendOrdering() {
  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto state = CreateSyncSessionState(domain, ae::ObjId{7});
  state.Save();

  SyncSessionData data = state->data;
  PendingSyncPacketState pending;
  pending.packet_id = ae::ObjId{11};
  pending.serialized_bytes = {9, 8, 7};
  pending.kind = PendingSyncPacketKind::kNodeState;
  pending.is_initial_state = true;
  data.pending_packets.push_back(pending);
  data.initial_sync_started = true;

  auto event =
      SetSyncSessionDataEvent::ptr::Create(ae::CreateWith{domain});
  event->data = data;
  state->Commit(std::move(event));
  state.Save();

  ae::Domain reloaded{ae::Now(), storage};
  auto loaded = SyncSessionState::ptr::Declare(
      ae::CreateWith{reloaded}.with_id(state.id()));
  loaded.Load();
  CHECK(loaded->data.pending_packets.size() == 1);
  CHECK(loaded->data.pending_packets[0].serialized_bytes ==
        (std::vector<std::uint8_t>{9, 8, 7}));
  CHECK(loaded->data.initial_sync_started);
  CHECK(!loaded->data.initial_sync_complete);
}

}  // namespace apptraverse::test

int main() {
  apptraverse::EnsureObjectRegistration();
  apptraverse::test::TestCreateAndMutateViaEvent();
  apptraverse::test::TestNotDiscoveredThroughLocalPtr();
  apptraverse::test::TestPendingBeforeSendOrdering();
  std::cout << "persisted_session_state_test OK\n";
  return 0;
}
