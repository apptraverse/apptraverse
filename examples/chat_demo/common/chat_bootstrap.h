#ifndef APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_BOOTSTRAP_H_
#define APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_BOOTSTRAP_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "aether-miscpp/reflect/reflect.h"
#include "aether-objects/obj/obj_id.h"

namespace apptraverse::example::chat_demo {

inline constexpr std::size_t kMaxControlPayloadBytes = 1024;

enum class ChatBootstrapKind : std::uint8_t {
  kJoinRequest = 1,
  kJoinAccepted = 2,
  kJoinRejected = 3,
};

struct ChatBootstrapMessage {
  ChatBootstrapKind kind{ChatBootstrapKind::kJoinRequest};
  ae::ObjId attempt_id;
  ae::ObjId room_id;
  std::string rejection_reason;

  AE_REFLECT_MEMBERS(kind, attempt_id, room_id, rejection_reason)
};

bool EncodeChatBootstrap(ChatBootstrapMessage const& message,
                         std::vector<std::uint8_t>& out);
bool DecodeChatBootstrap(std::span<std::uint8_t const> bytes,
                         ChatBootstrapMessage& out);

}  // namespace apptraverse::example::chat_demo

#endif  // APPTRAVERSE_EXAMPLE_CHAT_DEMO_CHAT_BOOTSTRAP_H_
