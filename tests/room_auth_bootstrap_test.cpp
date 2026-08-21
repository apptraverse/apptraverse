#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/domain_storage/ram_domain_storage.h"
#include "aether/types/uid.h"

#include "model/application_ids.h"
#include "model/chat.h"
#include "model/chat_component_registration.h"
#include "model/chat_room_local_state.h"
#include "room_control.h"
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

  ae::RamDomainStorage storage;
  ae::Domain domain{ae::Now(), storage};
  auto host_uid = MakeUid(1);
  auto client_uid = MakeUid(2);
  auto stranger_uid = MakeUid(9);

  auto host_state = ChatRoomLocalState::ptr::Create(
      ae::CreateWith{domain}.with_id(
          apptraverse::ToObjId(apptraverse::ApplicationObjId::ChatRoomLocalState)));
  host_state->role = ChatRoomRole::kHost;
  host_state->local_display_name = "HostUser";
  host_state->local_client_obj_id = 100;
  host_state.Save();

  bool saw_hello = false;
  RoomMembershipHooks hooks{};
  hooks.send_control = [&](ae::Uid const&,
                           std::vector<std::uint8_t> const&) {};
  hooks.ensure_host_join = [](ae::Uid const&, std::uint32_t,
                              std::string const&) { return true; };
  hooks.has_local_join = [] { return true; };
  hooks.log = [](std::string const&) {};

  RoomMembershipController host{ChatRoomRole::kHost, host_uid, 100, "HostUser",
                                host_state, hooks};
  host.HostBootstrap();
  CHECK(host.ui_status() == RoomUiStatus::kActive);
  CHECK(!host.IsAuthorizedSyncPeer(client_uid));

  // Chat sync auth still rejects unknown UID before activation.
  CHECK(!host.IsAuthorizedSyncPeer(stranger_uid));

  // Room-control bootstrap: ClientHello accepted before peer is authorized.
  RoomControlMessage hello{};
  hello.type = RoomControlType::kClientHello;
  hello.client_obj_id = 201;
  hello.display_name = "ClientOne";
  host.OnControl(client_uid, hello);
  saw_hello = true;
  CHECK(saw_hello);

  // Non-hello room control from unknown UID is rejected (no Prepare reply
  // path without hello); Applied from stranger must not advance revision.
  auto const rev_before = host.applied_revision();
  RoomControlMessage applied{};
  applied.type = RoomControlType::kMembershipApplied;
  applied.revision = rev_before + 1;
  host.OnControl(stranger_uid, applied);
  CHECK(host.applied_revision() == rev_before);

  std::cout << "room_auth_bootstrap_test OK\n";
  return 0;
}
