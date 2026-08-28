#ifndef APPTRAVERSE_CHAT_IDS_H_
#define APPTRAVERSE_CHAT_IDS_H_

#include <cstdint>

#include "aether/obj/obj_id.h"

namespace apptraverse::chat {

enum class ChatObjId : ae::ObjId::Type {
  Application = 100000,
  ChatRoom = 100020,
  HostClient = 100021,
  HostDisplayName = 100022,
  LocalAetherIdentity = 100023,
  LocalAetherUidText = 100024,
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
inline constexpr std::int32_t kChatConnectionBarHeight = 40;
inline constexpr std::int32_t kChatConnectionBarButtonWidth = 72;
inline constexpr std::int32_t kChatConnectionBarLabelWidth = 120;

// Stable SelectClient name so restart reloads the same registered client.
inline constexpr char const* kAetherClientName = "chat-host";
// Registration parent used by Aether cloud examples / benches.
inline constexpr char const* kAetherParentUid =
    "3ac93165-3d37-4970-87a6-fa4ee27744e4";

}  // namespace apptraverse::chat

#endif  // APPTRAVERSE_CHAT_IDS_H_
