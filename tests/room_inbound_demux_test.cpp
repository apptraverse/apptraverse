#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "aether/types/uid.h"

#include "chat_presence.h"
#include "room_control.h"
#include "room_inbound_demux.h"

namespace {

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::cerr << "CHECK failed: " #cond << " at " << __FILE__ << ":"       \
                << __LINE__ << '\n';                                         \
      std::exit(1);                                                          \
    }                                                                        \
  } while (0)

}  // namespace

int main() {
  using apptraverse::chat::ClassifyRoomControlInbound;
  using apptraverse::chat::EncodeChatPresence;
  using apptraverse::chat::EncodeRoomControl;
  using apptraverse::chat::RoomControlMessage;
  using apptraverse::chat::RoomControlType;
  using apptraverse::chat::RoomInboundKind;
  using apptraverse::chat::ChatPresenceMessage;

  RoomControlMessage hello{};
  hello.type = RoomControlType::kClientHello;
  hello.client_obj_id = 7;
  hello.display_name = "ClientOne";
  auto hello_bytes = EncodeRoomControl(hello);
  CHECK(!hello_bytes.empty());

  std::optional<RoomControlMessage> decoded;
  CHECK(ClassifyRoomControlInbound(hello_bytes, &decoded) ==
        RoomInboundKind::kRoomControlOk);
  CHECK(decoded.has_value());
  CHECK(decoded->display_name == "ClientOne");

  // Truncated ATRM magic must DecodeFail and never look like chat sync.
  std::vector<std::uint8_t> truncated{'A', 'T', 'R', 'M', 1};
  decoded.reset();
  CHECK(ClassifyRoomControlInbound(truncated, &decoded) ==
        RoomInboundKind::kRoomControlDecodeFail);
  CHECK(!decoded.has_value());

  // Presence / sync-looking payload without ATRM is NotRoomControl.
  auto presence = EncodeChatPresence(ChatPresenceMessage::kOnline);
  decoded.reset();
  CHECK(ClassifyRoomControlInbound(presence, &decoded) ==
        RoomInboundKind::kNotRoomControl);

  std::cout << "room_inbound_demux_test OK\n";
  return 0;
}
