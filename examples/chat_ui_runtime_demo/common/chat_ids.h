#ifndef CHAT_IDS_H_
#define CHAT_IDS_H_

#include <cstdint>

#include "aether-objects/obj/obj_id.h"

namespace chat {

enum class ChatObjId : ae::ObjId::Type {
  Application = 100000,
  ChatRoom = 100020,
  LocalDisplayName = 100022,
  ApplicationRuntime = 100025,
  NetworkState = 100026,
  AetherRegistration = 100027,
  WinPresentationApplication = 200000,
  WinChatWindowPresenter = 200010,
};

constexpr ae::ObjId::Type ToObjId(ChatObjId id) {
  return static_cast<ae::ObjId::Type>(id);
}

inline constexpr char const* kDefaultHostName = "Host";

inline constexpr std::int32_t kChatWindowLeft = 80;
inline constexpr std::int32_t kChatWindowTop = 80;
inline constexpr std::int32_t kChatWindowRight = 960;
inline constexpr std::int32_t kChatWindowBottom = 720;
inline constexpr std::int32_t kChatSidebarWidth = 180;
inline constexpr std::int32_t kChatInputHeight = 36;
inline constexpr std::int32_t kChatConnectionBarHeight = 44;
inline constexpr std::int32_t kChatConnectionBarButtonWidth = 88;
inline constexpr std::int32_t kChatConnectionBarLabelWidth = 80;

// Stable SelectClient name so restart reloads the same registered client.
inline constexpr char const* kAetherClientName = "chat-host";
// Registration parent used by Aether cloud examples / benches.
inline constexpr char const* kAetherParentUid =
    "3ac93165-3d37-4970-87a6-fa4ee27744e4";

}  // namespace chat

#endif  // CHAT_IDS_H_
