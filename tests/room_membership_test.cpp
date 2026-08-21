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
  std::vector<std::string> rejects;
  RoomMembershipHooks host_hooks{};
  host_hooks.send_control = [&](ae::Uid const& peer,
                                std::vector<std::uint8_t> const& bytes) {
    auto decoded = TryDecodeRoomControl(bytes);
    if (decoded && decoded->type == RoomControlType::kMembershipReject) {
      rejects.push_back(decoded->display_name);
    }
    put(peer, host_uid, bytes);
  };
  host_hooks.ensure_host_join = [&](ae::Uid const&, std::uint32_t,
                                    std::string const&) {
    ++host_joins;
    return true;
  };
  host_hooks.has_local_join = [] { return true; };
  host_hooks.add_chat_peer = [](ae::Uid const&) {};

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
  c1_hooks.add_chat_peer = [](ae::Uid const&) {};
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
            c1_has_join = true;
            c1.NotifyLocalJoinAppeared();
          }
        }
      }
    }
  };

  // Host + one Client → revision 2, two participants, one Join.
  c1.ClientConnect(host_uid);
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

  // Duplicate ClientHello → reconnect, no Join/revision bump.
  auto const rev = host.applied_revision();
  auto const joins_before = host_joins;
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

  // Second distinct UID → room_full; existing membership unchanged.
  rejects.clear();
  {
    RoomControlMessage hello{};
    hello.type = RoomControlType::kClientHello;
    hello.client_obj_id = 202;
    hello.display_name = "ClientTwo";
    host.OnControl(c2_uid, hello);
  }
  deliver_all();
  CHECK(!rejects.empty());
  CHECK(rejects.back() == "room_full");
  CHECK(host.applied_revision() == 2);
  CHECK(host_joins == joins_before);
  CHECK(host.ActiveParticipants().size() == 2);
  CHECK(c1.ui_status() == RoomUiStatus::kActive);

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

  std::cout << "room_membership_test OK\n";
  return 0;
}
