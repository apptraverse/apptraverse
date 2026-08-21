#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/types/uid.h"

#include "model/application_ids.h"
#include "model/chat_component_registration.h"
#include "model/chat_room_local_state.h"
#include "room_membership_controller.h"

namespace {

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

}  // namespace

int main() {
  apptraverse::EnsureChatComponentRegistration();
  using apptraverse::ChatRoomLocalState;
  using apptraverse::ChatRoomRole;
  using apptraverse::chat::RoomControlMessage;
  using apptraverse::chat::RoomControlType;
  using apptraverse::chat::RoomMembershipController;
  using apptraverse::chat::RoomMembershipHooks;
  using apptraverse::chat::RoomUiStatus;
  using apptraverse::chat::TryDecodeRoomControl;

  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};

  auto host_uid = MakeUid(1);
  auto c1_uid = MakeUid(2);
  auto c2_uid = MakeUid(3);

  auto host_state = ChatRoomLocalState::ptr::Create(
      ae::CreateWith{domain}.with_id(apptraverse::ToObjId(
          apptraverse::ApplicationObjId::ChatRoomLocalState)));
  host_state->role = ChatRoomRole::kHost;
  host_state->local_display_name = "HostUser";
  host_state->local_client_obj_id = 100;
  host_state.Save();

  ae::RamDomainStorage c1_storage;
  ae::Domain c1_domain{ae::Now(), c1_storage};
  auto c1_state = ChatRoomLocalState::ptr::Create(
      ae::CreateWith{c1_domain}.with_id(apptraverse::ToObjId(
          apptraverse::ApplicationObjId::ChatRoomLocalState)));
  c1_state->role = ChatRoomRole::kClient;
  c1_state->local_display_name = "ClientOne";
  c1_state->local_client_obj_id = 201;
  c1_state.Save();

  std::map<std::string,
           std::vector<std::pair<ae::Uid, std::vector<std::uint8_t>>>>
      mail;

  auto put = [&](ae::Uid to, ae::Uid from, std::vector<std::uint8_t> bytes) {
    mail[ae::Format("{}", to)].push_back({from, std::move(bytes)});
  };

  int host_joins = 0;
  int host_add_peer = 0;
  int client_add_peer = 0;
  std::vector<RoomControlType> host_out_types;
  std::vector<std::string> rejects;
  RoomMembershipHooks host_hooks{};
  host_hooks.send_control = [&](ae::Uid const& peer,
                                std::vector<std::uint8_t> const& bytes) {
    auto decoded = TryDecodeRoomControl(bytes);
    if (decoded) {
      host_out_types.push_back(decoded->type);
      if (decoded->type == RoomControlType::kMembershipReject) {
        rejects.push_back(decoded->display_name);
      }
    }
    put(peer, host_uid, bytes);
  };
  host_hooks.ensure_host_join = [&](ae::Uid const&, std::uint32_t,
                                    std::string const&) {
    ++host_joins;
    return true;
  };
  host_hooks.has_local_join = [] { return true; };
  host_hooks.add_chat_peer = [&](ae::Uid const&) { ++host_add_peer; };

  RoomMembershipController host{ChatRoomRole::kHost, host_uid, 100, "HostUser",
                                host_state, host_hooks};
  host.HostBootstrap();
  CHECK(host.applied_revision() == 1);
  CHECK(host.ui_status() == RoomUiStatus::kActive);
  CHECK(host.ActiveParticipants().size() == 1);

  bool c1_has_join = false;
  RoomMembershipHooks c1_hooks{};
  c1_hooks.send_control = [&](ae::Uid const& peer,
                              std::vector<std::uint8_t> const& bytes) {
    put(peer, c1_uid, bytes);
  };
  c1_hooks.has_local_join = [&] { return c1_has_join; };
  c1_hooks.add_chat_peer = [&](ae::Uid const&) { ++client_add_peer; };
  RoomMembershipController c1{ChatRoomRole::kClient, c1_uid, 201, "ClientOne",
                              c1_state, c1_hooks};

  auto deliver_all = [&] {
    bool progress = true;
    while (progress) {
      progress = false;
      for (auto& [key, q] : mail) {
        if (q.empty()) {
          continue;
        }
        auto item = std::move(q.front());
        q.erase(q.begin());
        progress = true;
        auto const& from = item.first;
        auto decoded = TryDecodeRoomControl(item.second);
        CHECK(decoded.has_value());
        if (key == ae::Format("{}", host_uid)) {
          host.OnControl(from, *decoded);
        } else if (key == ae::Format("{}", c1_uid)) {
          c1.OnControl(from, *decoded);
          if (decoded->type == RoomControlType::kMembershipSnapshot) {
            // Simulate own Join appearing via Chat sync after Snapshot/AddPeer.
            c1_has_join = true;
            c1.NotifyLocalJoinAppeared();
          }
        }
      }
    }
  };

  // Fresh Host+1 = Hello → Snapshot → Applied → Chat AddPeer → Join once.
  // Join is created on HostFinishActivation (after Applied/AddPeer), not before
  // Snapshot, so initial NodeState stays transport-safe.
  host_out_types.clear();
  c1.ClientConnect(host_uid);
  CHECK(c1.IsAuthorizedSyncPeer(host_uid));  // Host UID known after Connect.
  for (int i = 0; i < 40; ++i) {
    deliver_all();
    host.Tick(ae::Now());
    c1.Tick(ae::Now());
  }
  deliver_all();

  CHECK(host.applied_revision() == 2);
  CHECK(host_joins == 1);
  CHECK(c1.ui_status() == RoomUiStatus::kActive);
  CHECK(c1.applied_revision() == 2);
  CHECK(host.ActiveParticipants().size() == 2);
  CHECK(c1.CanSendChat());
  CHECK(client_add_peer >= 1);
  CHECK(host_add_peer >= 1);  // Host AddPeer at Snapshot (accepted identity)

  // Prepare/Activate must not appear on Host+1 critical outbound path.
  for (auto t : host_out_types) {
    CHECK(t != RoomControlType::kMembershipPrepare);
    CHECK(t != RoomControlType::kMembershipActivate);
  }
  bool saw_snapshot = false;
  for (auto t : host_out_types) {
    if (t == RoomControlType::kMembershipSnapshot) {
      saw_snapshot = true;
    }
  }
  CHECK(saw_snapshot);

  // 5: Duplicate ClientHello → reconnect, no Join/revision bump.
  auto const rev = host.applied_revision();
  auto const joins_before = host_joins;
  auto const host_add_before = host_add_peer;
  {
    RoomControlMessage hello{};
    hello.type = RoomControlType::kClientHello;
    hello.client_obj_id = 201;
    hello.display_name = "ClientOne";
    host.OnControl(c1_uid, hello);
  }
  for (int i = 0; i < 30; ++i) {
    deliver_all();
    host.Tick(ae::Now());
    c1.Tick(ae::Now());
  }
  deliver_all();
  CHECK(host.applied_revision() == rev);
  CHECK(host_joins == joins_before);
  CHECK(c1.ui_status() == RoomUiStatus::kActive);
  CHECK(host_add_peer >= host_add_before);  // idempotent AddPeer allowed

  // 6: Duplicate Snapshot/Applied idempotent.
  {
    RoomControlMessage snap{};
    snap.type = RoomControlType::kMembershipSnapshot;
    snap.revision = 2;
    snap.participants = host.ActiveParticipants();
    auto const status_before = c1.ui_status();
    c1.OnControl(host_uid, snap);
    CHECK(c1.ui_status() == status_before);
    CHECK(c1.applied_revision() == 2);
  }

  // Same UID, different ObjId → identity_mismatch.
  rejects.clear();
  {
    RoomControlMessage hello{};
    hello.type = RoomControlType::kClientHello;
    hello.client_obj_id = 999;
    hello.display_name = "ClientOne";
    host.OnControl(c1_uid, hello);
  }
  deliver_all();
  CHECK(!rejects.empty());
  CHECK(rejects.back() == "identity_mismatch");

  // Same ObjId, different UID → client_id_conflict.
  rejects.clear();
  {
    RoomControlMessage hello{};
    hello.type = RoomControlType::kClientHello;
    hello.client_obj_id = 201;
    hello.display_name = "Other";
    host.OnControl(c2_uid, hello);
  }
  deliver_all();
  CHECK(!rejects.empty());
  CHECK(rejects.back() == "client_id_conflict");

  // 12: Unknown UID still not authorized for Chat sync on Host.
  CHECK(!host.IsAuthorizedSyncPeer(MakeUid(9)));

  std::cout << "room_membership_test OK\n";
  return 0;
}
