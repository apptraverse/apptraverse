#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "aether/types/uid.h"

#include "room_control.h"

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
  using apptraverse::chat::EncodeRoomControl;
  using apptraverse::chat::RoomControlMessage;
  using apptraverse::chat::RoomControlType;
  using apptraverse::chat::RoomParticipantDesc;
  using apptraverse::chat::TryDecodeRoomControl;

  RoomControlMessage hello{};
  hello.type = RoomControlType::kClientHello;
  hello.client_obj_id = 42;
  hello.display_name = "Peter";
  auto enc = EncodeRoomControl(hello);
  CHECK(!enc.empty());
  auto dec = TryDecodeRoomControl(enc);
  CHECK(dec.has_value());
  CHECK(dec->type == RoomControlType::kClientHello);
  CHECK(dec->client_obj_id == 42);
  CHECK(dec->display_name == "Peter");

  RoomControlMessage snap{};
  snap.type = RoomControlType::kMembershipSnapshot;
  snap.revision = 3;
  RoomParticipantDesc a{};
  a.uid = MakeUid(1);
  a.client_obj_id = 10;
  a.display_name = "HostUser";
  RoomParticipantDesc b{};
  b.uid = MakeUid(2);
  b.client_obj_id = 11;
  b.display_name = "ClientOne";
  snap.participants = {a, b};
  enc = EncodeRoomControl(snap);
  CHECK(!enc.empty());
  dec = TryDecodeRoomControl(enc);
  CHECK(dec.has_value());
  CHECK(dec->revision == 3);
  CHECK(dec->participants.size() == 2);
  CHECK(dec->participants[0].display_name == "HostUser");
  CHECK(dec->participants[1].uid == MakeUid(2));

  for (auto type : {RoomControlType::kMembershipPrepared,
                    RoomControlType::kMembershipApplied,
                    RoomControlType::kMembershipActivate,
                    RoomControlType::kMembershipActivated}) {
    RoomControlMessage m{};
    m.type = type;
    m.revision = 7;
    enc = EncodeRoomControl(m);
    CHECK(!enc.empty());
    dec = TryDecodeRoomControl(enc);
    CHECK(dec.has_value());
    CHECK(dec->type == type);
    CHECK(dec->revision == 7);
  }

  // Truncated / bad version / oversize name rejected.
  CHECK(!TryDecodeRoomControl(enc.data(), 3).has_value());
  auto bad = enc;
  bad[4] = 99;  // version
  CHECK(!TryDecodeRoomControl(bad).has_value());

  RoomControlMessage long_name{};
  long_name.type = RoomControlType::kClientHello;
  long_name.client_obj_id = 1;
  long_name.display_name = std::string(65, 'x');
  CHECK(EncodeRoomControl(long_name).empty());

  std::cout << "room_control_codec_test OK\n";
  return 0;
}
