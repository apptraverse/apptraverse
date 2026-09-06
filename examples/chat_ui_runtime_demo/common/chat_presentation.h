#ifndef CHAT_PRESENTATION_H_
#define CHAT_PRESENTATION_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "chat_model.h"
#include "chat_presence.h"

namespace chat {

struct ChatFeedPresentationItem {
  std::uint32_t source_event_obj_id{0};
  std::string author_uid;
  std::string author_name;
  std::string text;
  std::uint32_t kind{0};
  std::int64_t sent_at_unix_ms{0};
  bool is_local_message{false};
  std::string display_line;
};

struct ChatContactPresentationItem {
  std::uint32_t client_obj_id{0};
  std::string display_name;
  std::string aether_uid;
  PresenceState presence{PresenceState::kUnknown};
  bool is_local{false};
};

struct ChatPresentationSnapshot {
  std::vector<ChatFeedPresentationItem> feed;
  std::vector<ChatContactPresentationItem> contacts;
};

struct ChatPresentationOptions {
  std::string local_aether_uid;
  std::function<std::optional<double>(std::uint32_t event_obj_id)>
      latency_ms_for_event;
};

std::string FormatUnixMsLocalTime(std::int64_t unix_ms);
std::string FormatChatJoinDisplayLine(std::string const& author);
std::string FormatChatMessageDisplayLine(std::string const& author,
                                         std::string const& body,
                                         std::int64_t sent_at_unix_ms);
std::string AppendUiLatencySuffix(std::string const& line, double latency_ms);

ChatPresentationSnapshot BuildChatPresentationSnapshot(
    ChatRoom const& room, ChatPresentationOptions const& options);

// Feed list scroll policy for RebuildFromDomain.
inline bool FeedListWasAtBottom(int top_index, int visible_items,
                                int count) noexcept {
  if (count <= 0) {
    return true;
  }
  if (visible_items < 1) {
    visible_items = 1;
  }
  return top_index + visible_items >= count - 1;
}

}  // namespace chat

#endif  // CHAT_PRESENTATION_H_
